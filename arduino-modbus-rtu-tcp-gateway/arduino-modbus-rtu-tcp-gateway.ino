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
  v6.0 2023-03-18 Save error counters to EEPROM, code optimization, separate file for advanced settings
  v6.1 2023-04-12 Code optimization
  v7.0 2023-XX-XX Manual MAC, better data types
*/

const uint8_t VERSION[] = { 7, 0 };

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

typedef struct {
  uint8_t ip[4];
  uint8_t subnet[4];
  uint8_t gateway[4];
  uint8_t dns[4];      // only used if ENABLE_DHCP
  bool enableDhcp;  // only used if ENABLE_DHCP
  uint16_t tcpPort;
  uint16_t udpPort;
  uint16_t webPort;
  bool enableRtuOverTcp;
  uint16_t tcpTimeout;
  uint16_t baud;
  uint8_t serialConfig;
  uint8_t frameDelay;
  uint16_t serialTimeout;
  uint8_t serialAttempts;
} config_type;

// local configuration values (stored in RAM)
config_type localConfig;

typedef struct {
  uint8_t tid[2];           // MBAP Transaction ID
  uint8_t msgLen;           // lenght of Modbus message stored in queueData
  IPAddress remIP;       // remote IP for UDP client (UDP response is sent back to remote IP)
  uint16_t remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  uint8_t requestType;      // TCP client who sent the request
  uint8_t atts;             // attempts counter
} header;

#include "advanced_settings.h"

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, MAX_QUEUE_REQUESTS> queueHeaders;  // queue of requests' headers and metadata
CircularBuffer<uint8_t, MAX_QUEUE_DATA> queueData;           // queue of PDU data


/****** ETHERNET AND SERIAL ******/

uint8_t mac[6];  // MAC Address (initial value is random generated)

#ifdef UDP_TX_PACKET_MAX_SIZE
#undef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE MODBUS_SIZE
#endif

uint8_t maxSockNum = MAX_SOCK_NUM;

#ifdef ENABLE_DHCP
bool dhcpSuccess = false;
#endif /* ENABLE_DHCP */

EthernetUDP Udp;
EthernetServer modbusServer(DEFAULT_CONFIG.tcpPort);
EthernetServer webServer(DEFAULT_CONFIG.webPort);

/****** TIMERS AND STATE MACHINE ******/

class MicroTimer {
private:
  uint32_t timestampLastHitMs;
  uint32_t sleepTimeMs;
public:
  boolean isOver();
  void sleep(uint32_t sleepTimeMs);
};
boolean MicroTimer::isOver() {
  if ((uint32_t)(micros() - timestampLastHitMs) > sleepTimeMs) {
    return true;
  }
  return false;
}
void MicroTimer::sleep(uint32_t sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = micros();
}

class Timer {
private:
  uint32_t timestampLastHitMs;
  uint32_t sleepTimeMs;
public:
  boolean isOver();
  void sleep(uint32_t sleepTimeMs);
};
boolean Timer::isOver() {
  if ((uint32_t)(millis() - timestampLastHitMs) > sleepTimeMs) {
    return true;
  }
  return false;
}
void Timer::sleep(uint32_t sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = millis();
}

MicroTimer recvMicroTimer;
MicroTimer sendMicroTimer;
Timer eepromTimer;  // timer to delay writing statistics to EEPROM

#define RS485_TRANSMIT HIGH
#define RS485_RECEIVE LOW

uint8_t scanCounter = 1;  // Start Modbus RTU scan after boot
enum state : uint8_t {
  IDLE,
  SENDING,
  DELAY,
  WAITING
};

uint8_t serialState;


/****** RUN TIME AND DATA COUNTERS ******/

enum status : uint8_t {
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
uint8_t priorityReqInQueue;      // Counter for priority requests in the queue

uint8_t response[MAX_RESPONSE_LEN];  // buffer to store the last Modbus response
uint8_t responseLen;                 // stores actual length of the response shown in WebUI

uint16_t queueDataSize;
uint8_t queueHeadersSize;

#ifdef ENABLE_EXTRA_DIAG
// store uptime seconds (includes seconds counted before millis() overflow)
uint32_t seconds;
// store last millis() so that we can detect millis() overflow
uint32_t last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
int32_t remaining_seconds;
// Data counters (we only use uint32_t in ENABLE_EXTRA_DIAG, to save flash memory)
enum data_count : uint8_t {
  DATA_TX,
  DATA_RX,
  DATA_LAST  // Number of status flags in this enum. Must be the last element within this enum!!
};
uint32_t rtuCount[DATA_LAST];
uint32_t ethCount[DATA_LAST];
#endif /* ENABLE_EXTRA_DIAG */

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;  // seed2 is static


/****** SETUP: RUNS ONCE ******/

void setup() {
  CreateTrulyRandomSeed();

  uint8_t address = CONFIG_START;
  EEPROM.get(address, eepromWrites);  // EEPROM write counter is persistent, it is never cleared during factory resets
  address += sizeof(eepromWrites);
  // is configuration already stored in EEPROM?
  if (EEPROM.read(address) == VERSION[0]) {
    // load configuration, error counters and other data countersfrom EEPROM
    address += 1;
    EEPROM.get(address, mac);
    address += 6;
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
