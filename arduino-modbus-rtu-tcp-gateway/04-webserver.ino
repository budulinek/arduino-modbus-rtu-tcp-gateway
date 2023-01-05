/* *******************************************************************
   Webserver functions

   recvWeb
   - receives GET requests for web pages
   - receives POST data from web forms
   - calls processPost
   - sends web pages, for simplicity, all web pages should be numbered (1.htm, 2.htm, ...), the page number is passed to sendPage() function
   - executes actions (such as ethernet restart, reboot) during "please wait" web page

   processPost
   - processes POST data from forms and buttons
   - updates localConfig (in RAM)
   - saves config into EEPROM
   - executes actions which do not require webserver restart

   ***************************************************************** */

const byte WEB_IN_BUFFER_SIZE = 128;  // size of web server read buffer (reads a complete line), 128 bytes necessary for POST data
const byte SMALL_BUFFER_SIZE = 32;    // a smaller buffer for uri

// Actions that need to be taken after saving configuration.
enum action_type : byte {
  NONE,
  FACTORY,      // Load default factory settings (but keep MAC address)
  MAC,          // Generate new random MAC
  REBOOT,       // Reboot the microcontroller
  ETH_SOFT,     // Ethernet software reset
  SERIAL_SOFT,  // Serial software reset
  SCAN,         // Initialize RS485 scan
  RST_STATS,         // Reset Modbus Statistics
  WEB           // Restart webserver
};
enum action_type action;

// Pages served by the webserver. Order of elements defines the order in the left menu of the web UI.
// URL of the page (*.htm) contains number corresponding to its position in this array.
// The following enum array can have a maximum of 10 elements (incl. PAGE_NONE and PAGE_WAIT)
enum page : byte {
  PAGE_NONE,  // reserved for NULL
  PAGE_INFO,
  PAGE_STATUS,
  PAGE_IP,
  PAGE_TCP,
  PAGE_RTU,
  PAGE_WAIT  // page with "Reloading. Please wait..." message. Must be the last element within this enum!!
};

// Keys for POST parameters, used in web forms and processed by processPost() function.
// Using enum ensures unique identification of each POST parameter key and consistence across functions.
// In HTML code, each element will apear as number corresponding to its position in this array.
enum post_key : byte {
  POST_NONE,  // reserved for NULL
  POST_DHCP,  // enable DHCP
  POST_IP,
  POST_IP_1,
  POST_IP_2,
  POST_IP_3,  // IP address         || Each part of an IP address has its own POST parameter.     ||
  POST_SUBNET,
  POST_SUBNET_1,
  POST_SUBNET_2,
  POST_SUBNET_3,  // subnet             || Because HTML code for IP, subnet, gateway and DNS          ||
  POST_GATEWAY,
  POST_GATEWAY_1,
  POST_GATEWAY_2,
  POST_GATEWAY_3,  // gateway            || is generated through one (nested) for-loop,                ||
  POST_DNS,
  POST_DNS_1,
  POST_DNS_2,
  POST_DNS_3,       // DNS                || all these 16 enum elements must be listed in succession!!  ||
  POST_TCP,         // TCP port                  || Because HTML code for these 3 ports              ||
  POST_UDP,         // UDP port                  || is generated through one for-loop,               ||
  POST_WEB,         // web UI port               || these 3 elements must be listed in succession!!  ||
  POST_RTU_OVER,    // RTU over TCP/UDP
  POST_BAUD,        // baud rate
  POST_DATA,        // data bits
  POST_PARITY,      // parity
  POST_STOP,        // stop bits
  POST_FRAMEDELAY,  //frame delay
  POST_TIMEOUT,     // response timeout
  POST_ATTEMPTS,    // number of request attempts
  POST_ACTION       // actions on Tools page
};

void recvWeb() {
  EthernetClient client = webServer.available();
  if (client) {
    char uri[SMALL_BUFFER_SIZE];  // the requested page
    // char requestParameter[SMALL_BUFFER_SIZE];      // parameter appended to the URI after a ?
    // char postParameter[SMALL_BUFFER_SIZE] {'\0'};         // parameter transmitted in the body / by POST
    if (client.available()) {
      char webInBuffer[WEB_IN_BUFFER_SIZE]{ '\0' };  // buffer for incoming data
      unsigned int i = 0;                            // index / current read position
      enum status_type : byte {
        REQUEST,
        CONTENT_LENGTH,
        EMPTY_LINE,
        BODY
      };
      enum status_type status;
      status = REQUEST;
      while (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (status == REQUEST)  // read the first line
          {
            // now split the input
            char *ptr;
            ptr = strtok(webInBuffer, " ");  // strtok willdestroy the newRequest
            ptr = strtok(NULL, " ");
            strlcpy(uri, ptr, sizeof(uri));      // enthält noch evtl. parameter
            status = EMPTY_LINE;                 // jump to next status
          } else if (status > REQUEST && i < 2)  // check if we have an empty line
          {
            status = BODY;
          }
          i = 0;
          memset(webInBuffer, 0, sizeof(webInBuffer));
        } else {
          if (i < (WEB_IN_BUFFER_SIZE - 1)) {
            webInBuffer[i] = c;
            i++;
            webInBuffer[i] = '\0';
          }
        }
      }
      if (status == BODY)  // status 3 could end without linefeed, therefore we takeover here also
      {
        if (webInBuffer[0] != '\0') {
          processPost(webInBuffer);
        }
      }
    }

    // Get number of the requested page from URI
    byte reqPage = 0;       //requested page
    if (!strcmp(uri, "/"))  // the homepage System Info
      reqPage = PAGE_INFO;
    else if ((uri[0] == '/') && !strcmp(uri + 2, ".htm")) {
      reqPage = (byte)(uri[1] - 48);  // Convert single ASCII char to byte
    }
    // Actions that require "please wait" page
    if (action == WEB || action == REBOOT || action == ETH_SOFT || action == FACTORY || action == MAC) {
      reqPage = PAGE_WAIT;
    }

    // Send page
    if ((reqPage > 0) && (reqPage <= PAGE_WAIT))
      sendPage(client, reqPage);
    else if (!strcmp(uri, "/favicon.ico"))  // a favicon
      send204(client);                      // if you don't have a favicon, send 204
    else                                    // if the page is unknown, HTTP response code 404
      send404(client);                      // defaults to 404 error

    // Do all actions before the "please wait" redirects (5s delay at the moment)
    if (reqPage == PAGE_WAIT) {
      for (byte n = 0; n < maxSockNum; n++) {
        // in case of webserver restart, stop only clients from old webserver (clients with port different from current settings)
        // EthernetClient clientTemp = EthernetClient(n);
        unsigned int port = W5100.readSnPORT(n);
        // for WEB, stop only clients from old webserver (those that do not match modbus ports or current web port); for other actions stop all clients
        if (action != WEB || (port && port != localConfig.webPort && port != localConfig.udpPort && port != localConfig.tcpPort)) {
          W5100.execCmdSn(n, Sock_CLOSE);  // close it forcefully
                                           // clientTemp.flush();
                                           //       clientTemp.stop();
        }
      }
      switch (action) {
        case WEB:
          webServer = EthernetServer(localConfig.webPort);
          break;
        case REBOOT:
          Serial.flush();
          Serial.end();
          resetFunc();
          break;
        default:  // ETH_SOFT, FACTORY, MAC
          Udp.stop();
          startEthernet();
          break;
      }
    }
    action = NONE;
  }
}

// This function stores POST parameter values in localConfig.
// Most changes are saved and applied immediatelly, some changes (IP settings, web server port, reboot) are saved but applied later after "please wait" page is sent.
void processPost(char postParameter[]) {
  char *point = NULL;
  char *sav1 = NULL;  // for outer strtok_r
  point = strtok_r(postParameter, "&", &sav1);
  while (point != NULL) {
    char *paramKey;
    char *paramValue;
    char *sav2 = NULL;                       // for inner strtok_r
    paramKey = strtok_r(point, "=", &sav2);  // inner strtok_r, use sav2
    paramValue = strtok_r(NULL, "=", &sav2);
    point = strtok_r(NULL, "&", &sav1);
    if (!paramValue)
      continue;  // do not process POST parameter if there is no parameter value
    byte paramKeyByte = atoi(paramKey);
    unsigned long paramValueUlong = atol(paramValue);
    switch (paramKeyByte) {
      case POST_NONE:  // reserved, because atoi / atol returns NULL in case of error
        break;
#ifdef ENABLE_DHCP
      case POST_DHCP:
        {
          extraConfig.enableDhcp = (byte)paramValueUlong;
        }
        break;
      case POST_DNS ... POST_DNS_3:
        {
          extraConfig.dns[paramKeyByte - POST_DNS] = (byte)paramValueUlong;
        }
        break;
#endif /* ENABLE_DHCP */
      case POST_IP ... POST_IP_3:
        {
          action = ETH_SOFT; // this ETH_SOFT is triggered when the user changes anything on the "IP Settings" page.
          // No need to trigger ETH_SOFT for other cases (POST_SUBNET, POST_GATEWAY etc.)
          localConfig.ip[paramKeyByte - POST_IP] = (byte)paramValueUlong;
        }
        break;
      case POST_SUBNET ... POST_SUBNET_3:
        {
          localConfig.subnet[paramKeyByte - POST_SUBNET] = (byte)paramValueUlong;
        }
        break;
      case POST_GATEWAY ... POST_GATEWAY_3:
        {
          localConfig.gateway[paramKeyByte - POST_GATEWAY] = (byte)paramValueUlong;
        }
        break;
      case POST_TCP:
        {
          for (byte i = 0; i < maxSockNum; i++) {
            EthernetClient clientTemp = EthernetClient(i);
            if (clientTemp.status() != SnSR::UDP && clientTemp.localPort() == localConfig.tcpPort) {
              clientTemp.flush();
              clientTemp.stop();
            }
          }
          localConfig.tcpPort = (unsigned int)paramValueUlong;
          modbusServer = EthernetServer(localConfig.tcpPort);
        }
        break;
      case POST_UDP:
        {
          localConfig.udpPort = (unsigned int)paramValueUlong;
          Udp.stop();
          Udp.begin(localConfig.udpPort);
        }
        break;
      case POST_WEB:
        {
          if (localConfig.webPort != (unsigned int)paramValueUlong) {
            localConfig.webPort = (unsigned int)paramValueUlong;
            action = WEB;
          }
        }
        break;
      case POST_RTU_OVER:
        localConfig.enableRtuOverTcp = (byte)paramValueUlong;
        break;
      case POST_BAUD:
        {
          action = SERIAL_SOFT; // this SERIAL_SOFT is triggered when the user changes anything on the "RS485 Settings" page.
          // No need to trigger ETH_SOFT for other cases (POST_DATA, POST_PARITY etc.)
          localConfig.baud = paramValueUlong;
          byte minFrameDelay = (byte)(frameDelay() / 1000UL) + 1;
          if (localConfig.frameDelay < minFrameDelay) {
            localConfig.frameDelay = minFrameDelay;
          }
        }
        break;
      case POST_DATA:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xF9) | (((byte)paramValueUlong - 5) << 1);
        }
        break;
      case POST_PARITY:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xCF) | ((byte)paramValueUlong << 4);
        }
        break;
      case POST_STOP:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xF7) | (((byte)paramValueUlong - 1) << 3);
        }
        break;
      case POST_FRAMEDELAY:
        localConfig.frameDelay = (byte)paramValueUlong;
        break;
      case POST_TIMEOUT:
        localConfig.serialTimeout = paramValueUlong;
        break;
      case POST_ATTEMPTS:
        localConfig.serialAttempts = (byte)paramValueUlong;
        break;
      case POST_ACTION:
        action = (action_type)paramValueUlong;
        break;
      default:
        break;
    }
  }  // while (point != NULL)
  switch (action) {
    case FACTORY:
      {
        byte tempMac[3];
        memcpy(tempMac, localConfig.macEnd, 3);  // keep current MAC
        localConfig = DEFAULT_CONFIG;
        memcpy(localConfig.macEnd, tempMac, 3);
        break;
      }
    case MAC:
      generateMac();
      break;
    case RST_STATS:
      resetStats();
      break;
    case SCAN:
      scanCounter = 1;
      memset(&stat, 0, sizeof(stat));  // clear all status flags
      break;
    default:
      break;
  }
  // new parameter values received, save them to EEPROM
  EEPROM.put(CONFIG_START + 1, localConfig);  // it is safe to call, only changed values are updated
  if (action == SERIAL_SOFT) {                // can do it without "please wait" page
    Serial.flush();
    Serial.end();
    startSerial(); // TODO clear queue?
    action = NONE;
  }
}
