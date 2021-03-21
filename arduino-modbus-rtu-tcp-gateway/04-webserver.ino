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


const byte pagesCnt = 5;                   // number of consecutive pages ("1.htm", "2.htm"....) served by the server, maximum is 9
const uint16_t webInBufferSize = 128;      // size of web server read buffer (reads a complete line), 128 bytes necessary for POST data
const uint16_t smallbuffersize = 32;       // a smaller buffer for uri

// Actions that need to be taken after saving configuration.
// Order of these actions must correspond to POST param values of buttons on the "Tools" web page (e.g. "value=1" for Restore factory defaults, etc.)
enum action_type : byte
{
  NONE, FACTORY, MAC, ETH_SOFT, ETH_HARD, SERIAL_SOFT, REBOOT, SCAN, WEB
};
enum action_type action;

void recvWeb()
{
  EthernetClient client = webServer.available();
  if (client) {
    dbg(F("[web] Data from client "));

    char uri[smallbuffersize];                   // the requestet page, shorter than smallbuffersize - method
    // char requestParameter[smallbuffersize];      // parameter appended to the URI after a ?
    // char postParameter[smallbuffersize] {'\0'};         // parameter transmitted in the body / by POST
    // while (client.connected()) {
    if (client.available()) {
      char webInBuffer[webInBufferSize] {'\0'};          // buffer for incoming data
      int i = 0;                               // index / current read position
      enum status_type : byte
      {
        REQUEST, CONTENT_LENGTH, EMPTY_LINE, BODY
      };
      enum status_type status;
      status = REQUEST;
      while (client.available()) {
        char c = client.read();
        // dbg(c);     // Debug print received characters to Serial monitor
        if ( c == '\n' )
        {
          if (status == REQUEST)         // read the first line
          {
            dbg(F("[web] webInBuffer=")); dbgln(webInBuffer);
            // now split the input
            char *ptr;
            ptr = strtok(webInBuffer, " ");       // strtok willdestroy the newRequest
            ptr = strtok(NULL, " ");
            strlcpy(uri, ptr, sizeof(uri));  // enthält noch evtl. parameter
            dbg(F("[web] uri=")); dbgln(uri);
            status = EMPTY_LINE;                   // jump to next status
          }
          else if (status > REQUEST && i < 2)      // check if we have an empty line
          {
            status = BODY;
          }
          //            else if (status == BODY)
          //            {
          //              dbg(F("[web] postParameter=")); dbgln(webInBuffer);
          //              if (webInBuffer[0] != '\0') {
          //                dbg(F("[web] Processing POST"));
          //                processPost(webInBuffer);
          //              }
          //              // strlcpy(postParameter, webInBuffer, sizeof(postParameter));
          //              break; // we have received one line payload and break out
          //            }
          i = 0;
          memset(webInBuffer, 0, sizeof(webInBuffer));
        }
        else
        {
          if (i < (webInBufferSize - 1))           // <==== BUG FOUND
          {
            webInBuffer[i] = c;
            i++;
            webInBuffer[i] = '\0';
          }
        }
      }
      if (status == BODY)      // status 3 could end without linefeed, therefore we takeover here also
      {
        dbg(F("[web] POST data=")); dbgln(webInBuffer);
        if (webInBuffer[0] != '\0') {
          processPost(webInBuffer);
        }
      }
    }

    // send back a response
    // Get number of the requested page
    byte reqPage = 0;      //requested page
    if ((uri[0] == '/') && !strcmp(uri + 2, ".htm")) {
      reqPage = uri[1] - 48;                // Convert ASCII to byte
    }

    // Actions that require "please wait" page
    if (action == WEB || action == REBOOT || action == ETH_SOFT || action == ETH_HARD || action == FACTORY || action == MAC) {
      sendPage(client, 0xFF);                                 // Send "please wait" page
      // delay(1);                          // do we need it??
      // Do all actions before the "please wait" redirects (5s delay at the moment)
      for (byte n = 0; n < Ethernet._maxSockNum; n++) {
        // in case of webserver restart, stop only clients from old webserver (clients with port different from current settings)
        EthernetClient clientTemp = EthernetClient(n);
        if (action != WEB || (Ethernet._server_port[n] && Ethernet._server_port[n] != localConfig.webPort && Ethernet._server_port[n] != localConfig.udpPort && Ethernet._server_port[n] != localConfig.tcpPort)) {
          clientTemp.flush();
          clientTemp.stop();
        }
      }
      switch (action) {
        case WEB:
          webServer = EthernetServer(localConfig.webPort);
          break;
        case REBOOT:
          Serial.flush();
          Serial.end();
          delay(1);
          resetFunc();
          break;
        case ETH_HARD:
          Ethernet.hardreset();
        case ETH_SOFT:
        case FACTORY:
        case MAC:
          Ethernet.softreset();
        default:          // triggered by cases without break;
          Udp.stop();
          startEthernet();
          break;
      }
    }
    else if (!strcmp(uri, "/") || !strcmp(uri, "/index.htm"))        // the homepage Current Status
      sendPage(client, 1);
    else if ((reqPage > 0) && (reqPage <= pagesCnt))
      sendPage(client, reqPage);
    else if (!strcmp(uri, "/favicon.ico"))     // a favicon
      send204(client);                         // if you don't have a favicon, send 204
    else                                       // if the page is unknown, HTTP response code 404
      send404(client);
    // delay(1);                          // do we need it??
    client.stop();
    action = NONE;
    dbg(F("[web] Stop client "));
    // }
  }         
}


// This function stores POST parameter values in localConfig.
// Most changes are saved and applied immediatelly, some changes (IP settings, web server port, actions in "Tools" page) are saved but applied later after "please wait" page is sent.
void processPost(char postParameter[]) {
  char *point = NULL;
  char *sav1 = NULL;              // for outer strtok_r
  point = strtok_r(postParameter, "&", &sav1);
  while (point != NULL) {
    char *paramKey;
    char *paramValue;
    char *sav2 = NULL;            // for inner strtok_r
    char buf[7] {'\0'};           // for itoa, max length 6 bytes
    paramKey = strtok_r(point, "=", &sav2);   // inner strtok_r, use sav2

    switch (paramKey[0]) {
      case 'i':                               // processing IP Settings
        {
          paramValue = strtok_r(NULL, "=", &sav2);
          int paramValueInt = atoi(paramValue);
          if (paramValue && (paramValueInt >= 0 && paramValueInt <= 255)) {
            byte j = atoi(paramKey + 1);
            if (j <= 0);           // do nothing
            else if (j == 1) {       // Enable DHCP
              if ((byte)paramValueInt != localConfig.enableDhcp) {
                action = ETH_SOFT;
                localConfig.enableDhcp = (byte)paramValueInt;
              }
            } else if (j <= 5) {      // IP
              if ((byte)paramValueInt != localConfig.ip[j - 2]) {
                action = ETH_SOFT;
                localConfig.ip[j - 2] = (byte)paramValueInt;
              }
            } else if (j <= 9) {      // Subnet
              if ((byte)paramValueInt != localConfig.subnet[j - 6]) {
                action = ETH_SOFT;
                localConfig.subnet[j - 6] = (byte)paramValueInt;
              }
            } else if (j <= 13) {      // Gateway
              if ((byte)paramValueInt != localConfig.gateway[j - 10]) {
                action = ETH_SOFT;
                localConfig.gateway[j - 10] = (byte)paramValueInt;
              }
            } else if (j <= 17) {      // DNS
              if ((byte)paramValueInt != localConfig.dns[j - 14]) {
                action = ETH_SOFT;
                localConfig.dns[j - 14] = (byte)paramValueInt;
              }
            }
          }
          break;
        }
      case 't':                               // processing TCP/UDP Settings
        {
          paramValue = strtok_r(NULL, "=", &sav2);
          unsigned int paramValueUint = atoi(paramValue);
          if (paramValue) {
            switch (atoi(paramKey + 1)) {
              case 1:             // TCP port
                if (localConfig.tcpPort != paramValueUint) {
                  for (byte i = 0; i < Ethernet._maxSockNum; i++) {
                    EthernetClient clientTemp = EthernetClient(i);
                    if (Ethernet._server_port[i] == localConfig.tcpPort) {
                      clientTemp.flush();
                      clientTemp.stop();
                    }
                  }
                  localConfig.tcpPort = paramValueUint;
                  modbusServer = EthernetServer(localConfig.tcpPort);
                }
                break;
              case 2:             // UDP Port
                if (localConfig.udpPort != paramValueUint) {
                  localConfig.udpPort = paramValueUint;
                  Udp.stop();
                  Udp.begin(localConfig.udpPort);
                }
                break;
              case 3:             // Web Port
                if (localConfig.webPort != paramValueUint) {
                  localConfig.webPort = paramValueUint;
                  action = WEB;
                }
                break;
              case 4:             // Enable Modbus RTU over TCP/UDP
                localConfig.enableRtuOverTcp = (byte)paramValueUint;
                break;
              default:
                break;
            }
          }
          break;
        }
      case 'r':                               // processing RS485 Settings
        {
          paramValue = strtok_r(NULL, "=", &sav2);
          unsigned long paramValueUlong = atol(paramValue);
          if (paramValue) {
            switch (atoi(paramKey + 1)) {
              case 1:             // Baud
                if (localConfig.baud != paramValueUlong) {
                  action = SERIAL_SOFT;
                  localConfig.baud = paramValueUlong;
                }
                break;
              case 2:             // Data Size
                if ((((localConfig.serialConfig & 0x06) >> 1) + 5) != atoi(paramValue)) {
                  action = SERIAL_SOFT;
                  localConfig.serialConfig = (localConfig.serialConfig & 0xF9) | ((atoi(paramValue) - 5) << 1);
                }
                break;
              case 3:             // Parity
                if (((localConfig.serialConfig & 0x30) >> 4) != atoi(paramValue)) {
                  action = SERIAL_SOFT;
                  localConfig.serialConfig = (localConfig.serialConfig & 0xCF) | (atoi(paramValue) << 4);
                }
                break;
              case 4:             // Stop Bits
                if ((((localConfig.serialConfig & 0x08) >> 3) + 1) != atoi(paramValue)) {
                  action = SERIAL_SOFT;
                  localConfig.serialConfig = (localConfig.serialConfig & 0xF7) | ((atoi(paramValue) - 1) << 3);
                }
                break;
              case 5:             // Modbus timeout
                localConfig.serialTimeout = paramValueUlong;
                break;
              case 6:             // Modbus retry
                localConfig.serialRetry = (byte)paramValueUlong;
                break;
              default:
                break;
            }
          }
          break;
        }
      case 'a':                               // processing Tools buttons
        {
          action = atoi(strtok_r(NULL, "=", &sav2));
          break;
        default:
          break;
        }
    }
    point = strtok_r(NULL, "&", &sav1);
  }     // while (point != NULL)
  switch (action) {
    case FACTORY:
      {
        byte tempMac[6];
        memcpy(tempMac, localConfig.mac, 6); // keep current MAC
        localConfig = defaultConfig;
        memcpy(localConfig.mac, tempMac, 6);
        break;
      }
    case MAC:
      for (int i = 3; i < 6; i++) {
        localConfig.mac[i] = TrueRandom.randomByte();
      }
      break;
    case SCAN:
      scanCounter = 1;
      break;
    default:
      break;
  }
  // new parameter values received, save them to EEPROM
  EEPROM.put(configStart, localConfig);    // it is safe to call, only changed values are updated
  if (action == SERIAL_SOFT) {              // can do it without "please wait" page
    dbgln(F("[serial] reload Serial"));
    Serial.flush();
    Serial.end();
    startSerial();
    action = NONE;
  }
}
