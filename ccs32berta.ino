
/* ESP32 with OLED Display and PLC modem QCA7000 via SPI
 *  
 *  Hardware:
 *     1. HTIT-WB32, also known as heltec wifi kit 32.
 *        Arduino board name: WiFi Kit 32
 *        Version for Arduino IDE 2.0.4 (March 2023) together with
 *        Heltec board "Heltec ESP32 Series Dev-boards by Heltec" Version 0.0.7
 *        From board manager url
 *        https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/releases/download/0.0.7/package_heltec_esp32_index.json
 *
 *     2. Qualcomm QCA7000 or QCA7005
 *  
 *  Functionality:
 *   - Demo for Reading the software version of the QCA7000 via SPI, and showing this on the OLED.
 *    This is just a very basic demonstration. Many things need to be improved, e.g.
 *      - check for the status flags, evaluate the interrupt line, react on status flags and buffer sizes.
 *     
 */

#include <SPI.h>

/* The QCA7000 is connected via SPI to the ESP32. */
/* SPI pins of the ESP32 VSPI (The HSPI pins are already occupied by the OLED.) */
#define VSPI_MISO   MISO /* 19 */
#define VSPI_MOSI   MOSI /* 23 */
#define VSPI_SCLK   SCK /* 18 */
#define VSPI_SS     SS  /* 5 */
SPIClass * vspi = NULL;
static const int spiClk = 2000000; // 2 MHz
  
#define USE_OLED
#ifdef USE_OLED
  /* The OLED uses I2C. For a connection via I2C using the Arduino Wire include: */
  #include <Wire.h>               
  #include "HT_SSD1306Wire.h"
  SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst
#endif

int n_loops;
int nDebug;
uint8_t mySpiRxBuffer[4000];
uint8_t mySpiTxBuffer[300];
#define MY_ETH_TRANSMIT_BUFFER_LEN 250
uint8_t myethtransmitbuffer[MY_ETH_TRANSMIT_BUFFER_LEN];
uint16_t myethtransmitbufferLen=0; /* The number of used bytes in the ethernet transmit buffer */
#define MY_ETH_RECEIVE_BUFFER_LEN 250
uint8_t myethreceivebuffer[MY_ETH_RECEIVE_BUFFER_LEN];
uint16_t myethreceivebufferLen=0; /* The number of used bytes in the ethernet receive buffer */
const uint8_t MAC_BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t myMAC[6] = {0xFE, 0xED, 0xBE, 0xEF, 0xAF, 0xFE};
char strVersion[200];
uint8_t verLen;
uint8_t sourceMac[6];

String line1 = "Hello";
String line2 = "Init...";
String line3 = "...test";
String line4 = "1234...";


void setup() {
  /* initialise instance of the SPIClass attached to VSPI */
  vspi = new SPIClass(VSPI);
  vspi->begin();
  /* set up slave select pins as outputs as the Arduino API doesn't handle
     automatically pulling SS low */
  pinMode(vspi->pinSS(), OUTPUT);

  Serial.begin(9600);
  Serial.print("Hello World.\n");
  
  #ifdef USE_OLED
    display.init();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
  #endif  
  Serial.print("Setup finished.\n");
}

void spiQCA7000DemoReadSignature(void) {
  /* Demo for reading the signature of the QCA7000. This should show AA55. */
  uint16_t sig;
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
  digitalWrite(vspi->pinSS(), LOW);
  (void)vspi->transfer(0xDA); /* Read, internal, reg 1A (SIGNATURE) */
  (void)vspi->transfer(0x00);
  sig = vspi->transfer(0x00);
  sig <<= 8;
  sig += vspi->transfer(0x00);
  digitalWrite(vspi->pinSS(), HIGH);
  vspi->endTransaction();
  Serial.println(String(sig, HEX));  /* should be AA 55  */
}

void spiQCA7000DemoReadWRBUF_SPC_AVA(void) {
  /* Demo for reading the available write buffer size from the QCA7000 */
  int i;
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
  digitalWrite(vspi->pinSS(), LOW);
  mySpiRxBuffer[0] = vspi->transfer(0xC2);
  mySpiRxBuffer[1] = vspi->transfer(0x00);
  mySpiRxBuffer[2] = vspi->transfer(0x00);
  mySpiRxBuffer[3] = vspi->transfer(0x00);
  digitalWrite(vspi->pinSS(), HIGH);
  vspi->endTransaction();
  String s;
  s = "WRBUF_SPC_AVA: ";
  for (i=0; i<4; i++) {
    s = s + String(mySpiRxBuffer[i], HEX) + " ";
  }
  Serial.println(s);  
}

void spiQCA7000DemoWriteBFR_SIZE(uint16_t n) {
  /* Demo for writing the write buffer size to the QCA7000 */
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
  digitalWrite(vspi->pinSS(), LOW);
  (void) vspi->transfer(0x41); /* 0x41 is write, internal, reg 1 */
  (void) vspi->transfer(0x00);
  (void) vspi->transfer(n>>8);
  (void) vspi->transfer(n);
  digitalWrite(vspi->pinSS(), HIGH);
  vspi->endTransaction(); 
}


uint16_t spiQCA7000DemoReadRDBUF_BYTE_AVA(void) {
  /* Demo for retrieving the amount of available received data from QCA7000 */
  uint16_t n;
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
  digitalWrite(vspi->pinSS(), LOW);
  (void)vspi->transfer(0xC3); /* 0xC3 is read, internal, reg 3 RDBUF_BYTE_AVA */
  (void)vspi->transfer(0x00);
  n = vspi->transfer(0x00); /* upper byte of the size */
  n<<=8;  
  n+=vspi->transfer(0x00); /* lower byte of the size */
  digitalWrite(vspi->pinSS(), HIGH);
  vspi->endTransaction(); 
  return n;
}

void evaluateGetSwCnf(void) {
    /* The GET_SW confirmation. This contains the software version of the homeplug modem.
       Reference: see wireshark interpreted frame from TPlink, Ioniq and Alpitronic charger */
    uint8_t i, x;  
    String strMac, StringVersion;    
    Serial.println("[PEVSLAC] received GET_SW.CNF");
    for (i=0; i<6; i++) {
        sourceMac[i] = myethreceivebuffer[6+i];
    }
    strMac = String(sourceMac[0], HEX) + ":" + String(sourceMac[1], HEX) + ":" + String(sourceMac[2], HEX) + ":" 
     + String(sourceMac[3], HEX) + ":" + String(sourceMac[4], HEX) + ":" + String(sourceMac[5], HEX);
    verLen = myethreceivebuffer[22];
    if ((verLen>0) && (verLen<0x30)) {
      for (i=0; i<verLen; i++) {
            x = myethreceivebuffer[23+i];
            if (x<0x20) { x=0x20; } /* make unprintable character to space. */
            strVersion[i]=x;            
      }
      strVersion[i] = 0; 
      StringVersion = String(strVersion);
      Serial.println("For " + strMac + " the software version is " + StringVersion);
      /* As demo, show the modems software version on the OLED display, splitted in four lines: */      
      line1 = StringVersion.substring(0, 11);
      line2 = StringVersion.substring(11, 22);
      line3 = StringVersion.substring(22, 33);
      line4 = StringVersion.substring(33, 44);
  }        
}

void spiQCA7000checkForReceivedData(void) {
  /* checks whether the QCA7000 indicates received data, and if yes, fetches the data. */
  uint16_t availBytes;
  uint16_t i, L1, L2;
  availBytes = spiQCA7000DemoReadRDBUF_BYTE_AVA();
  Serial.println("avail RX bytes: " + String(availBytes));
  if (availBytes>0) {
    /* If the QCA indicates that the receive buffer contains data, the following procedure
    is necessary to get the data (according to https://chargebyte.com/assets/Downloads/an4_rev5.pdf)
       - write the BFR SIZE, this sets the length of data to be read via external read
       - start an external read and receive as much data as set in SPI REG BFR SIZE before */
    spiQCA7000DemoWriteBFR_SIZE(availBytes);
    vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
    digitalWrite(vspi->pinSS(), LOW);
    (void)vspi->transfer(0x80); /* 0x80 is read, external */
    (void)vspi->transfer(0x00);
    for (i=0; i<availBytes; i++) {
      mySpiRxBuffer[i] = vspi->transfer(0x00); /* loop over all the receive data */
    }
    /* the SpiRxBuffer contains more than the ethernet frame:
       4 byte length
       4 byte start of frame AA AA AA AA
       2 byte frame length, little endian
       2 byte reserved 00 00
      payload
      2 byte End of frame, 55 55 */
    /* The higher 2 bytes of the len are assumed to be 0. */
    /* The lower two bytes of the "outer" len, big endian: */       
    L1 = mySpiRxBuffer[2]; L1<<=8; L1+=mySpiRxBuffer[3];
    /* The "inner" len, little endian. */
    L2 = mySpiRxBuffer[9]; L2<<=8; L2+=mySpiRxBuffer[8];
    if ((mySpiRxBuffer[4]=0xAA) && (mySpiRxBuffer[5]=0xAA) && (mySpiRxBuffer[6]=0xAA) && (mySpiRxBuffer[7]=0xAA) 
        && (L2+10==L1)) {
      /* The start of frame and the two length informations are plausible. Copy the payload to the eth receive buffer. */
      myethreceivebufferLen = L2;
      /* but limit the length, to avoid buffer overflow */       
      if (myethreceivebufferLen > MY_ETH_RECEIVE_BUFFER_LEN) {
          myethreceivebufferLen = MY_ETH_RECEIVE_BUFFER_LEN;
      }
      memcpy(myethreceivebuffer, &mySpiRxBuffer[12], myethreceivebufferLen);
      String s;
      String strSwVersion;      
      s = "rx data (" + String(myethreceivebufferLen) + " bytes): ";
      for (i=0; i<myethreceivebufferLen; i++) {
        s = s + String(myethreceivebuffer[i], HEX) + " ";
      }
      Serial.println(s);
      evaluateGetSwCnf();      
    }
    digitalWrite(vspi->pinSS(), HIGH);
    vspi->endTransaction();     
  }
}

void cleanTransmitBuffer(void) {
  /* fill the complete ethernet transmit buffer with 0x00 */
  int i;
  for (i=0; i<MY_ETH_TRANSMIT_BUFFER_LEN; i++) {
    myethtransmitbuffer[i]=0;
  }
}

void fillSourceMac(const uint8_t *mac, uint8_t offset=6) {
 /* at offset 6 in the ethernet frame, we have the source MAC.
    we can give a different offset, to re-use the MAC also in the data area */
  memcpy(&myethtransmitbuffer[offset], mac, 6); 
}

void fillDestinationMac(const uint8_t *mac, uint8_t offset=0) {
 /* at offset 0 in the ethernet frame, we have the destination MAC.
    we can give a different offset, to re-use the MAC also in the data area */
  memcpy(&myethtransmitbuffer[offset], mac, 6); 
}

void composeGetSwReq(void) {
	/* GET_SW.REQ request, as used by the win10 laptop */
    myethtransmitbufferLen = 60;
    cleanTransmitBuffer();
    /* Destination MAC */
    fillDestinationMac(MAC_BROADCAST);
    /* Source MAC */
    fillSourceMac(myMAC);
    /* Protocol */
    myethtransmitbuffer[12]=0x88; // Protocol HomeplugAV
    myethtransmitbuffer[13]=0xE1; //
    myethtransmitbuffer[14]=0x00; // version
    myethtransmitbuffer[15]=0x00; // GET_SW.REQ
    myethtransmitbuffer[16]=0xA0; // 
    myethtransmitbuffer[17]=0x00; // Vendor OUI
    myethtransmitbuffer[18]=0xB0; // 
    myethtransmitbuffer[19]=0x52; //  
}

void spiQCA7000SendEthFrame(void) {
  /* to send an ETH frame, we need two steps:
     1. Write the BFR_SIZE (internal reg 1)
     2. Write external, preamble, size, data */
/* Example (from CCM)
  The SlacParamReq has 60 "bytes on wire" (in the Ethernet terminology).
  The   BFR_SIZE is set to 0x46 (command 41 00 00 46). This is 70 decimal.
  The transmit command on SPI is
  00 00  
  AA AA AA AA
  3C 00 00 00    (where 3C is 60, matches the "bytes on wire")
  <60 bytes payload>
  55 55 After the footer, the frame is finished according to the qca linux driver implementation.
  xx yy But the Hyundai CCM sends two bytes more, either 00 00 or FE 80 or E1 FF or other. Most likely not relevant.
  Protocol explanation from https://chargebyte.com/assets/Downloads/an4_rev5.pdf 
*/
  spiQCA7000DemoWriteBFR_SIZE(myethtransmitbufferLen+10); /* The size in the BFR_SIZE is 10 bytes more than in the size after the preamble below (in the original CCM trace) */
  mySpiTxBuffer[0] = 0x00; /* external write command */
  mySpiTxBuffer[1] = 0x00;
  mySpiTxBuffer[2] = 0xAA; /* Start of frame */
  mySpiTxBuffer[3] = 0xAA;
  mySpiTxBuffer[4] = 0xAA;
  mySpiTxBuffer[5] = 0xAA;
  mySpiTxBuffer[6] = (uint8_t)myethtransmitbufferLen; /* LSB of the length */
  mySpiTxBuffer[7] = myethtransmitbufferLen>>8; /* MSB of the length */
  mySpiTxBuffer[8] = 0x00; /* to bytes reserved, 0x00 */
  mySpiTxBuffer[9] = 0x00;
  memcpy(&mySpiTxBuffer[10], myethtransmitbuffer, myethtransmitbufferLen); /* the ethernet frame */
  mySpiTxBuffer[10+myethtransmitbufferLen] = 0x55; /* End of frame, 2 bytes with 0x55. Aka QcaFrmCreateFooter in the linux driver */
  mySpiTxBuffer[11+myethtransmitbufferLen] = 0x55;
  int i;
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
  digitalWrite(vspi->pinSS(), LOW);
  for (i=0; i<12+myethtransmitbufferLen; i++) {
    (void) vspi->transfer(mySpiTxBuffer[i]);
  }
  digitalWrite(vspi->pinSS(), HIGH);
  vspi->endTransaction();   
}

void demoQCA7000SendSoftwareVersionRequest(void) {
  composeGetSwReq();
  spiQCA7000SendEthFrame(); 
}

void loop() {
  spiQCA7000DemoReadSignature();
  spiQCA7000DemoReadWRBUF_SPC_AVA();
  demoQCA7000SendSoftwareVersionRequest();
  spiQCA7000checkForReceivedData();
  spiQCA7000checkForReceivedData();  
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0,   0, line1);
  display.drawString(0,  16, line2);
  display.drawString(0,  32, line3);
  display.drawString(0,  48, line4);
  display.display();
  delay(1000);
  n_loops++;
}
