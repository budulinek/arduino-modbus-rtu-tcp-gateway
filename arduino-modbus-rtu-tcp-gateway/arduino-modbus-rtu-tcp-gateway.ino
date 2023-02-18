/* Arduino-based Modbus RTU (slaves) to Modbus TCP/UDP (master) gateway with web interface
   - slaves are connected via RS485 interface
   - master(s) are connected via ethernet interface
   - up to 247 Modbus RTU slaves
   - up to 8 TCP/UDP sockets for Modbus TCP/UDP masters and web interface
   - RS485 interface protocols:
            o Modbus RTU
   - Ethernet interface protocols:
            o Modbus TCP
            o Modbus UDP
            o Modbus RTU over TCP
            o Modbus RTU over UDP
   - supports broadcast (slave address 0x00) and error codes
   - supports all Modbus function codes
   - settings can be changed via web interface, stored in EEPROM
   - diagnostics and Modbus RTU scan via web interface
   - optimized queue for Modbus requests
            o prioritization of requests to responding slaves
            o queue will accept only one requests to non-responding slaves

  Connections:
  Arduino <-> MAX485 module
  Tx1 <-> DI
  Rx0 <-> RO
  Pin 6 <-> DE,RE

  Version history
  v0.1 2020-04-05 Initial commit
  v0.2 2021-03-02 Random MAC generation
  v1.0 2021-03-20 Add web interface, settings stored in EEPROM
  v2.0 2021-04-01 Improve random MAC algorithm (Marsaglia algorithm from https://github.com/RobTillaart/randomHelpers),
                  replace some libraries with more efficient code, compatibility with Arduino Mega
  v2.1 2021-04-12 Code optimisation
  v2.2 2021-06-06 Fix TCP closed socket, support RS485 modules with hardware automatic flow control
  v2.3 2021-09-10 Fix IPAddress cast (gateway freeze)
  v2.4 2021-10-15 Add SW version. Forced factory reset (load defaut settings from sketch) on MAJOR version change.
  v3.0 2021-11-07 Improve POST parameters processing, bugfix 404 and 204 error headers. 
  v3.1 2022-01-28 Code optimization, bugfix DHCP settings.
  v3.2 2022-06-04 Reduce program size (so that it fits on Nano), ethernet data counter only available when ENABLE_EXTRA_DIAG.
  v4.0 2023-01-05 Modbus statistics and error reporting on "Modbus Status" page, add Frame Delay setting for Modbus RTU
                  Optimize Modbus timeout and attempts, significant reduction of code size
  v4.1 2023-01-14 Fetch API, bugfix MAX485
  v5.0 2023-01-19 Send Modbus Request from WebUI, optimized POST parameter processing (less RAM consumption), select baud rate in WebUI

*/

const byte VERSION[] = { 5, 0 };

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <CircularBuffer.h>  // CircularBuffer https://github.com/rlogiacco/CircularBuffer
#include <EEPROM.h>
#include <StreamLib.h>  // StreamLib https://github.com/jandrassy/StreamLib

// these are used by CreateTrulyRandomSeed() function
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/atomic.h>


/****** ADVANCED SETTINGS ******/

const byte MAX_QUEUE_REQUESTS = 10;                  // max number of TCP or UDP requests stored in a queue
const int MAX_QUEUE_DATA = 256;                      // total length of TCP or UDP requests stored in a queue (in bytes)
const byte MAX_SLAVES = 247;                         // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for reserved addresses)
const int MODBUS_SIZE = 256;                         // size of a MODBUS RTU frame (determines size of various buffers)
#define mySerial Serial                              // define serial port for RS485 interface, for Arduino Mega choose from Serial1, Serial2 or Serial3
#define RS485_CONTROL_PIN 6                          // Arduino Pin for RS485 Direction control, disable if you have module with hardware flow control
const byte ETH_RESET_PIN = 7;                        // Ethernet shield reset pin (deals with power on reset issue of the ethernet shield)
const unsigned int TCP_WEB_DISCON_AGE = 300;         // Minimum age (ms) from last client data at which TCP socket could be disconnected, non-blocking. TCP_DISCON_MIN_AGE should be long enough to flush TCP read/write buffers.
const unsigned int TCP_DISCON_TIMEOUT = 500;         // Timeout (ms) for client DISCON socket command, non-blocking alternative to https://www.arduino.cc/reference/en/libraries/ethernet/client.setconnectiontimeout/
const unsigned int TCP_RETRANSMISSION_TIMEOUT = 50;  // Ethernet controller’s timeout (ms), blocking (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissiontimeout/)
const byte TCP_RETRANSMISSION_COUNT = 3;             // Number of transmission attempts the Ethernet controller will make before giving up (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissioncount/)
const unsigned int SCAN_TIMEOUT = 200;               // Timeout (ms) for Modbus scan requests
const byte SCAN_FUNCTION_FIRST = 0x03;               // Function code sent during Modbus RTU Scan request (first attempt)
const byte SCAN_FUNCTION_SECOND = 0x04;              // Function code sent during Modbus RTU Scan request (second attempt)
const byte SCAN_DATA_ADDRESS = 0x01;                 // Data address sent during Modbus RTU Scan request (both attempts)
// Slave is detected as "Responding" if any response (even error) is received.
const int FETCH_INTERVAL = 2000;                                                     // Fetch API interval (ms) for the Modbus Status webpage to renew data from JSON served by Arduino
const byte MAX_RESPONSE_LEN = 16;                                                    // Max length (bytes) of the Modbus response shown in WebUI
const unsigned int BAUD_RATES[] = { 3, 6, 9, 12, 24, 48, 96, 192, 384, 576, 1152 };  // List of baud rates (divided by 100) available in WebUI. Feel free to add your custom baud rate (anything between 3 and 2500)

/****** EXTRA FUNCTIONS ******/

// these do not fit into the limited flash memory of Arduino Uno/Nano, uncomment if you have a board with more memory
// #define ENABLE_DHCP            // Enable DHCP (Auto IP settings)
// #define ENABLE_EXTRA_DIAG      // Enable Ethernet and Serial byte counter.
// #define TEST_SOCKS

/****** DEFAULT FACTORY SETTINGS ******/

typedef struct {
  byte macEnd[3];
  byte ip[4];
  byte subnet[4];
  byte gateway[4];
  unsigned int tcpPort;
  unsigned int udpPort;
  unsigned int webPort;
  bool enableRtuOverTcp;
  unsigned int tcpTimeout;
  unsigned int baud;
  byte serialConfig;
  byte frameDelay;
  unsigned int serialTimeout;
  byte serialAttempts;
} config_type;

/*
  Please note that after boot, Arduino loads settings stored in EEPROM, even if you flash new program to it!

  Arduino loads factory defaults specified bellow in case:
  1) User clicks "Restore" defaults in WebUI (factory reset configuration, keeps MAC)
  2) VERSION_MAJOR changes (factory reset configuration AND generates new MAC)
*/

const config_type DEFAULT_CONFIG = {
  {},                    // macEnd (last 3 bytes)
  { 192, 168, 1, 254 },  // ip
  { 255, 255, 255, 0 },  // subnet
  { 192, 168, 1, 1 },    // gateway
  502,                   // tcpPort
  502,                   // udpPort
  80,                    // webPort
  false,                 // enableRtuOverTcp
  600,                   // tcpTimeout
  96,                    // baud / 100
  SERIAL_8E1,            // serialConfig (Modbus RTU default is 8E1, another frequently used option is 8N2)
  150,                   // frameDelay
  500,                   // serialTimeout
  3                      // serialAttempts
};
// local configuration values (stored in RAM)
config_type localConfig;
// Start address where config is saved in EEPROM
const byte CONFIG_START = 128;

#ifdef ENABLE_DHCP
typedef struct
{
  char dummyChar;
  bool enableDhcp;
  byte dns[4];
} extra_config_type;

const extra_config_type EXTRA_CONFIG = {
  'x',
  false,               // enableDhcp
  { 192, 168, 1, 1 },  // dns
};
extra_config_type extraConfig;
#endif /* ENABLE_DHCP */

typedef struct {
  byte tid[2];           // MBAP Transaction ID
  byte msgLen;           // lenght of Modbus message stored in queueData
  IPAddress remIP;       // remote IP for UDP client (UDP response is sent back to remote IP)
  unsigned int remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  byte requestType;      // TCP client who sent the request
  byte atts;             // attempts counter
} header;

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, MAX_QUEUE_REQUESTS> queueHeaders;  // queue of requests' headers and metadata
CircularBuffer<byte, MAX_QUEUE_DATA> queueData;           // queue of PDU data


/****** ETHERNET AND SERIAL ******/

#ifdef UDP_TX_PACKET_MAX_SIZE
#undef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE MODBUS_SIZE
#endif

byte maxSockNum = MAX_SOCK_NUM;

#ifdef ENABLE_DHCP
bool dhcpSuccess = false;
#endif /* ENABLE_DHCP */

const byte MAC_START[3] = { 0x90, 0xA2, 0xDA };

EthernetUDP Udp;
EthernetServer modbusServer(DEFAULT_CONFIG.tcpPort);
EthernetServer webServer(DEFAULT_CONFIG.webPort);

/****** TIMERS AND STATE MACHINE ******/

class MicroTimer {
private:
  unsigned long timestampLastHitMs;
  unsigned long sleepTimeMs;
public:
  boolean isOver();
  void sleep(unsigned long sleepTimeMs);
};
boolean MicroTimer::isOver() {
  if ((unsigned long)(micros() - timestampLastHitMs) > sleepTimeMs) {
    return true;
  }
  return false;
}
void MicroTimer::sleep(unsigned long sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = micros();
}

MicroTimer recvTimer;
MicroTimer sendTimer;

#define RS485_TRANSMIT HIGH
#define RS485_RECEIVE LOW
byte scanCounter = 1;  // Start Modbus RTU scan after boot
enum state : byte {
  IDLE,
  SENDING,
  DELAY,
  WAITING
};

byte serialState;

enum status : byte {
  SLAVE_OK,              // Slave Responded
  SLAVE_ERROR_0X,        // Slave Responded with Error (Codes 1~8)
  SLAVE_ERROR_0A,        // Gateway Overloaded (Code 10)
  SLAVE_ERROR_0B,        // Slave Failed to Respond (Code 11)
  SLAVE_ERROR_0B_QUEUE,  // Slave Failed to Respond (Code 11) + in Queue
  SLAVE_ERROR_LAST       // Number of status flags in this enum. Must be the last element within this enum!!
};

// bool arrays for storing Modbus RTU status of individual slaves
uint8_t stat[SLAVE_ERROR_LAST][(MAX_SLAVES + 1 + 7) / 8];

// Scan request is in the queue
bool scanReqInQueue = false;
// byte tcpReqInQueue = 0;

byte scanFunction = 0x03;  //

// Counter for priority requests in the queue
byte priorityReqInQueue;

byte response[MAX_RESPONSE_LEN];
byte responseLen;  // stores actual length of the response shown in WebUI


/****** RUN TIME AND DATA COUNTERS ******/

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;  // seed2 is static

// array for storing error counts
uint32_t errorCount[SLAVE_ERROR_0B_QUEUE];  // there is no counter for SLAVE_ERROR_0B_QUEUE
uint32_t errorTcpCount;
uint32_t errorRtuCount;
uint32_t errorTimeoutCount;

uint16_t queueDataSize;
uint8_t queueHeadersSize;

#ifdef ENABLE_EXTRA_DIAG
// store uptime seconds (includes seconds counted before millis() overflow)
unsigned long seconds;
// store last millis() so that we can detect millis() overflow
unsigned long last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
long remaining_seconds;
// Data counters (we only use unsigned long in ENABLE_EXTRA_DIAG, to save flash memory)
unsigned long serialTxCount = 0;
unsigned long serialRxCount = 0;
unsigned long ethTxCount = 0;
unsigned long ethRxCount = 0;
#endif /* ENABLE_EXTRA_DIAG */

/****** SETUP: RUNS ONCE ******/

void setup() {
  CreateTrulyRandomSeed();

  // is config already stored in EEPROM?
  if (EEPROM.read(CONFIG_START) == VERSION[0]) {
    // load (CONFIG_START) the local configuration struct from EEPROM
    EEPROM.get(CONFIG_START + 1, localConfig);
#ifdef ENABLE_DHCP
    if (EEPROM.read(CONFIG_START + 1 + sizeof(localConfig)) == EXTRA_CONFIG.dummyChar) {
      EEPROM.get(CONFIG_START + 1 + sizeof(localConfig), extraConfig);
    } else {
      extraConfig = EXTRA_CONFIG;
      EEPROM.put(CONFIG_START + 1 + sizeof(localConfig), extraConfig);
    }
#endif /* ENABLE_DHCP */
  } else {
    // load (overwrite) the local configuration struct from defaults and save them to EEPROM
    localConfig = DEFAULT_CONFIG;
    // generate new MAC (bytes 0, 1 and 2 are static, bytes 3, 4 and 5 are generated randomly)
    generateMac();
    EEPROM.write(CONFIG_START, VERSION[0]);
    EEPROM.put(CONFIG_START + 1, localConfig);
#ifdef ENABLE_DHCP
    extraConfig = EXTRA_CONFIG;
    EEPROM.put(CONFIG_START + 1 + sizeof(localConfig), extraConfig);
#endif /* ENABLE_DHCP */
  }
  startSerial();
  startEthernet();
}

/****** LOOP ******/

void loop() {

  scanRequest();
  sendSerial();
  recvSerial();
  recvUdp();
  // recvTcp();
  // recvWeb();

  if (rollover()) resetStats();

#ifdef ENABLE_EXTRA_DIAG
  maintainUptime();  // maintain uptime in case of millis() overflow
#endif               /* ENABLE_EXTRA_DIAG */
#ifdef ENABLE_DHCP
  maintainDhcp();
#endif /* ENABLE_DHCP */

  manageSockets();
}
