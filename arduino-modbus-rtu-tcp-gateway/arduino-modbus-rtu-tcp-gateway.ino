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
  v3.2 2022-06-04 Reduce program size (so that it fits on Nano), ethernet data counter only available when ENABLE_EXTENDED_WEBUI.
  v4.0 2023-01-05 Modbus statistics and error reporting on "Modbus Status" page, add Frame Delay setting for Modbus RTU
                  Optimize Modbus timeout and attempts, significant reduction of code size
  v4.1 2023-01-14 Fetch API, bugfix MAX485
  v5.0 2023-02-19 Send Modbus Request from WebUI, optimized POST parameter processing (less RAM consumption), select baud rate in WebUI,
                  improved TCP socket management, Modbus TCP Idle Timeout settings
  v6.0 2023-03-18 Save error counters to EEPROM, code optimization, separate file for advanced settings
  v6.1 2023-04-12 Code optimization
  v7.0 2023-07-21 Manual MAC, better data types
  v7.1 2023-08-25 Simplify EEPROM read and write, Tools page
  v7.2 2023-10-20 Disable DHCP renewal fallback, better advanced_settings.h layout
                  ENABLE_EXTENDED_WEBUI and ENABLE_DHCP is set by default for Mega
  v7.3 2024-01-16 Bugfix Modbus RTU Request form, code comments
  v7.4 2024-12-16 CSS improvement, code optimization, simplify DHCP renew, better README (solution to ethernet reset issue)
*/

const byte VERSION[] = { 7, 4 };

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <CircularBuffer.hpp>  // CircularBuffer https://github.com/rlogiacco/CircularBuffer
#include <EEPROM.h>
#include <StreamLib.h>  // StreamLib https://github.com/jandrassy/StreamLib

// these are used by CreateTrulyRandomSeed() function
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/atomic.h>

typedef struct {
  byte ip[4];
  byte subnet[4];
  byte gateway[4];
#ifdef ENABLE_DHCP
  byte dns[4];      // only used if ENABLE_DHCP
  bool enableDhcp;  // only used if ENABLE_DHCP
#endif
  uint16_t tcpPort;
  uint16_t udpPort;
  uint16_t webPort;
  bool enableRtuOverTcp;
  uint16_t tcpTimeout;
  uint16_t baud;
  byte serialConfig;
  byte frameDelay;
  uint16_t serialTimeout;
  byte serialAttempts;
} config_t;

#include "advanced_settings.h"

const config_t DEFAULT_CONFIG = {
  DEFAULT_STATIC_IP,
  DEFAULT_SUBMASK,
  DEFAULT_GATEWAY,
#ifdef ENABLE_DHCP
  DEFAULT_DNS,
  DEFAULT_AUTO_IP,
#endif
  DEFAULT_TCP_PORT,
  DEFAULT_UDP_PORT,
  DEFAULT_WEB_PORT,
  DEFAULT_RTU_OVER_TCP,
  DEFAULT_TCP_TIMEOUT,
  DEFAULT_BAUD_RATE,
  DEFAULT_SERIAL_CONFIG,
  DEFAULT_FRAME_DELAY,
  DEFAULT_RESPONSE_TIMEPOUT,
  DEFAULT_ATTEMPTS,
};

enum status_t : byte {
  SLAVE_OK,              // Slave Responded
  SLAVE_ERROR_0X,        // Slave Responded with Error (Codes 1~8)
  SLAVE_ERROR_0A,        // Gateway Overloaded (Code 10)
  SLAVE_ERROR_0B,        // Slave Failed to Respond (Code 11)
  SLAVE_ERROR_0B_QUEUE,  // Slave Failed to Respond (Code 11) & is in Queue (not shown in web UI)
  ERROR_TIMEOUT,         // Response Timeout
  ERROR_RTU,             // Invalid RTU Response
  ERROR_TCP,             // Invalid TCP/UDP Request
  ERROR_LAST             // Number of status flags in this enum. Must be the last element within this enum!!
};

enum flow_t : byte {
  DATA_TX,
  DATA_RX,
  DATA_LAST  // Number of status flags in this enum. Must be the last element within this enum!!
};

typedef struct {
  uint32_t eepromWrites;          // Number of EEPROM write cycles (persistent, it is never cleared during factory resets)
  byte major;                     // major version
  byte mac[6];                    // MAC Address (initial value is random generated)
  config_t config;                // configuration values
  uint32_t errorCnt[ERROR_LAST];  // array for storing error counters
#ifdef ENABLE_EXTENDED_WEBUI
  uint32_t rtuCnt[DATA_LAST];  // array for storing RTU data counters
  uint32_t ethCnt[DATA_LAST];  // array for storing ethernet data counters
#endif                         /* ENABLE_EXTENDED_WEBUI */
} data_t;

data_t data;

typedef struct {
  byte tid[2];       // MBAP Transaction ID
  byte msgLen;       // lenght of Modbus message stored in queueData
  IPAddress remIP;   // remote IP for UDP client (UDP response is sent back to remote IP)
  uint16_t remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  byte requestType;  // TCP client who sent the request
  byte atts;         // attempts counter
} header_t;

// bool arrays for storing Modbus RTU status of individual slaves
byte slaveStatus[SLAVE_ERROR_0B_QUEUE + 1][(MAX_SLAVES + 1 + 7) / 8];  // SLAVE_ERROR_0B_QUEUE is the last status of slaves

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header_t, MAX_QUEUE_REQUESTS> queueHeaders;  // queue of requests' headers and metadata
CircularBuffer<byte, MAX_QUEUE_DATA> queueData;             // queue of PDU data


/****** ETHERNET AND SERIAL ******/

byte maxSockNum = MAX_SOCK_NUM;

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
Timer eepromTimer;    // timer to delay writing statistics to EEPROM
Timer checkEthTimer;  // timer to check SPI connection with ethernet shield

#define RS485_TRANSMIT HIGH
#define RS485_RECEIVE LOW

byte scanCounter = 1;  // Start Modbus RTU scan after boot
enum state_t : byte {
  IDLE,
  SENDING,
  DELAY,
  WAITING
};

byte serialState;


/****** RUN TIME AND DATA COUNTERS ******/

bool scanReqInQueue = false;  // Scan request is in the queue
byte priorityReqInQueue;      // Counter for priority requests in the queue

byte response[MAX_RESPONSE_LEN];  // buffer to store the last Modbus response
byte responseLen;                 // stores actual length of the response shown in WebUI

uint16_t queueDataSize;
byte queueHeadersSize;

#ifdef ENABLE_EXTENDED_WEBUI
// store uptime seconds (includes seconds counted before millis() overflow)
uint32_t seconds;
// store last millis() so that we can detect millis() overflow
uint32_t last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
int32_t remaining_seconds;
// Data counters (we only use uint32_t in ENABLE_EXTENDED_WEBUI, to save flash memory)
#endif /* ENABLE_EXTENDED_WEBUI */

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;  // seed2 is static


/****** SETUP: RUNS ONCE ******/

void setup() {
  CreateTrulyRandomSeed();
  EEPROM.get(DATA_START, data);
  // is configuration already stored in EEPROM?
  if (data.major != VERSION[0]) {
    data.major = VERSION[0];
    // load default configuration from flash memory
    data.config = DEFAULT_CONFIG;
    generateMac();
    resetStats();
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

  // could help with freeze after power loss reported by some users, not enabled yet
  // if (CHECK_ETH_INTERVAL > 0 && checkEthTimer.isOver() == true) {
  //   checkEthernet();
  // }

  if (rollover()) {
    resetStats();
    updateEeprom();
  }
#ifdef ENABLE_EXTENDED_WEBUI
  maintainUptime();  // maintain uptime in case of millis() overflow
#endif               /* ENABLE_EXTENDED_WEBUI */
#ifdef ENABLE_DHCP
  Ethernet.maintain();
#endif /* ENABLE_DHCP */
}
