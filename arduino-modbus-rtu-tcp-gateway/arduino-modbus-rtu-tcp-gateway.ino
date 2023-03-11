/* Arduino-based Modbus RTU (slaves) to Modbus TCP/UDP (master) gateway with web interface

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
  v5.0 2023-02-19 Send Modbus Request from WebUI, optimized POST parameter processing (less RAM consumption), select baud rate in WebUI,
                  improved TCP socket management, Modbus TCP Idle Timeout settings
  v6.0 2023-XX-XX Save error counters to EEPROM, code optimization

*/

const byte VERSION[] = { 6, 0 };

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
const byte ETH_RESET_PIN = 7;                        // Ethernet shield reset pin (deals with power on reset issue on low quality ethernet shields)
const unsigned int ETH_RESET_DELAY = 500;            // Delay (ms) during Ethernet start, wait for Ethernet shield to start (reset issue on low quality ethernet shields)
const unsigned int WEB_IDLE_TIMEOUT = 400;           // Time (ms) from last client data after which webserver TCP socket could be disconnected, non-blocking.
const unsigned int TCP_DISCON_TIMEOUT = 500;         // Timeout (ms) for client DISCON socket command, non-blocking alternative to https://www.arduino.cc/reference/en/libraries/ethernet/client.setconnectiontimeout/
const unsigned int TCP_RETRANSMISSION_TIMEOUT = 50;  // Ethernet controller’s timeout (ms), blocking (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissiontimeout/)
const byte TCP_RETRANSMISSION_COUNT = 3;             // Number of transmission attempts the Ethernet controller will make before giving up (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissioncount/)
const unsigned int SCAN_TIMEOUT = 200;               // Timeout (ms) for Modbus scan requests
const byte SCAN_FUNCTION_FIRST = 0x03;               // Function code sent during Modbus RTU Scan request (first attempt)
const byte SCAN_FUNCTION_SECOND = 0x04;              // Function code sent during Modbus RTU Scan request (second attempt)
const byte SCAN_DATA_ADDRESS = 0x01;                 // Data address sent during Modbus RTU Scan request (both attempts)
const int FETCH_INTERVAL = 2000;                     // Fetch API interval (ms) for the Modbus Status webpage to renew data from JSON served by Arduino
const byte EEPROM_INTERVAL = 6;                      // Interval (hours) for saving Modbus statistics to EEPROM (in order to minimize writes to EEPROM)
const byte MAX_RESPONSE_LEN = 16;                    // Max length (bytes) of the Modbus response shown in WebUI
// List of baud rates (divided by 100) available in WebUI. Feel free to add your custom baud rate (anything between 3 and 2500)
const unsigned int BAUD_RATES[] = { 3, 6, 9, 12, 24, 48, 96, 192, 384, 576, 1152 };

/****** EXTRA FUNCTIONS ******/

// these do not fit into the limited flash memory of Arduino Uno/Nano, uncomment if you have a board with more memory
// #define ENABLE_DHCP        // Enable DHCP (Auto IP settings)
// #define ENABLE_EXTRA_DIAG  // Enable Ethernet and Serial byte counter.
// #define TEST_SOCKS        // shows 1) port, 2) status and 3) age for all sockets in "Modbus Status" page. IP settings are not available.

/****** DEFAULT FACTORY SETTINGS ******/

typedef struct {
  byte macEnd[3];
  byte ip[4];
  byte subnet[4];
  byte gateway[4];
  byte dns[4];      // only used if ENABLE_DHCP
  bool enableDhcp;  // only used if ENABLE_DHCP
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
  { 192, 168, 1, 1 },    // dns
  false,                 // enableDhcp
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
const int CONFIG_START = 96;

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

class Timer {
private:
  unsigned long timestampLastHitMs;
  unsigned long sleepTimeMs;
public:
  boolean isOver();
  void sleep(unsigned long sleepTimeMs);
};
boolean Timer::isOver() {
  if ((unsigned long)(millis() - timestampLastHitMs) > sleepTimeMs) {
    return true;
  }
  return false;
}
void Timer::sleep(unsigned long sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = millis();
}

MicroTimer recvMicroTimer;
MicroTimer sendMicroTimer;
Timer eepromTimer;  // timer to delay writing statistics to EEPROM

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


/****** RUN TIME AND DATA COUNTERS ******/

enum status : byte {
  SLAVE_OK,              // Slave Responded
  SLAVE_ERROR_0X,        // Slave Responded with Error (Codes 1~8)
  SLAVE_ERROR_0A,        // Gateway Overloaded (Code 10)
  SLAVE_ERROR_0B,        // Slave Failed to Respond (Code 11)
  SLAVE_ERROR_0B_QUEUE,  // Slave Failed to Respond (Code 11) + in Queue
  ERROR_TIMEOUT,         // Response Timeout
  ERROR_RTU,             // Invalid RTU Response
  ERROR_TCP,             // Invalid TCP/UDP Request
  ERROR_LAST             // Number of status flags in this enum. Must be the last element within this enum!!
};

// bool arrays for storing Modbus RTU status of individual slaves
uint8_t slaveStatus[SLAVE_ERROR_0B_QUEUE + 1][(MAX_SLAVES + 1 + 7) / 8];  // SLAVE_ERROR_0B_QUEUE is the last status of slaves

// array for storing error counts
uint32_t errorCount[ERROR_LAST];  // there is no counter for SLAVE_ERROR_0B_QUEUE

uint32_t eepromWrites;  // Number of EEPROM write cycles

bool scanReqInQueue = false;  // Scan request is in the queue
byte priorityReqInQueue;      // Counter for priority requests in the queue

byte response[MAX_RESPONSE_LEN];  // buffer to store the last Modbus response
byte responseLen;                 // stores actual length of the response shown in WebUI

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
enum data_count : byte {
  DATA_TX,
  DATA_RX,
  DATA_LAST  // Number of status flags in this enum. Must be the last element within this enum!!
};
unsigned long rtuCount[DATA_LAST];
unsigned long ethCount[DATA_LAST];
#endif /* ENABLE_EXTRA_DIAG */

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;  // seed2 is static


/****** SETUP: RUNS ONCE ******/

void setup() {
  CreateTrulyRandomSeed();

  int address = CONFIG_START;
  EEPROM.get(address, eepromWrites);  // EEPROM write counter is persistent, it is never cleared during factory resets
  address += sizeof(eepromWrites);
  // is configuration already stored in EEPROM?
  if (EEPROM.read(address) == VERSION[0]) {
    // load configuration, error counters and other data countersfrom EEPROM
    address += 1;
    EEPROM.get(address, localConfig);
    address += sizeof(localConfig);
    EEPROM.get(address, errorCount);
    address += sizeof(errorCount);
#ifdef ENABLE_EXTRA_DIAG
    EEPROM.get(address, rtuCount);
    address += sizeof(rtuCount);
    EEPROM.get(address, ethCount);
    address += sizeof(ethCount);
#endif /* ENABLE_EXTRA_DIAG */
  } else {
    // load default configuration from flash memory
    localConfig = DEFAULT_CONFIG;
    // generate new MAC (bytes 0, 1 and 2 are static, bytes 3, 4 and 5 are generated randomly)
    generateMac();
    // save configuration (incl. last 3 bytes of MAC) to EEPROM
    updateEeprom();
  }
  startSerial();
  startEthernet();
}

/****** LOOP ******/

void loop() {

  scanRequest();
  sendSerial();
  recvUdp();
  recvSerial();

  manageSockets();

  if (EEPROM_INTERVAL > 0 && eepromTimer.isOver() == true) {
    updateEeprom();
  }

  if (rollover()) {
    resetStats();
  }
#ifdef ENABLE_EXTRA_DIAG
  maintainUptime();  // maintain uptime in case of millis() overflow
#endif               /* ENABLE_EXTRA_DIAG */
#ifdef ENABLE_DHCP
  maintainDhcp();
#endif /* ENABLE_DHCP */
}
