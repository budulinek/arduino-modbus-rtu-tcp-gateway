/*  Advanced settings, extra functions and default config for Modbus RTU ⇒ Modbus TCP/UDP Gateway
*/

/****** FUNCTIONALITY ******/

// #define ENABLE_EXTENDED_WEBUI  // Enable extended Web UI (additional items and settings), consumes FLASH memory
// uncomment ENABLE_EXTENDED_WEBUI if you have a board with large FLASH memory (Arduino Mega)

// #define ENABLE_DHCP  // Enable DHCP (Auto IP settings), consumes a lot of FLASH memory

#if defined(ARDUINO_AVR_MEGA) || defined(ARDUINO_AVR_MEGA2560)
#define ENABLE_EXTENDED_WEBUI
#define ENABLE_DHCP
#endif

/****** DEFAULT CONFIGURATION ******/
/*
  Arduino loads user settings stored in EEPROM, even if you flash new program to it.
  
  Arduino loads factory defaults if:
  1) User clicks "Load default settings" in WebUI (factory reset configuration, keeps MAC)
  2) VERSION_MAJOR changes (factory reset configuration AND generates new MAC)
*/

/****** IP Settings ******/
const bool DEFAULT_AUTO_IP = false;  // Default Auto IP setting (only used if ENABLE_DHCP)
#define DEFAULT_STATIC_IP \
  { 192, 168, 1, 254 }  // Default Static IP
#define DEFAULT_SUBMASK \
  { 255, 255, 255, 0 }  // Default Submask
#define DEFAULT_GATEWAY \
  { 192, 168, 1, 1 }  // Default Gateway
#define DEFAULT_DNS \
  { 192, 168, 1, 1 }  // Default DNS Server (only used if ENABLE_DHCP)

/****** TCP/UDP Settings ******/
const uint16_t DEFAULT_TCP_PORT = 502;     // Default Modbus TCP Port
const uint16_t DEFAULT_UDP_PORT = 502;     // Default Modbus UDP Port
const uint16_t DEFAULT_WEB_PORT = 80;      // Default WebUI Port
const bool DEFAULT_RTU_OVER_TCP = false;   // Default Modbus Mode (Modbus TCP or Modbus RTU over TCP)
const uint16_t DEFAULT_TCP_TIMEOUT = 600;  // Default Modbus TCP Idle Timeout

/****** RTU Settings ******/
const uint16_t DEFAULT_BAUD_RATE = 96;           // Default Baud Rate / 100
const byte DEFAULT_SERIAL_CONFIG = SERIAL_8E1;   // Default Data Bits, Parity, Stop bits. Modbus default is 8E1, another frequently used option is 8N2
                                                 // for all valid options see https://www.arduino.cc/reference/en/language/functions/communication/serial/begin/
const byte DEFAULT_FRAME_DELAY = 150;            // Default Inter-frame Delay
const uint16_t DEFAULT_RESPONSE_TIMEPOUT = 500;  // Default Response Timeout
const byte DEFAULT_ATTEMPTS = 3;                 // Default Attempts

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
const uint16_t CHECK_ETH_INTERVAL = 2000;        // Interval (ms) to check SPI connection with ethernet shield
const uint16_t ETH_RESET_DELAY = 500;            // Delay (ms) during Ethernet start, wait for Ethernet shield to start (reset issue on low quality ethernet shields)
const uint16_t WEB_IDLE_TIMEOUT = 400;           // Time (ms) from last client data after which webserver TCP socket could be disconnected, non-blocking.
const uint16_t TCP_DISCON_TIMEOUT = 500;         // Timeout (ms) for client DISCON socket command, non-blocking alternative to https://www.arduino.cc/reference/en/libraries/ethernet/client.setconnectiontimeout/
const uint16_t TCP_RETRANSMISSION_TIMEOUT = 50;  // Ethernet controller’s timeout (ms), blocking (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissiontimeout/)
const byte TCP_RETRANSMISSION_COUNT = 3;         // Number of transmission attempts the Ethernet controller will make before giving up (see https://www.arduino.cc/reference/en/libraries/ethernet/ethernet.setretransmissioncount/)
const uint16_t FETCH_INTERVAL = 2000;            // Fetch API interval (ms) for the Modbus Status webpage to renew data from JSON served by Arduino

const byte DATA_START = 96;      // Start address where config and counters are saved in EEPROM
const byte EEPROM_INTERVAL = 6;  // Interval (hours) for saving Modbus statistics to EEPROM (in order to minimize writes to EEPROM)