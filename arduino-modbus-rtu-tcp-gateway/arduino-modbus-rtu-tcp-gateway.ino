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

*/

const byte version[] = {2, 4};

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <CircularBuffer.h>     // CircularBuffer https://github.com/rlogiacco/CircularBuffer
#include <EEPROM.h>
#include <StreamLib.h>          // StreamLib https://github.com/jandrassy/StreamLib

// these are used by CreateTrulyRandomSeed() function
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/atomic.h>

/****** ADVANCED SETTINGS ******/

const byte reqQueueCount = 15;       // max number of TCP or UDP requests stored in queue
const int reqQueueSize = 256;        // total length of TCP or UDP requests stored in queue (in bytes)
const byte maxSlaves = 247;          // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for reserved addresses)
const int modbusSize = 256;          // size of a MODBUS RTU frame (determines size of serialInBuffer and tcpInBuffer)
#define mySerial Serial              // define serial port for RS485 interface, for Arduino Mega choose from Serial1, Serial2 or Serial3         
#define RS485_CONTROL_PIN 6          // Arduino Pin for RS485 Direction control, disable if you have module with hardware flow control
const byte ethResetPin = 7;          // Ethernet shield reset pin (deals with power on reset issue of the ethernet shield)
const byte scanCommand[] = {0x03, 0x00, 0x00, 0x00, 0x01};  // Command sent during Modbus RTU Scan. Slave is detected if any response (even error) is received.

// #define DEBUG            // Main Serial (USB) is used for printing some debug info, not for Modbus RTU. At the moment, only web server related debug messages are printed.

/****** EXTRA FUNCTIONS ******/

// these do not fit into the limited flash memory of Arduino Uno/Nano, uncomment if you have board with more memory
// #define ENABLE_DHCP            // Enable DHCP (Auto IP settings)
// #define ENABLE_EXTRA_DIAG      // Enable per socket diagnostics, run time counter

/****** DEFAULT FACTORY SETTINGS ******/

typedef struct
{
  byte mac[6];
  bool enableDhcp;
  IPAddress ip;
  IPAddress subnet;
  IPAddress gateway;
  IPAddress dns;
  unsigned int tcpPort;
  unsigned int udpPort;
  unsigned int webPort;
  bool enableRtuOverTcp;
  unsigned long baud;
  byte serialConfig;
  unsigned int serialTimeout;
  byte serialRetry;
} config_type;

/*
  Please note that after boot, Arduino loads settings stored in EEPROM, even if you flash new program to it!
  
  Arduino loads factory defaults specified bellow in case:
  1) User clicks "Restore" defaults in WebUI (factory reset configuration, keeps MAC)
  2) VERSION_MAJOR changes (factory reset configuration AND generates new MAC)
*/

const config_type defaultConfig = {
  { 0x90, 0xA2, 0xDA },  // mac (bytes 4, 5 and 6 will be generated randomly)
  false,                 // enableDhcp
  {192, 168, 1, 254},    // ip
  {255, 255, 255, 0},    // subnet
  {192, 168, 1, 1},      // gateway
  {192, 168, 1, 1},      // dns
  502,                   // tcpPort
  502,                   // udpPort
  80,                    // webPort
  false,                 // enableRtuOverTcp
  9600,                  // baud
  SERIAL_8E1,            // serialConfig (Modbus RTU default is 8E1, another frequently used option is 8N2)
  500,                   // serialTimeout
  5                      // serialRetry
};
// local configuration values (stored in RAM)
config_type localConfig;
// Start address where config is saved in EEPROM
const byte configStart = 128;

/****** ETHERNET AND SERIAL ******/

#define UDP_TX_PACKET_MAX_SIZE modbusSize
byte maxSockNum = MAX_SOCK_NUM;

bool dhcpSuccess = false;

EthernetUDP Udp;
EthernetServer modbusServer(502);
EthernetServer webServer(80);
#ifdef DEBUG
#define dbg(x...) Serial.print(x);
#define dbgln(x...) Serial.println(x);
#else /* DEBUG */
#define dbg(x...) ;
#define dbgln(x...) ;
#endif /* DEBUG */
#define UDP_REQUEST 0xFF      // We store these codes in "header.clientNum" in order to differentiate 
#define SCAN_REQUEST 0xFE      // between TCP requests (their clientNum is nevew higher than 0x07), UDP requests and scan requests (triggered by scan button)

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
Timer requestTimeout;
uint16_t crc;
#define RS485_TRANSMIT    HIGH
#define RS485_RECEIVE     LOW
byte scanCounter = 0;
enum state : byte
{
  IDLE, SENDING, DELAY, WAITING
};
enum state serialState;
unsigned int charTimeout;
unsigned int frameDelay;

/****** RUN TIME AND DATA COUNTERS ******/

volatile uint32_t seed1;  // seed1 is generated by CreateTrulyRandomSeed()
volatile int8_t nrot;
uint32_t seed2 = 17111989;   // seed2 is static

// store uptime seconds (includes seconds counted before millis() overflow)
unsigned long seconds;
// store last millis() so that we can detect millis() overflow
unsigned long last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
unsigned long remaining_seconds = 0;
// Data counters (we only use unsigned long in ENABLE_EXTRA_DIAG, to save flash memory)
#ifdef ENABLE_EXTRA_DIAG
unsigned long serialTxCount = 0;
unsigned long serialRxCount = 0;
unsigned long ethTxCount = 0;
unsigned long ethRxCount = 0;
#else
unsigned int serialTxCount = 0;
unsigned int serialRxCount = 0;
unsigned int ethTxCount = 0;
unsigned int ethRxCount = 0;
#endif /* ENABLE_EXTRA_DIAG */

/****** SETUP: RUNS ONCE ******/

void setup()
{
  CreateTrulyRandomSeed();

  // is config already stored in EEPROM?
  if (EEPROM.read(configStart) == version[0]) {
    // load (configStart) the local configuration struct from EEPROM
    EEPROM.get(configStart + 1, localConfig);
  } else {
    // load (overwrite) the local configuration struct from defaults and save them to EEPROM
    localConfig = defaultConfig;
    // generate new MAC (bytes 0, 1 and 2 are static, bytes 3, 4 and 5 are generated randomly)
    generateMac();
    EEPROM.write(configStart, version[0]);
    EEPROM.put(configStart + 1, localConfig);
  }

  startSerial();
  startEthernet();
  dbgln(F("\n[arduino] Starting..."));
}

/****** LOOP ******/

void loop()
{
  recvUdp();
  recvTcp();
  processRequests();

#ifndef DEBUG
  sendSerial();
  recvSerial();
#endif /* DEBUG */

  recvWeb();
  maintainCounters();   // maintain counters and synchronize their reset to zero when they overflow

#ifdef ENABLE_DHCP
  maintainDhcp();
#endif /* ENABLE_DHCP */

#ifdef ENABLE_EXTRA_DIAG
  maintainUptime();    // maintain uptime in case of millis() overflow
#endif /* ENABLE_EXTRA_DIAG */
}
