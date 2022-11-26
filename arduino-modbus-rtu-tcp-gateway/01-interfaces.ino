/* *******************************************************************
   Ethernet and serial interface functions

   startSerial
   - starts HW serial interface which we use for RS485 line
   - calculates Modbus RTU character timeout and frame delay

   startEthernet
   - initiates ethernet interface
   - if enabled, gets IP from DHCP
   - starts all servers (Modbus TCP, UDP, web server)

   resetFunc
   - well... resets Arduino

   maintainDhcp()
   - maintain DHCP lease

   maintainUptime
   - maintains up time in case of millis() overflow

   maintainCounters
   - synchronizes roll-over of data counters to zero

   CreateTrulyRandomSeed
   - seed pseudorandom generator using  watch dog timer interrupt (works only on AVR)
   - see https://sites.google.com/site/astudyofentropy/project-definition/timer-jitter-entropy-sources/entropy-library/arduino-random-seed

   generateMac()
   - generate random MAC using pseudo random generator (faster and than build-in random())

   + preprocessor code for identifying microcontroller board

   ***************************************************************** */


void startSerial() {
  mySerial.begin(localConfig.baud, localConfig.serialConfig);
  // Calculate Modbus RTU character timeout and frame delay
  byte bits =                                         // number of bits per character (11 in default Modbus RTU settings)
    1 +                                               // start bit
    (((localConfig.serialConfig & 0x06) >> 1) + 5) +  // data bits
    (((localConfig.serialConfig & 0x08) >> 3) + 1);   // stop bits
  if (((localConfig.serialConfig & 0x30) >> 4) > 1) bits += 1;    // parity bit (if present)
  int T = ((unsigned long)bits * 1000000UL) / localConfig.baud;       // time to send 1 character over serial in microseconds
  if (localConfig.baud <= 19200) {
    charTimeout = 1.5 * T;         // inter-character time-out should be 1,5T
    frameDelay = 3.5 * T;         // inter-frame delay should be 3,5T
  }
  else {
    charTimeout = 750;
    frameDelay = 1750;
  }
#ifdef RS485_CONTROL_PIN
  pinMode(RS485_CONTROL_PIN, OUTPUT);
  digitalWrite(RS485_CONTROL_PIN, RS485_RECEIVE);  // Init Transceiver
#endif /* RS485_CONTROL_PIN */
}

void startEthernet() {
  if (ethResetPin != 0) {
    pinMode(ethResetPin, OUTPUT);
    digitalWrite(ethResetPin, LOW);
    delay(25);
    digitalWrite(ethResetPin, HIGH);
    delay(500);
  }
  byte mac[6];
  memcpy(mac, MAC_START, 3); // set first 3 bytes
  memcpy(mac + 3, localConfig.macEnd, 3); // set last 3 bytes
#ifdef ENABLE_DHCP
  if (localConfig.enableDhcp) {
    dhcpSuccess = Ethernet.begin(mac);
  }
  if (!localConfig.enableDhcp || dhcpSuccess == false) {
    Ethernet.begin(mac, localConfig.ip, localConfig.dns, localConfig.gateway, localConfig.subnet);
  }
#else /* ENABLE_DHCP */
  Ethernet.begin(mac, localConfig.ip, localConfig.dns, localConfig.gateway, localConfig.subnet);
  localConfig.enableDhcp = false;                 // Make sure Dhcp is disabled in config
#endif /* ENABLE_DHCP */
  modbusServer = EthernetServer(localConfig.tcpPort);
  webServer = EthernetServer(localConfig.webPort);
  Udp.begin(localConfig.udpPort);
  modbusServer.begin();
  webServer.begin();
  // Ethernet library determines MAX_SOCK_NUM by Microcontroller RAM (not by Ethernet chip type).
  // Therefore, for Arduino Mega + W5100, MAX_SOCK_NUM is wrongly set to 8
  if (Ethernet.hardwareStatus() == EthernetW5100) {
    maxSockNum = 4;
  }
  dbg(F("[arduino] Server available at http://"));
  dbgln(Ethernet.localIP());
}

void(* resetFunc) (void) = 0;   //declare reset function at address 0

void maintainDhcp()
{
  if (localConfig.enableDhcp && dhcpSuccess == true) {      // only call maintain if initial DHCP request by startEthernet was successfull
    uint8_t maintainResult = Ethernet.maintain();
    if (maintainResult == 1 || maintainResult == 3) {    // renew failed or rebind failed
      dhcpSuccess = false;
      startEthernet();      // another DHCP request, fallback to static IP
    }
  }
}

void maintainUptime()
{
  unsigned long milliseconds = millis();
  if (last_milliseconds > milliseconds) {
    //in case of millis() overflow, store existing passed seconds
    remaining_seconds = seconds;
  }
  //store last millis(), so that we can detect on the next call
  //if there is a millis() overflow ( millis() returns 0 )
  last_milliseconds = milliseconds;
  //In case of overflow, the "remaining_seconds" variable contains seconds counted before the overflow.
  //We add the "remaining_seconds", so that we can continue measuring the time passed from the last boot of the device.
  seconds = (milliseconds / 1000) + remaining_seconds;
}

void maintainCounters()
{
  // synchronize roll-over of data counters to zero, at 0xFFFFFF00 or 0xFF00 respectively
#ifdef ENABLE_EXTRA_DIAG
  const unsigned long rollover = 0xFFFFFF00;
#else
  const unsigned int rollover = 0xFF00;
#endif /* ENABLE_EXTRA_DIAG */
  if (serialTxCount > rollover || serialRxCount > rollover || ethTxCount > rollover || ethRxCount > rollover) {
    serialRxCount = 0;
    serialTxCount = 0;
    ethRxCount = 0;
    ethTxCount = 0;
  }
}


void generateMac()
{
  // Marsaglia algorithm from https://github.com/RobTillaart/randomHelpers
  seed1 = 36969L * (seed1 & 65535L) + (seed1 >> 16);
  seed2 = 18000L * (seed2 & 65535L) + (seed2 >> 16);
  uint32_t randomBuffer = (seed1 << 16) + seed2;  /* 32-bit random */

  for (byte i = 0; i < 3; i++) {
    localConfig.macEnd[i] = randomBuffer & 0xFF;
    randomBuffer >>= 8;
  }
}

void CreateTrulyRandomSeed()
{
  seed1 = 0;
  nrot = 32; // Must be at least 4, but more increased the uniformity of the produced
  // seeds entropy.

  // The following five lines of code turn on the watch dog timer interrupt to create
  // the seed value
  cli();
  MCUSR = 0;
  _WD_CONTROL_REG |= (1 << _WD_CHANGE_BIT) | (1 << WDE);
  _WD_CONTROL_REG = (1 << WDIE);
  sei();

  while (nrot > 0);  // wait here until seed is created

  // The following five lines turn off the watch dog timer interrupt
  cli();
  MCUSR = 0;
  _WD_CONTROL_REG |= (1 << _WD_CHANGE_BIT) | (0 << WDE);
  _WD_CONTROL_REG = (0 << WDIE);
  sei();
}

ISR(WDT_vect)
{
  nrot--;
  seed1 = seed1 << 8;
  seed1 = seed1 ^ TCNT1L;
}

// Board definitions
#if defined(TEENSYDUINO)

//  --------------- Teensy -----------------

#if defined(__AVR_ATmega32U4__)
#define BOARD F("Teensy 2.0")
#elif defined(__AVR_AT90USB1286__)
#define BOARD F("Teensy++ 2.0")
#elif defined(__MK20DX128__)
#define BOARD F("Teensy 3.0")
#elif defined(__MK20DX256__)
#define BOARD F("Teensy 3.2") // and Teensy 3.1 (obsolete)
#elif defined(__MKL26Z64__)
#define BOARD F("Teensy LC")
#elif defined(__MK64FX512__)
#define BOARD F("Teensy 3.5")
#elif defined(__MK66FX1M0__)
#define BOARD F("Teensy 3.6")
#else
#error "Unknown board"
#endif

#else // --------------- Arduino ------------------

#if   defined(ARDUINO_AVR_ADK)
#define BOARD F("Arduino Mega Adk")
#elif defined(ARDUINO_AVR_BT)    // Bluetooth
#define BOARD F("Arduino Bt")
#elif defined(ARDUINO_AVR_DUEMILANOVE)
#define BOARD F("Arduino Duemilanove")
#elif defined(ARDUINO_AVR_ESPLORA)
#define BOARD F("Arduino Esplora")
#elif defined(ARDUINO_AVR_ETHERNET)
#define BOARD F("Arduino Ethernet")
#elif defined(ARDUINO_AVR_FIO)
#define BOARD F("Arduino Fio")
#elif defined(ARDUINO_AVR_GEMMA)
#define BOARD F("Arduino Gemma")
#elif defined(ARDUINO_AVR_LEONARDO)
#define BOARD F("Arduino Leonardo")
#elif defined(ARDUINO_AVR_LILYPAD)
#define BOARD F("Arduino Lilypad")
#elif defined(ARDUINO_AVR_LILYPAD_USB)
#define BOARD F("Arduino Lilypad Usb")
#elif defined(ARDUINO_AVR_MEGA)
#define BOARD F("Arduino Mega")
#elif defined(ARDUINO_AVR_MEGA2560)
#define BOARD F("Arduino Mega 2560")
#elif defined(ARDUINO_AVR_MICRO)
#define BOARD F("Arduino Micro")
#elif defined(ARDUINO_AVR_MINI)
#define BOARD F("Arduino Mini")
#elif defined(ARDUINO_AVR_NANO)
#define BOARD F("Arduino Nano")
#elif defined(ARDUINO_AVR_NG)
#define BOARD F("Arduino NG")
#elif defined(ARDUINO_AVR_PRO)
#define BOARD F("Arduino Pro")
#elif defined(ARDUINO_AVR_ROBOT_CONTROL)
#define BOARD F("Arduino Robot Ctrl")
#elif defined(ARDUINO_AVR_ROBOT_MOTOR)
#define BOARD F("Arduino Robot Motor")
#elif defined(ARDUINO_AVR_UNO)
#define BOARD F("Arduino Uno")
#elif defined(ARDUINO_AVR_YUN)
#define BOARD F("Arduino Yun")

// These boards must be installed separately:
#elif defined(ARDUINO_SAM_DUE)
#define BOARD F("Arduino Due")
#elif defined(ARDUINO_SAMD_ZERO)
#define BOARD F("Arduino Zero")
#elif defined(ARDUINO_ARC32_TOOLS)
#define BOARD F("Arduino 101")
#else
#error "Unknown board"
#endif

#endif
