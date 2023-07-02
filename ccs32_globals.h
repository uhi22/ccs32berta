
//extern int uart_write_bytes(int uart_num, const void* src, size_t size);

/* Logging verbosity settings */
//#define VERBOSE_INIT_ETH

/* Ethernet */
#define MY_ETH_TRANSMIT_BUFFER_LEN 250
#define MY_ETH_RECEIVE_BUFFER_LEN 3000
extern uint32_t nTotalEthReceiveBytes; /* total number of bytes which has been received from the ethernet port */
extern uint32_t nTotalTransmittedBytes;
extern uint8_t myethtransmitbuffer[MY_ETH_TRANSMIT_BUFFER_LEN];
extern uint16_t myethtransmitbufferLen; /* The number of used bytes in the ethernet transmit buffer */
extern uint8_t myethreceivebuffer[MY_ETH_RECEIVE_BUFFER_LEN];
extern uint16_t myethreceivebufferLen;
extern const uint8_t myMAC[6];
extern uint8_t nMaxInMyEthernetReceiveCallback, nInMyEthernetReceiveCallback;
extern uint16_t nTcpPacketsReceived;


/* TCP */
#define TCP_RX_DATA_LEN 200
extern uint8_t tcp_rxdataLen;
extern uint8_t tcp_rxdata[TCP_RX_DATA_LEN];

#define TCP_PAYLOAD_LEN 200
extern uint8_t tcpPayloadLen;
extern uint8_t tcpPayload[TCP_PAYLOAD_LEN];

/* V2GTP */
#define V2GTP_HEADER_SIZE 8 /* header has 8 bytes */

/* ConnectionManager */
#define CONNLEVEL_100_APPL_RUNNING 100
#define CONNLEVEL_80_TCP_RUNNING 80
#define CONNLEVEL_50_SDP_DONE 50
#define CONNLEVEL_20_TWO_MODEMS_FOUND 20
#define CONNLEVEL_15_SLAC_ONGOING 15
#define CONNLEVEL_10_ONE_MODEM_FOUND 10
#define CONNLEVEL_5_ETH_LINK_PRESENT 5


/* Charging behavior */
#define PARAM_U_DELTA_MAX_FOR_END_OF_PRECHARGE 20 /* [volts] The maximum voltage difference during PreCharge, to close the relay. */
#define isLightBulbDemo 1 /* Activates the special behavior for light-bulb-demo-charging */
#define USE_EVSEPRESENTVOLTAGE_FOR_PRECHARGE_END /* to configure, which criteria is used for end of PreCharge */


/* functions */
#if defined(__cplusplus)
extern "C"
{
#endif
void addToTrace_chararray(char *s);
#if defined(__cplusplus)
}
#endif



