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

*/

const byte VERSION[] = { 4, 0 };

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

const byte MAX_QUEUE_REQUESTS = 10;                            // max number of TCP or UDP requests stored in a queue
const int MAX_QUEUE_DATA = 256;                                // total length of TCP or UDP requests stored in a queue (in bytes)
const byte MAX_SLAVES = 247;                                   // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for reserved addresses)
const int MODBUS_SIZE = 256;                                   // size of a MODBUS RTU frame (determines size of various buffers)
#define mySerial Serial                                        // define serial port for RS485 interface, for Arduino Mega choose from Serial1, Serial2 or Serial3
#define RS485_CONTROL_PIN 6                                    // Arduino Pin for RS485 Direction control, disable if you have module with hardware flow control
const byte ETH_RESET_PIN = 7;                                  // Ethernet shield reset pin (deals with power on reset issue of the ethernet shield)
const int SCAN_TIMEOUT = 200;                                  // Timeout (ms) for scan requests
const byte SCAN_COMMAND[] = { 0x03, 0x00, 0x00, 0x00, 0x01 };  // Command sent during Modbus RTU Scan. Slave is detected as "Responding" if any response (even error) is received.

/****** EXTRA FUNCTIONS ******/

// these do not fit into the limited flash memory of Arduino Uno/Nano, uncomment if you have a board with more memory
// #define ENABLE_DHCP            // Enable DHCP (Auto IP settings)
// #define ENABLE_EXTRA_DIAG      // Enable per socket diagnostics, run time counter, etc.

/****** DEFAULT FACTORY SETTINGS ******/

typedef struct
{
  byte macEnd[3];
  byte ip[4];
  byte subnet[4];
  byte gateway[4];
  unsigned int tcpPort;
  unsigned int udpPort;
  unsigned int webPort;
  bool enableRtuOverTcp;
  unsigned long baud;
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
  {},  // macEnd (last 3 bytes)
  { 192, 168, 1, 254 },  // ip
  { 255, 255, 255, 0 },  // subnet
  { 192, 168, 1, 1 },    // gateway
  502,                   // tcpPort
  502,                   // udpPort
  80,                    // webPort
  false,                 // enableRtuOverTcp
  9600,                  // baud
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

/****** ETHERNET AND SERIAL ******/

#ifdef UDP_TX_PACKET_MAX_SIZE
#undef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE MODBUS_SIZE
#endif

#ifdef MAX_SOCK_NUM     // Ethernet.h library determines MAX_SOCK_NUM by Microcontroller RAM (not by Ethernet chip type).
#undef MAX_SOCK_NUM     // Ignore the RAM-based limitation on the number of sockets.
#define MAX_SOCK_NUM 8  // Use all sockets supported by the ethernet chip.
#endif

byte maxSockNum = MAX_SOCK_NUM;

#ifdef ENABLE_DHCP
bool dhcpSuccess = false;
#endif /* ENABLE_DHCP */

const byte MAC_START[3] = { 0x90, 0xA2, 0xDA };

EthernetUDP Udp;
EthernetServer modbusServer(502);
EthernetServer webServer(80);

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

uint16_t crc;
#define RS485_TRANSMIT HIGH
#define RS485_RECEIVE LOW
byte scanCounter = 1;  // scan RS485 line after boot
enum state : byte {
  IDLE,
  SENDING,
  DELAY,
  WAITING
};
byte serialState;

enum status : byte {
  STAT_OK,              // Slave Responded
  STAT_ERROR_0X,        // Slave Responded with Error (Codes 1~8)
  STAT_ERROR_0A,        // Gateway Overloaded (Code 10)
  STAT_ERROR_0B,        // Slave Failed to Respond (Code 11)
  STAT_ERROR_0B_QUEUE,  // Slave Failed to Respond (Code 11) + in Queue
  STAT_NUM              // Number of status flags in this enum. Must be the last element within this enum!!
};

/****** RUN TIME AND DATA COUNTERS ******/

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;  // seed2 is static

// array for storing error counts
uint32_t errorCount[STAT_ERROR_0B_QUEUE];  // there is no counter for STAT_ERROR_0B_QUEUE
uint32_t errorTcpCount;
uint32_t errorRtuCount;
uint32_t errorTimeoutCount;

uint16_t queueDataSize;
uint8_t queueHeadersSize;

// store uptime seconds (includes seconds counted before millis() overflow)
unsigned long seconds;
// store last millis() so that we can detect millis() overflow
unsigned long last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
long remaining_seconds;
#ifdef ENABLE_EXTRA_DIAG
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

  recvUdp();
  recvTcp();
  scanRequest();
  sendSerial();
  recvSerial();
  maintainUptime();  // maintain uptime in case of millis() overflow
  recvWeb();

  if (rollover()) resetStats();
  
#ifdef ENABLE_DHCP
  maintainDhcp();
#endif /* ENABLE_DHCP */
}
