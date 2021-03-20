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


*/


#include <SPI.h>
#include <Ethernet3.h>          // https://github.com/sstaub/Ethernet3
#include <EthernetUdp3.h>       // https://github.com/sstaub/Ethernet3
#include <CircularBuffer.h>     // CircularBuffer https://github.com/rlogiacco/CircularBuffer
#include <BitBool.h>            // BitBool https://github.com/Chris--A/BitBool
#include <TrueRandom.h>         // https://github.com/sirleech/TrueRandom
#include <EEPROM.h>
#include <StreamLib.h>          // StreamLib https://github.com/jandrassy/StreamLib

/****** ADVANCED SETTINGS ******/

// #define DEBUG            // Serial is used for printing some debug info, not for Modbus RTU. At the moment, only web server related debug messages are printed.
// #define ENABLE_DHCP      // Eable DHCP, requires additional flash memory

const byte reqQueueCount = 15;       // max number of TCP or UDP requests stored in queue
const int reqQueueSize = 256;        // total length of TCP or UDP requests stored in queue (in bytes)
const byte maxSlaves = 247;          // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for including broadcast and reserved addresses
const int modbusSize = 256;          // size of a MODBUS RTU frame (determines size of serialInBuffer and tcpInBuffer)
const byte SerialTxControl = 6;      // Arduino Pin for RS485 Direction control
const byte ethResetPin = 7;          // Ethernet shield reset pin (deals with power on reset issue of the ethernet shield)
const byte scanCommand[] = {0x03, 0x00, 0x00, 0x00, 0x01};  // Command sent during Modbus RTU Scan. Slave is detected if any response (even error) is received.


/****** DEFAULT FACTORY SETTINGS ******/

typedef struct
{
  char marker;
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

// Please note that if you change settings via WebUI, they will be stored in EEPROM.
// After boot, Arduino will load stored settings from EEPROM, even after you flash new program to it!
// If you want to restore factory defaults, you must either click "Restore" defaults in WebUI or change "marker" in the sketch to another character.
const config_type defaultConfig = {
  '#',                   // marker (indicates that settings are already stored in EEPROM and can be loaded after boot)
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
#define RS485Transmit    HIGH
#define RS485Receive     LOW
byte scanCounter = 0;
enum state : byte
{
  IDLE, SENDING, DELAY, WAITING
};
enum state serialState;
unsigned int charTimeout;
unsigned int frameDelay;

/****** RUN TIME AND DATA COUNTERS ******/

// store uptime seconds (includes seconds counted before millis() overflow)
unsigned long seconds;
// store last millis() so that we can detect millis() overflow
unsigned long last_milliseconds = 0;
// store seconds passed until the moment of the overflow so that we can add them to "seconds" on the next call
unsigned long remaining_seconds = 0;
// Data counters
unsigned long serialTxCount = 0;
unsigned long serialRxCount = 0;
unsigned long ethTxCount = 0;
unsigned long ethRxCount = 0;

/****** SETUP: RUNS ONCE ******/

void setup()
{
  // is config already stored in EEPROM?
  if (EEPROM.read(configStart) == defaultConfig.marker) {
    // load (configStart) the local configuration struct from EEPROM
    EEPROM.get(configStart, localConfig);
  } else {
    // load (overwrite) the local configuration struct from defaults and save them to EEPROM
    localConfig = defaultConfig;
    // bytes 4, 5 and 6 are generated randomly
    for (byte i = 3; i < 6; i++) {
      localConfig.mac[i] = TrueRandom.randomByte();
    }
    EEPROM.put(configStart, localConfig);
  }

  startSerial();
  startEthernet();
  dbgln(F("\n[arduino] Starting..."));
}

/****** LOOP ******/

void loop()
{
#ifdef ENABLE_DHCP
  if (localConfig.enableDhcp) {
    Ethernet.maintain();
  }
#endif /* ENABLE_DHCP */
  recvUdp();
  recvTcp();
  processRequests();
#ifndef DEBUG
  sendSerial();
  recvSerial();
#endif /* DEBUG */

  recvWeb();

  maintainCounters();   // maintain counters and synchronize their reset to zero when they overflow
  maintainUptime();    // maintain uptime in case of millis() overflow
}
