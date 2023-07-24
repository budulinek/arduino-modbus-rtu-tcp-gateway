/* *******************************************************************
   Webserver functions

   recvWeb()
   - receives GET requests for web pages
   - receives POST data from web forms
   - calls processPost
   - sends web pages, for simplicity, all web pages should are numbered (1.htm, 2.htm, ...), the page number is passed to sendPage() function
   - executes actions (such as ethernet restart, reboot) during "please wait" web page

   processPost()
   - processes POST data from forms and buttons
   - updates localConfig (in RAM)
   - saves config into EEPROM
   - executes actions which do not require webserver restart

   strToByte(), hex()
   - helper functions for parsing and writing hex data

   ***************************************************************** */

const byte URI_SIZE = 24;   // a smaller buffer for uri
const byte POST_SIZE = 24;  // a smaller buffer for single post parameter + key

// Actions that need to be taken after saving configuration.
enum action_type : byte {
  ACT_NONE,
  ACT_FACTORY,        // Load default factory settings (but keep MAC address)
  ACT_MAC,            // Generate new random MAC
  ACT_REBOOT,         // Reboot the microcontroller
  ACT_RESET_ETH,      // Ethernet reset
  ACT_RESET_SERIAL,   // Serial reset
  ACT_SCAN,           // Initialize RTU scan
  ACT_RESET_STATS,    // Reset Modbus Statistics
  ACT_CLEAR_REQUEST,  // Clear Modbus Request form
  ACT_WEB             // Restart webserver
};
enum action_type action;

// Pages served by the webserver. Order of elements defines the order in the left menu of the web UI.
// URL of the page (*.htm) contains number corresponding to its position in this array.
// The following enum array can have a maximum of 10 elements (incl. PAGE_NONE and PAGE_WAIT)
enum page : byte {
  PAGE_ERROR,  // 404 Error
  PAGE_INFO,
  PAGE_STATUS,
  PAGE_IP,
  PAGE_TCP,
  PAGE_RTU,
  PAGE_WAIT,  // page with "Reloading. Please wait..." message.
  PAGE_DATA,  // data.json
};

// Keys for POST parameters, used in web forms and processed by processPost() function.
// Using enum ensures unique identification of each POST parameter key and consistence across functions.
// In HTML code, each element will apear as number corresponding to its position in this array.
enum post_key : byte {
  POST_NONE,  // reserved for NULL
  POST_DHCP,  // enable DHCP
  POST_MAC,
  POST_MAC_1,
  POST_MAC_2,
  POST_MAC_3,
  POST_MAC_4,
  POST_MAC_5,
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
  POST_DNS_3,        // DNS                || all these 16 enum elements must be listed in succession!!  ||
  POST_TCP,          // TCP port                  || Because HTML code for these 3 ports              ||
  POST_UDP,          // UDP port                  || is generated through one for-loop,               ||
  POST_WEB,          // web UI port               || these 3 elements must be listed in succession!!  ||
  POST_RTU_OVER,     // RTU over TCP/UDP
  POST_TCP_TIMEOUT,  // Modbus TCP socket close timeout
  POST_BAUD,         // baud rate
  POST_DATA,         // data bits
  POST_PARITY,       // parity
  POST_STOP,         // stop bits
  POST_FRAMEDELAY,   //frame delay
  POST_TIMEOUT,      // response timeout
  POST_ATTEMPTS,     // number of request attempts
  POST_REQ,          // Modbus request send from WebUI (first byte)
  POST_REQ_1,
  POST_REQ_2,
  POST_REQ_3,
  POST_REQ_4,
  POST_REQ_5,
  POST_REQ_6,
  POST_REQ_LAST,  // 8 bytes in total
  POST_ACTION,    // actions on Tools page
};

byte request[POST_REQ_LAST - POST_REQ + 1];  // Array to store Modbus request sent from WebUI
byte requestLen = 0;                         // Length of the Modbus request send from WebUI


// Keys for JSON elements, used in: 1) JSON documents, 2) ID of span tags, 3) Javascript.
enum JSON_type : byte {
  JSON_TIME,  // Runtime seconds
  JSON_RTU_DATA,
  JSON_ETH_DATA,
  JSON_RESPONSE,
  JSON_STATS,  // Modbus statistics from array errorCount[]
  JSON_QUEUE,
  JSON_TCP_UDP_MASTERS,  // list of Modbus TCP/UDP masters separated by <br>
  JSON_SLAVES,           // list of Modbus RTU slaves separated by <br>
  JSON_SOCKETS,
  JSON_LAST,  // Must be the very last element in this array
};

void recvWeb(EthernetClient &client) {
  char uri[URI_SIZE];  // the requested page
  memset(uri, 0, sizeof(uri));
  while (client.available()) {        // start reading the first line which should look like: GET /uri HTTP/1.1
    if (client.read() == ' ') break;  // find space before /uri
  }
  byte len = 0;
  while (client.available() && len < sizeof(uri) - 1) {
    char c = client.read();  // parse uri
    if (c == ' ') break;     // find space after /uri
    uri[len] = c;
    len++;
  }
  while (client.available()) {
    if (client.read() == '\r')
      if (client.read() == '\n')
        if (client.read() == '\r')
          if (client.read() == '\n')
            break;  // find 2 end of lines between header and body
  }
  if (client.available()) {
    processPost(client);  // parse post parameters
  }

  // Get the requested page from URI
  byte reqPage = PAGE_ERROR;  // requested page, 404 error is a default
  if (uri[0] == '/') {
    if (uri[1] == '\0')  // the homepage System Info
      reqPage = PAGE_INFO;
    else if (!strcmp(uri + 2, ".htm")) {
      reqPage = byte(uri[1] - 48);  // Convert single ASCII char to byte
      if (reqPage >= PAGE_WAIT) reqPage = PAGE_ERROR;
    } else if (!strcmp(uri, "/d.json")) {
      reqPage = PAGE_DATA;
    }
  }
  // Actions that require "please wait" page
  if (action == ACT_WEB || action == ACT_MAC || action == ACT_RESET_ETH || action == ACT_REBOOT || action == ACT_FACTORY) {
    reqPage = PAGE_WAIT;
  }
  // Send page
  sendPage(client, reqPage);

  // Do all actions before the "please wait" redirects (5s delay at the moment)
  if (reqPage == PAGE_WAIT) {
    switch (action) {
      case ACT_WEB:
        for (byte s = 0; s < maxSockNum; s++) {
          // close old webserver TCP connections
          if (EthernetClient(s).localPort() != localConfig.tcpPort) {
            disconSocket(s);
          }
        }
        webServer = EthernetServer(localConfig.webPort);
        break;
      case ACT_MAC:
      case ACT_RESET_ETH:
        for (byte s = 0; s < maxSockNum; s++) {
          // close all TCP and UDP sockets
          disconSocket(s);
        }
        startEthernet();
        break;
      case ACT_REBOOT:
      case ACT_FACTORY:
        resetFunc();
        break;
      default:
        break;
    }
  }
  action = ACT_NONE;
}

// This function stores POST parameter values in localConfig.
// Most changes are saved and applied immediatelly, some changes (IP settings, web server port, reboot) are saved but applied later after "please wait" page is sent.
void processPost(EthernetClient &client) {
  while (client.available()) {
    char post[POST_SIZE];
    byte len = 0;
    while (client.available() && len < sizeof(post) - 1) {
      char c = client.read();
      if (c == '&') break;
      post[len] = c;
      len++;
    }
    post[len] = '\0';
    char *paramKey = post;
    char *paramValue = post;
    while (*paramValue) {
      if (*paramValue == '=') {
        paramValue++;
        break;
      }
      paramValue++;
    }
    if (*paramValue == '\0')
      continue;  // do not process POST parameter if there is no parameter value
    byte paramKeyByte = strToByte(paramKey);
    uint16_t paramValueUint = atol(paramValue);
    switch (paramKeyByte) {
      case POST_NONE:  // reserved, because atoi / atol returns NULL in case of error
        break;
#ifdef ENABLE_DHCP
      case POST_DHCP:
        {
          localConfig.enableDhcp = byte(paramValueUint);
        }
        break;
      case POST_DNS ... POST_DNS_3:
        {
          localConfig.dns[paramKeyByte - POST_DNS] = byte(paramValueUint);
        }
        break;
#endif /* ENABLE_DHCP */
      case POST_REQ ... POST_REQ_LAST:
        {
          requestLen = paramKeyByte - POST_REQ + 1;
          request[requestLen - 1] = strToByte(paramValue);
        }
        break;
      case POST_MAC ... POST_MAC_5:
        {
          action = ACT_RESET_ETH;  // this RESET_ETH is triggered when the user changes anything on the "IP Settings" page.
                                   // No need to trigger RESET_ETH for other cases (POST_SUBNET, POST_GATEWAY etc.)
                                   // if "Randomize" button is pressed, action is set to ACT_MAC
          mac[paramKeyByte - POST_MAC] = strToByte(paramValue);
        }
        break;
      case POST_IP ... POST_IP_3:
        {
          localConfig.ip[paramKeyByte - POST_IP] = byte(paramValueUint);
        }
        break;
      case POST_SUBNET ... POST_SUBNET_3:
        {
          localConfig.subnet[paramKeyByte - POST_SUBNET] = byte(paramValueUint);
        }
        break;
      case POST_GATEWAY ... POST_GATEWAY_3:
        {
          localConfig.gateway[paramKeyByte - POST_GATEWAY] = byte(paramValueUint);
        }
        break;
      case POST_TCP:
        {
          if (paramValueUint != localConfig.webPort && paramValueUint != localConfig.tcpPort) {  // continue only of the value changed and it differs from WebUI port
            for (byte s = 0; s < maxSockNum; s++) {
              if (EthernetClient(s).localPort() == localConfig.tcpPort) {  // close only Modbus TCP sockets
                disconSocket(s);
              }
            }
            localConfig.tcpPort = paramValueUint;
            modbusServer = EthernetServer(localConfig.tcpPort);
          }
        }
        break;
      case POST_UDP:
        {
          localConfig.udpPort = paramValueUint;
          Udp.stop();
          Udp.begin(localConfig.udpPort);
        }
        break;
      case POST_WEB:
        {
          if (paramValueUint != localConfig.webPort && paramValueUint != localConfig.tcpPort) {  // continue only of the value changed and it differs from Modbus TCP port
            localConfig.webPort = paramValueUint;
            action = ACT_WEB;
          }
        }
        break;
      case POST_RTU_OVER:
        localConfig.enableRtuOverTcp = byte(paramValueUint);
        break;
      case POST_TCP_TIMEOUT:
        localConfig.tcpTimeout = paramValueUint;
        break;
      case POST_BAUD:
        {
          action = ACT_RESET_SERIAL;  // this RESET_SERIAL is triggered when the user changes anything on the "RTU Settings" page.
          // No need to trigger RESET_ETH for other cases (POST_DATA, POST_PARITY etc.)
          localConfig.baud = paramValueUint;
          byte minFrameDelay = byte((frameDelay() / 1000UL) + 1);
          if (localConfig.frameDelay < minFrameDelay) {
            localConfig.frameDelay = minFrameDelay;
          }
        }
        break;
      case POST_DATA:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xF9) | ((byte(paramValueUint) - 5) << 1);
        }
        break;
      case POST_PARITY:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xCF) | (byte(paramValueUint) << 4);
        }
        break;
      case POST_STOP:
        {
          localConfig.serialConfig = (localConfig.serialConfig & 0xF7) | ((byte(paramValueUint) - 1) << 3);
        }
        break;
      case POST_FRAMEDELAY:
        localConfig.frameDelay = byte(paramValueUint);
        break;
      case POST_TIMEOUT:
        localConfig.serialTimeout = paramValueUint;
        break;
      case POST_ATTEMPTS:
        localConfig.serialAttempts = byte(paramValueUint);
        break;
      case POST_ACTION:
        action = action_type(paramValueUint);
        break;
      default:
        break;
    }
  }
  switch (action) {
    case ACT_FACTORY:
      localConfig = DEFAULT_CONFIG;
      resetStats();
      break;
    case ACT_RESET_STATS:
      resetStats();
      break;
    case ACT_MAC:
      generateMac();
      break;
    case ACT_RESET_SERIAL:
      clearQueue();
      startSerial();
      break;
    case ACT_SCAN:
      scanCounter = 1;
      memset(&slaveStatus, 0, sizeof(slaveStatus));  // clear all status flags
      break;
    case ACT_CLEAR_REQUEST:
      requestLen = 0;
      responseLen = 0;
      break;
    default:
      break;
  }
  // if new Modbus request received, put into queue
  if (requestLen > 1 && queueHeaders.available() > 1 && queueData.available() > requestLen) {  // at least 2 bytes in request (slave address and function)
    // push to queue
    queueHeaders.push(header{
      { 0x00, 0x00 },  // tid[2]
      requestLen,      // msgLen
      { 0, 0, 0, 0 },  // remIP[4]
      0,               // remPort
      UDP_REQUEST,     // requestType
      0,               // atts
    });
    for (byte i = 0; i < requestLen; i++) {
      queueData.push(request[i]);
    }
    responseLen = 0;  // clear old Modbus Response from WebUI
  }
  // new parameter values received, save them to EEPROM
  updateEeprom();  // it is safe to call, only changed values (and changed error and data counters) are updated
}

// takes 2 chars, 1 char + null byte or 1 null byte
byte strToByte(const char myStr[]) {
  if (!myStr) return 0;
  byte x = 0;
  for (byte i = 0; i < 2; i++) {
    char c = myStr[i];
    if (c >= '0' && c <= '9') {
      x *= 16;
      x += c - '0';
    } else if (c >= 'A' && c <= 'F') {
      x *= 16;
      x += (c - 'A') + 10;
    } else if (c >= 'a' && c <= 'f') {
      x *= 16;
      x += (c - 'a') + 10;
    }
  }
  return x;
}

// from https://github.com/RobTillaart/printHelpers
char __printbuffer[3];
char *hex(byte val) {
  char *buffer = __printbuffer;
  byte digits = 2;
  buffer[digits] = '\0';
  while (digits > 0) {
    byte v = val & 0x0F;
    val >>= 4;
    digits--;
    buffer[digits] = (v < 10) ? '0' + v : ('A' - 10) + v;
  }
  return buffer;
}
