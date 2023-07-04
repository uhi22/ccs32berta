/* CCS Charging with WT32-ETH01 and HomePlug modem */
/* This is the main Arduino file of the project. */
/* Developed in Arduino IDE 2.0.4 */

/* Modularization concept:
- The ccs32.ino is the main Arduino file of the project.
- Some other .ino files are present, and the Arduino IDE will merge all the .ino into a
  single cpp file before compiling. That's why, all the .ino share the same global context.
- Some other files are "hidden" in the src folder. These are not shown in the Arduino IDE,
  but the Arduino IDE "knows" them and will compile and link them. You may want to use
  an other editor (e.g. Notepad++) for editing them.
- We define "global variables" as the data sharing concept. The two header files "ccs32_globals.h"
  and "projectExiConnector.h" declare the public data and public functions.
- Using a mix of cpp and c modules works; the only requirement is to use the special "extern C" syntax in
  the header files.
*/

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

#include "ccs32_globals.h"
#include "src/exi/projectExiConnector.h"

/**********************************************************/
/* extern variables for debugging */
//extern uint32_t uwe_rxMallocAccumulated;
//extern uint32_t uwe_rxCounter;
/**********************************************************/
#define PIN_STATE_C 4 /* The IO4 is used to change the CP line to state C. High=StateC, Low=StateB */ 
#define PIN_POWER_RELAIS 14 /* IO14 for the power relay */
uint32_t currentTime;
uint32_t lastTime1s;
uint32_t lastTime30ms;
uint32_t nCycles30ms;
uint8_t ledState;
uint32_t initialHeapSpace;
uint32_t eatenHeapSpace;
String globalLine1;
String globalLine2;
String globalLine3;
uint16_t counterForDisplayUpdate;


#define USE_OLED
#ifdef USE_OLED
  /* The OLED uses I2C. For a connection via I2C using the Arduino Wire include: */
  #include <Wire.h>               
  #include "HT_SSD1306Wire.h"
  SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst
#endif

int n_loops;
int nDebug;


String OledLine1 = "Hello";
String OledLine2 = "Init...";
String OledLine3 = "...test";
String OledLine4 = "1234...";


void sanityCheck(String info) {
  int r;
  r= hardwareInterface_sanityCheck();
  r=r | homeplug_sanityCheck();
  if (eatenHeapSpace>10000) {
    /* if something is eating the heap, this is a fatal error. */
    addToTrace("ERROR: Sanity check failed due to heap space check.");
    r = -10;
  }
  if (r!=0) {
      addToTrace(String("ERROR: Sanity check failed ") + String(r) + " " + info);
      delay(2000); /* Todo: we should make a reset here. */
  }
}
  


/**********************************************************/
/* The logging macros and functions */
#undef log_v
#undef log_e
#define log_v(format, ...) log_printf(ARDUHAL_LOG_FORMAT(V, format), ##__VA_ARGS__)
#define log_e(format, ...) log_printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__)

void addToTrace_chararray(char *s) {
  log_v("%s", s);  
}

void addToTrace(String strTrace) {
  //Serial.println(strTrace);  
  log_v("%s", strTrace.c_str());  
}

void showAsHex(uint8_t *arr, uint16_t len, char *info) {
 char strTmp[10];
 #define MAX_RESULT_LEN 700
 char strResult[MAX_RESULT_LEN];
 uint16_t i;
 sprintf(strResult, "%s has %d bytes:", info, len);
 for (i=0; i<len; i++) {
  sprintf(strTmp, "%02hx ", arr[i]);
  if (strlen(strResult)<MAX_RESULT_LEN-10) {  
    strcat(strResult, strTmp);
  } else {
    /* does not fit. Just ignore the remaining bytes. */
  }
 }
 addToTrace_chararray(strResult);
} 

/**********************************************************/
/* The global status printer */
void publishStatus(String line1, String line2 = "", String line3 = "") {
  globalLine1=line1;
  globalLine2=line2;
  globalLine3=line3;
}  

void cyclicLcdUpdate(void) {
  uint32_t t;
  uint16_t minutes, seconds;
  String strMinutes, strSeconds, strLine3extended;
  if (counterForDisplayUpdate>0) {
    counterForDisplayUpdate--;  
  } else {
    /* show the uptime in the third line */  
    t = millis()/1000;
    minutes = t / 60;
    seconds = t - (minutes*60);
    strMinutes = String(minutes);
    strSeconds = String(seconds);  
    if (strMinutes.length()<2) strMinutes = "0" + strMinutes;
    if (strSeconds.length()<2) strSeconds = "0" + strSeconds;
    strLine3extended = globalLine3 + " " + strMinutes + ":" + strSeconds;
    OledLine1 = globalLine1;
    OledLine2 = globalLine2;
    OledLine3 = strLine3extended;
    OledLine4 = "test";
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0,   0, OledLine1);
    display.drawString(0,  16, OledLine2);
    display.drawString(0,  32, OledLine3);
    display.drawString(0,  48, OledLine4);
    display.display();
    counterForDisplayUpdate=15; /* 15*30ms=450ms until forced cyclic update of the LCD */  
  }
}

/**********************************************************/
/* The tasks */

/* This task runs each 30ms. */
void task30ms(void) {
  nCycles30ms++;
  spiQCA7000checkForReceivedData();  
  connMgr_Mainfunction(); /* ConnectionManager */
  modemFinder_Mainfunction();
  runSlacSequencer();
  runSdpStateMachine();
  tcp_Mainfunction();
  pevStateMachine_Mainfunction();
  cyclicLcdUpdate();
  sanityCheck("cyclic30ms");
}

/* This task runs once a second. */
void task1s(void) {
  if (ledState==0) {
    //digitalWrite(PIN_LED,HIGH);
    //Serial.println("LED on");
    ledState = 1;
  } else {
    //digitalWrite(PIN_LED,LOW);
    //Serial.println("LED off");
    ledState = 0;
  }
  //log_v("nTotalEthReceiveBytes=%ld, nCycles30ms=%ld", nTotalEthReceiveBytes, nCycles30ms);
  //log_v("nTotalEthReceiveBytes=%ld, nMaxInMyEthernetReceiveCallback=%d, nTcpPacketsReceived=%d", nTotalEthReceiveBytes, nMaxInMyEthernetReceiveCallback, nTcpPacketsReceived);
  //log_v("nTotalTransmittedBytes=%ld", nTotalTransmittedBytes);
  //tcp_testSendData(); /* just for testing, send something with TCP. */
  //sendTestFrame(); /* just for testing, send something on the Ethernet. */
  eatenHeapSpace = initialHeapSpace - ESP.getFreeHeap();
  //Serial.println("EatenHeapSpace=" + String(eatenHeapSpace) + " uwe_rxCounter=" + String(uwe_rxCounter) + " uwe_rxMallocAccumulated=" + String(uwe_rxMallocAccumulated) );
  if (eatenHeapSpace>1000) {
    /* if we lost more than 1000 bytes on heap, print a waring message: */
    Serial.println("WARNING: EatenHeapSpace=" + String(eatenHeapSpace));
  }

  //demoQCA7000(); 
}

/**********************************************************/
/* The Arduino standard entry points */

void setup() {
  Serial.begin(115200);
  Serial.println("CCS32berta Started.");

  // Set pin mode
  //pinMode(PIN_LED,OUTPUT);
  pinMode(PIN_STATE_C, OUTPUT);
  pinMode(PIN_POWER_RELAIS, OUTPUT);
  digitalWrite(PIN_POWER_RELAIS, HIGH); /* deactivate relais */
  delay(500); /* wait for power inrush, to avoid low-voltage during startup if we would switch the relay here. */

  qca7000setup();
  
  #ifdef USE_OLED
    display.init();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
  #endif
  homeplugInit();
  pevStateMachine_Init();
  Serial.println("Relay test.");
  digitalWrite(PIN_POWER_RELAIS, LOW); /* activate relais as test */
  delay(500);
  digitalWrite(PIN_POWER_RELAIS, HIGH); /* deactivate relais */
  /* The time for the tasks starts here. */
  currentTime = millis();
  lastTime30ms = currentTime;
  lastTime1s = currentTime;
  log_v("Setup finished.");
  initialHeapSpace=ESP.getFreeHeap();

  Serial.println("Setup finished.");
}



void loop() {
  /* a simple scheduler which calls the cyclic tasks depending on system time */
  currentTime = millis();
  if ((currentTime - lastTime30ms)>30) {
    lastTime30ms += 30;
    task30ms();
  }
  if ((currentTime - lastTime1s)>1000) {
    lastTime1s += 1000;
    task1s();
  }
}


