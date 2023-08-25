/*  Advanced settings, extra functions and default config for Modbus RTU ⇒ Modbus TCP/UDP Gateway
*/

/****** ADVANCED SETTINGS ******/

#define mySerial Serial  // define serial port for RS485 interface, for Arduino Mega choose from Serial1, Serial2 or Serial3
// List of baud rates (divided by 100) available in WebUI. Feel free to add your custom baud rate (anything between 3 and 2500)
const uint16_t BAUD_RATES[] = { 3, 6, 9, 12, 24, 48, 96, 192, 384, 576, 1152 };
#define RS485_CONTROL_PIN 6              // Arduino Pin for RS485 Direction control, disable if you have module with hardware flow control
const byte MAX_QUEUE_REQUESTS = 10;      // max number of TCP or UDP requests stored in a queue
const uint16_t MAX_QUEUE_DATA = 254;     // total length of TCP or UDP requests stored in a queue (in bytes),
                                         // should be at least MODBUS_SIZE - 2 (CRC is not stored in queue)
const uint16_t MAX_SLAVES = 247;         // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for reserved addresses)
const uint16_t MODBUS_SIZE = 256;        // maximum size of a MODBUS RTU frame incl slave address and CRC (determines size of various buffers)
const byte MAX_RESPONSE_LEN = 16;        // Max length (bytes) of the Modbus response shown in WebUI
const byte SCAN_FUNCTION_FIRST = 0x03;   // Function code sent during Modbus RTU Scan request (first attempt)
const byte SCAN_FUNCTION_SECOND = 0x04;  // Function code sent during Modbus RTU Scan request (second attempt)
const byte SCAN_DATA_ADDRESS = 0x01;     // Data address sent during Modbus RTU Scan request (both attempts)
const uint16_t SCAN_TIMEOUT = 200;       // Timeout (ms) for Modbus scan requests

const byte MAC_START[3] = { 0x90, 0xA2, 0xDA };  // MAC range for Gheo SA
const byte ETH_RESET_PIN = 7;                    // Ethernet shield reset pin (deals with power on reset issue on low quality ethernet shields)
const uint16_t ETH_RESET_DELAY = 500;            // Delay (ms) during Ethernet start, wait for Ethernet shield to start (reset issue on low quality ethernet shields)
const uint16_t WEB_IDLE_TIMEOUT = 400;           // Time (ms) from last client data after which webserver TCP socket could be disconnected, non-blocking.
const uint16_t TCP_DISCON_TIMEOUT = 500;         // Timeout (ms) for client DISCON socket command, non-blocking alternative to https://www.arduino.cc/reference/en/libraries/ethernet/client.setconnectiontimeout/
const uint16_t TCP_RETRANSMISSION_TIMEOUT = 50;  // Ethernet controller’s timeout (ms), blocking (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissiontimeout/)
const byte TCP_RETRANSMISSION_COUNT = 3;         // Number of transmission attempts the Ethernet controller will make before giving up (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissioncount/)
const uint16_t FETCH_INTERVAL = 2000;            // Fetch API interval (ms) for the Modbus Status webpage to renew data from JSON served by Arduino

const byte DATA_START = 96;      // Start address where config and counters are saved in EEPROM
const byte EEPROM_INTERVAL = 6;  // Interval (hours) for saving Modbus statistics to EEPROM (in order to minimize writes to EEPROM)

/****** EXTRA FUNCTIONS ******/

// these do not fit into the limited flash memory of Arduino Uno/Nano, uncomment if you have a board with more memory
// #define ENABLE_DHCP        // Enable DHCP (Auto IP settings)
// #define ENABLE_EXTRA_DIAG  // Enable Ethernet and Serial byte counter.

/****** DEFAULT FACTORY SETTINGS ******/

/*
  Please note that after boot, Arduino loads user settings stored in EEPROM, even if you flash new program to it!
  Arduino loads factory defaults if:
  1) User clicks "Load default settings" in WebUI (factory reset configuration, keeps MAC)
  2) VERSION_MAJOR changes (factory reset configuration AND generates new MAC)

  You can change default factory settings bellow, but do not delete (comment out) individual lines! 
*/
const config_t DEFAULT_CONFIG = {
  { 192, 168, 1, 254 },  // Static IP
  { 255, 255, 255, 0 },  // Submask
  { 192, 168, 1, 1 },    // Gateway
  { 192, 168, 1, 1 },    // Dns (only used if ENABLE_DHCP)
  false,                 // enableDhcp (only used if ENABLE_DHCP)
  502,                   // Modbus TCP Port
  502,                   // Modbus UDP Port
  80,                    // WebUI Port
  false,                 // Modbus Mode (enableRTU over TCP)
  600,                   // Modbus TCP Idle Timeout
  96,                    // Baud Rate / 100
  SERIAL_8E1,            // Serial Config (Data Bits, Parity, Stop bits), Modbus RTU default is 8E1, another frequently used option is 8N2
  150,                   // Inter-frame Delay (byte)
  500,                   // Response Timeout
  3,                     // Attempts (byte)
};
