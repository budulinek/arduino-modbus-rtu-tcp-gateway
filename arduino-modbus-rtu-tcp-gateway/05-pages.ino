const byte WEB_OUT_BUFFER_SIZE = 64;  // size of web server write buffer (used by StreamLib)

/**************************************************************************/
/*!
  @brief Sends the requested page (incl. 404 error and JSON document),
  displays main page, renders title and left menu using, calls content functions
  depending on the number (i.e. URL) of the requested web page.
  In order to save flash memory, some HTML closing tags are omitted,
  new lines in HTML code are also omitted.
  @param client Ethernet TCP client
  @param reqPage Requested page number
*/
/**************************************************************************/
void sendPage(EthernetClient &client, byte reqPage) {
  char webOutBuffer[WEB_OUT_BUFFER_SIZE];
  ChunkedPrint chunked(client, webOutBuffer, sizeof(webOutBuffer));  // the StreamLib object to replace client print
  if (reqPage == PAGE_ERROR) {
    chunked.print(F(
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 13\r\n\r\n"
      "404 Not Found"));
    chunked.flush();
    return;
  } else if (reqPage == PAGE_DATA) {
    chunked.print(F("HTTP/1.1 200\r\n"  // An advantage of HTTP 1.1 is that you can keep the connection alive
                    "Content-Type: application/json\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"));
    chunked.begin();
    chunked.print(F("{"));
    for (byte i = 0; i < JSON_LAST; i++) {
      if (i) chunked.print(F(","));
      chunked.print(F("\""));
      chunked.print(i);
      chunked.print(F("\":\""));
      jsonVal(chunked, i);
      chunked.print(F("\""));
    }
    chunked.print(F("}"));
    chunked.end();
    return;
  }
  chunked.print(F("HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"));
  chunked.begin();
  chunked.print(F("<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width"));
  if (reqPage == PAGE_WAIT) {  // redirect to new IP and web port
    chunked.print(F("'>"
                    "<meta http-equiv=refresh content='5; url=http://"));
    chunked.print(IPAddress(data.config.ip));
    chunked.print(F(":"));
    chunked.print(data.config.webPort);
  }
  chunked.print(F("'>"
                  "<title>Modbus RTU &rArr; Modbus TCP/UDP Gateway</title>"
                  "<style>"
                  /*
                  HTML Tags
                    h1 - main title of the page
                    h4 - text in navigation menu and header of page content
                    a - items in left navigation menu
                    label - first cell of a row in content
                  CSS Classes
                    w - wrapper (includes m + c)
                    m  - navigation menu (left)
                    c - wrapper for the content of a page (incl. smaller header and main)
                    q - row inside a content (default: top-aligned)
                    r - row inside a content (adds: center-aligned)
                    i - short input (byte or IP address octet)
                    n - input type=number
                    s - select input with numbers
                    p - inputs disabled by id=o checkbox
                  CSS Ids
                    o - checkbox which disables other checkboxes and inputs
                  */
                  "*{box-sizing:border-box}"
                  "body{padding:1px;margin:0;font-family:sans-serif;height:100vh}"
                  "body,.w,.c,.q{display:flex}"
                  "body,.c{flex-flow:column}"
                  ".w{flex-grow:1;min-height:0}"
                  ".m{flex:0 0 20vw}"
                  ".c{flex:1}"
                  ".m,main{overflow:auto;padding:15px}"
                  ".m,.q{padding:1px}"
                  ".r{align-items:center}"
                  "h1,h4{padding:10px}"
                  "h1,.m,h4{background:#0067AC;margin:1px}"
                  "a,h1,h4{color:white;text-decoration:none}"
                  ".c h4{padding-left:30%}"
                  "label{width:30%;text-align:right;margin-right:2px}"
                  ".s{text-align:right}"
                  ".s>option{direction:rtl}"
                  ".i{text-align:center;width:4ch;color:black}"
                  ".n{width:10ch}"
                  "</style>"
                  "</head>"
                  "<body"));
#ifdef ENABLE_DHCP
  chunked.print(F(" onload=g(document.getElementById('o').checked)>"
                  "<script>function g(h) {var x = document.getElementsByClassName('p');for (var i = 0; i < x.length; i++) {x[i].disabled = h}}</script"));
#endif /* ENABLE_DHCP */
  if (reqPage == PAGE_STATUS) {
    chunked.print(F("><script>"
                    "var a;"
                    "const b=()=>{"
                    "fetch('d.json')"  // Call the fetch function passing the url of the API as a parameter
                    ".then(e=>{return e.json();a=0})"
                    ".then(f=>{for(var i in f){if(document.getElementById(i))document.getElementById(i).innerHTML=f[i];}})"
                    ".catch(()=>{if(!a){alert('Connnection lost');a=1}})"
                    "};"
                    "setInterval(()=>b(),"));
    chunked.print(FETCH_INTERVAL);
    chunked.print(F(");"
                    "</script"));
  }
  chunked.print(F(">"
                  "<h1>Modbus RTU &rArr; Modbus TCP/UDP Gateway</h1>"
                  "<div class=w>"
                  "<div class=m>"));

  // Left Menu
  for (byte i = 1; i < PAGE_WAIT; i++) {  // PAGE_WAIT is the last item in enum
    chunked.print(F("<h4"));
    if ((i) == reqPage) {
      chunked.print(F(" style=background-color:#FF6600"));
    }
    chunked.print(F("><a href="));
    chunked.print(i);
    chunked.print(F(".htm>"));
    stringPageName(chunked, i);
    chunked.print(F("</a></h4>"));
  }
  chunked.print(F("</div>"  // <div class=w>
                  "<div class=c>"
                  "<h4>"));
  stringPageName(chunked, reqPage);
  chunked.print(F("</h4>"
                  "<main>"
                  "<form method=post>"));

  //   PLACE FUNCTIONS PROVIDING CONTENT HERE
  switch (reqPage) {
    case PAGE_INFO:
      contentInfo(chunked);
      break;
    case PAGE_STATUS:
      contentStatus(chunked);
      break;
    case PAGE_IP:
      contentIp(chunked);
      break;
    case PAGE_TCP:
      contentTcp(chunked);
      break;
    case PAGE_RTU:
      contentRtu(chunked);
      break;
    case PAGE_TOOLS:
      contentTools(chunked);
      break;
    case PAGE_WAIT:
      contentWait(chunked);
      break;
    default:
      break;
  }

  if (reqPage == PAGE_IP || reqPage == PAGE_TCP || reqPage == PAGE_RTU) {
    chunked.print(F("<p><div class=q><label><input type=submit value='Save & Apply'></label><input type=reset value=Cancel></div>"));
  }
  chunked.print(F("</form>"
                  "</main>"));
  tagDivClose(chunked);  // close tags <div class=c> <div class=w>
  chunked.end();         // closing tags not required </body></html>
}


/**************************************************************************/
/*!
  @brief System Info

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentInfo(ChunkedPrint &chunked) {
  tagLabelDiv(chunked, F("SW Version"));
  chunked.print(VERSION[0]);
  chunked.print(F("."));
  chunked.print(VERSION[1]);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Board"));
  chunked.print(BOARD);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("EEPROM Health"));
  chunked.print(data.eepromWrites);
  chunked.print(F(" Write Cycles"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Ethernet Chip"));
  switch (W5100.getChip()) {
    case 51:
      chunked.print(F("W5100"));
      break;
    case 52:
      chunked.print(F("W5200"));
      break;
    case 55:
      chunked.print(F("W5500"));
      break;
    default:  // TODO: add W6100 once it is included in Ethernet library
      chunked.print(F("Unknown"));
      break;
  }
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Ethernet Sockets"));
  chunked.print(maxSockNum);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("MAC Address"));
  for (byte i = 0; i < 6; i++) {
    chunked.print(hex(data.mac[i]));
    if (i < 5) chunked.print(F(":"));
  }
  tagDivClose(chunked);

#ifdef ENABLE_DHCP
  tagLabelDiv(chunked, F("DHCP Status"));
  if (!data.config.enableDhcp) {
    chunked.print(F("Disabled"));
  } else if (dhcpSuccess == true) {
    chunked.print(F("Success"));
  } else {
    chunked.print(F("Failed, using fallback static IP"));
  }
  tagDivClose(chunked);
#endif /* ENABLE_DHCP */

  tagLabelDiv(chunked, F("IP Address"));
  chunked.print(IPAddress(Ethernet.localIP()));
  tagDivClose(chunked);
}

/**************************************************************************/
/*!
  @brief P1P2 Status

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentStatus(ChunkedPrint &chunked) {

#ifdef ENABLE_EXTENDED_WEBUI
  tagLabelDiv(chunked, F("Run Time"));
  tagSpan(chunked, JSON_TIME);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("RTU Data"));
  tagSpan(chunked, JSON_RTU_DATA);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Ethernet Data"));
  tagSpan(chunked, JSON_ETH_DATA);
  tagDivClose(chunked);
#endif /* ENABLE_EXTENDED_WEBUI */

  tagLabelDiv(chunked, F("Modbus RTU Request"));
  for (byte i = 0; i <= POST_REQ_LAST - POST_REQ; i++) {
    bool required = false;
    bool printVal = false;
    byte value = 0;
    if (i == 0 || i == 1) {
      required = true;  // first byte (slave address) and second byte (function code) are required
    }
    if (i < requestLen) {
      printVal = true;
      value = request[i];
    }
    tagInputHex(chunked, POST_REQ + i, required, printVal, value);
  }
  chunked.print(F("h (without CRC) <input type=submit value=Send>"));
  tagButton(chunked, F("Clear"), ACT_CLEAR_REQUEST);
  tagDivClose(chunked);
  chunked.print(F("</form><form method=post>"));
  tagLabelDiv(chunked, F("Modbus RTU Response"));
  tagSpan(chunked, JSON_RESPONSE);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Requests Queue"), true);
  tagSpan(chunked, JSON_QUEUE);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Modbus Statistics"));
  tagButton(chunked, F("Reset Stats"), ACT_RESET_STATS);
  tagDivClose(chunked);
  tagLabelDiv(chunked, 0);
  tagSpan(chunked, JSON_STATS);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Modbus Masters"), true);
  tagSpan(chunked, JSON_TCP_UDP_MASTERS);
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Modbus Slaves"));
  tagButton(chunked, F("Scan Slaves"), ACT_SCAN);
  tagDivClose(chunked);
  tagLabelDiv(chunked, 0);
  tagSpan(chunked, JSON_SLAVES);
  tagDivClose(chunked);
}

/**************************************************************************/
/*!
  @brief IP Settings

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentIp(ChunkedPrint &chunked) {

  tagLabelDiv(chunked, F("MAC Address"));
  for (byte i = 0; i < 6; i++) {
    tagInputHex(chunked, POST_MAC + i, true, true, data.mac[i]);
    if (i < 5) chunked.print(F(":"));
  }
  tagButton(chunked, F("Randomize"), ACT_MAC);
  tagDivClose(chunked);

#ifdef ENABLE_DHCP
  tagLabelDiv(chunked, F("Auto IP"));
  chunked.print(F("<input type=hidden name="));
  chunked.print(POST_DHCP, HEX);
  chunked.print(F(" value=0>"
                  "<input type=checkbox id=o name="));
  chunked.print(POST_DHCP, HEX);
  chunked.print(F(" onclick=g(this.checked) value=1"));
  if (data.config.enableDhcp) chunked.print(F(" checked"));
  chunked.print(F("> DHCP"));
  tagDivClose(chunked);
#endif /* ENABLE_DHCP */

  byte *tempIp;
  for (byte j = 0; j < 3; j++) {
    switch (j) {
      case 0:
        tagLabelDiv(chunked, F("Static IP"));
        tempIp = data.config.ip;
        break;
      case 1:
        tagLabelDiv(chunked, F("Submask"));
        tempIp = data.config.subnet;
        break;
      case 2:
        tagLabelDiv(chunked, F("Gateway"));
        tempIp = data.config.gateway;
        break;
      default:
        break;
    }
    tagInputIp(chunked, POST_IP + (j * 4), tempIp);
    tagDivClose(chunked);
  }
#ifdef ENABLE_DHCP
  tagLabelDiv(chunked, F("DNS Server"));
  tagInputIp(chunked, POST_DNS, data.config.dns);
  tagDivClose(chunked);
#endif /* ENABLE_DHCP */
}

/**************************************************************************/
/*!
  @brief TCP/UDP Settings

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentTcp(ChunkedPrint &chunked) {
  uint16_t value;
  for (byte i = 0; i < 3; i++) {
    switch (i) {
      case 0:
        tagLabelDiv(chunked, F("Modbus TCP Port"));
        value = data.config.tcpPort;
        break;
      case 1:
        tagLabelDiv(chunked, F("Modbus UDP Port"));
        value = data.config.udpPort;
        break;
      case 2:
        tagLabelDiv(chunked, F("WebUI Port"));
        value = data.config.webPort;
        break;
      default:
        break;
    }
    tagInputNumber(chunked, POST_TCP + i, 1, 65535, value, F(""));
    tagDivClose(chunked);
  }
  tagLabelDiv(chunked, F("Modbus Mode"));
  chunked.print(F("<select name="));
  chunked.print(POST_RTU_OVER, HEX);
  chunked.print(F(">"));
  for (byte i = 0; i < 2; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if (data.config.enableRtuOverTcp == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    switch (i) {
      case 0:
        chunked.print(F("Modbus TCP/UDP"));
        break;
      case 1:
        chunked.print(F("Modbus RTU over TCP/UDP"));
        break;
      default:
        break;
    }
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select>"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Modbus TCP Idle Timeout"));
  tagInputNumber(chunked, POST_TCP_TIMEOUT, 1, 3600, data.config.tcpTimeout, F("sec"));
  tagDivClose(chunked);
}

/**************************************************************************/
/*!
  @brief RTU Settings

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentRtu(ChunkedPrint &chunked) {
  tagLabelDiv(chunked, F("Baud Rate"));
  chunked.print(F("<select class=s name="));
  chunked.print(POST_BAUD, HEX);
  chunked.print(F(">"));
  for (byte i = 0; i < (sizeof(BAUD_RATES) / 2); i++) {
    chunked.print(F("<option value="));
    chunked.print(BAUD_RATES[i]);
    if (data.config.baud == BAUD_RATES[i]) chunked.print(F(" selected"));
    chunked.print(F(">"));
    chunked.print(BAUD_RATES[i]);
    chunked.print(F("00</option>"));
  }
  chunked.print(F("</select> bps"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Data Bits"));
  chunked.print(F("<select name="));
  chunked.print(POST_DATA, HEX);
  chunked.print(F(">"));
  for (byte i = 5; i <= 8; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if ((((data.config.serialConfig & 0x06) >> 1) + 5) == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    chunked.print(i);
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select> bit"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Parity"));
  chunked.print(F("<select name="));
  chunked.print(POST_PARITY, HEX);
  chunked.print(F(">"));
  for (byte i = 0; i <= 3; i++) {
    if (i == 1) continue;  // invalid value, skip and continue for loop
    chunked.print(F("<option value="));
    chunked.print(i);
    if (((data.config.serialConfig & 0x30) >> 4) == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    switch (i) {
      case 0:
        chunked.print(F("None"));
        break;
      case 2:
        chunked.print(F("Even"));
        break;
      case 3:
        chunked.print(F("Odd"));
        break;
      default:
        break;
    }
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select>"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Stop Bits"));
  chunked.print(F("<select name="));
  chunked.print(POST_STOP, HEX);
  chunked.print(F(">"));
  for (byte i = 1; i <= 2; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if ((((data.config.serialConfig & 0x08) >> 3) + 1) == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    chunked.print(i);
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select> bit"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Inter-frame Delay"));
  tagInputNumber(chunked, POST_FRAMEDELAY, byte(frameDelay() / 1000UL) + 1, 250, data.config.frameDelay, F("ms"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Response Timeout"));
  tagInputNumber(chunked, POST_TIMEOUT, 50, 5000, data.config.serialTimeout, F("ms"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, F("Attempts"));
  tagInputNumber(chunked, POST_ATTEMPTS, 1, 5, data.config.serialAttempts, F(""));
  tagDivClose(chunked);
}

/**************************************************************************/
/*!
  @brief Tools

  @param chunked Chunked buffer
*/
/**************************************************************************/
void contentTools(ChunkedPrint &chunked) {
  tagLabelDiv(chunked, 0);
  tagButton(chunked, F("Load Default Settings"), ACT_DEFAULT);
  chunked.print(F(" (static IP: "));
  chunked.print(IPAddress(DEFAULT_CONFIG.ip));
  chunked.print(F(")"));
  tagDivClose(chunked);
  tagLabelDiv(chunked, 0);
  tagButton(chunked, F("Reboot"), ACT_REBOOT);
  tagDivClose(chunked);
}


void contentWait(ChunkedPrint &chunked) {
  tagLabelDiv(chunked, 0);
  chunked.print(F("Reloading. Please wait..."));
  tagDivClose(chunked);
}

/**************************************************************************/
/*!
  @brief <input type=number>

  @param chunked Chunked buffer
  @param name Name POST_
  @param min Minimum value
  @param max Maximum value
  @param value Current value
  @param units Units (string)
*/
/**************************************************************************/
void tagInputNumber(ChunkedPrint &chunked, const byte name, const byte min, uint16_t max, uint16_t value, const __FlashStringHelper *units) {
  chunked.print(F("<input class='s n' required type=number name="));
  chunked.print(name, HEX);
  chunked.print(F(" min="));
  chunked.print(min);
  chunked.print(F(" max="));
  chunked.print(max);
  chunked.print(F(" value="));
  chunked.print(value);
  chunked.print(F("> ("));
  chunked.print(min);
  chunked.print(F("~"));
  chunked.print(max);
  chunked.print(F(") "));
  chunked.print(units);
}

/**************************************************************************/
/*!
  @brief <input>
  IP address (4 elements)

  @param chunked Chunked buffer
  @param name Name POST_
  @param ip IP address from data.config
*/
/**************************************************************************/
void tagInputIp(ChunkedPrint &chunked, const byte name, byte ip[]) {
  for (byte i = 0; i < 4; i++) {
    chunked.print(F("<input name="));
    chunked.print(name + i, HEX);
    chunked.print(F(" class='p i' required maxlength=3 pattern='^(&bsol;d{1,2}|1&bsol;d&bsol;d|2[0-4]&bsol;d|25[0-5])$' value="));
    chunked.print(ip[i]);
    chunked.print(F(">"));
    if (i < 3) chunked.print(F("."));
  }
}

/**************************************************************************/
/*!
  @brief <input>
  HEX string (2 chars)

  @param chunked Chunked buffer
  @param name Name POST_
  @param required True if input is required
  @param printVal True if value is shown
  @param value Value
*/
/**************************************************************************/
void tagInputHex(ChunkedPrint &chunked, const byte name, const bool required, const bool printVal, const byte value) {
  chunked.print(F("<input name="));
  chunked.print(name, HEX);
  if (required) {
    chunked.print(F(" required"));
  }
  chunked.print(F(" minlength=2 maxlength=2 class=i pattern='[a-fA-F&bsol;d]+' value='"));
  if (printVal) {
    chunked.print(hex(value));
  }
  chunked.print(F("'>"));
}

/**************************************************************************/
/*!
  @brief <label><div>

  @param chunked Chunked buffer
  @param label Label string
  @param top Align to top
*/
/**************************************************************************/
void tagLabelDiv(ChunkedPrint &chunked, const __FlashStringHelper *label) {
  tagLabelDiv(chunked, label, false);
}
void tagLabelDiv(ChunkedPrint &chunked, const __FlashStringHelper *label, bool top) {
  chunked.print(F("<div class='q"));
  if (!top) chunked.print(F(" r"));
  chunked.print(F("'><label> "));
  if (label) {
    chunked.print(label);
    chunked.print(F(":"));
  }
  chunked.print(F("</label><div>"));
}

/**************************************************************************/
/*!
  @brief <button>

  @param chunked Chunked buffer
  @param flashString Button string
  @param value Value to be sent via POST
*/
/**************************************************************************/
void tagButton(ChunkedPrint &chunked, const __FlashStringHelper *flashString, byte value) {
  chunked.print(F(" <button name="));
  chunked.print(POST_ACTION, HEX);
  chunked.print(F(" value="));
  chunked.print(value);
  chunked.print(F(">"));
  chunked.print(flashString);
  chunked.print(F("</button>"));
}

/**************************************************************************/
/*!
  @brief </div>

  @param chunked Chunked buffer
*/
/**************************************************************************/
void tagDivClose(ChunkedPrint &chunked) {
  chunked.print(F("</div>"
                  "</div>"));  // <div class=q>
}

/**************************************************************************/
/*!
  @brief <span>

  @param chunked Chunked buffer
  @param JSONKEY JSON_ id
*/
/**************************************************************************/
void tagSpan(ChunkedPrint &chunked, const byte JSONKEY) {
  chunked.print(F("<span id="));
  chunked.print(JSONKEY);
  chunked.print(F(">"));
  jsonVal(chunked, JSONKEY);
  chunked.print(F("</span>"));
}

/**************************************************************************/
/*!
  @brief Menu item strings

  @param chunked Chunked buffer
  @param item Page number
*/
/**************************************************************************/
void stringPageName(ChunkedPrint &chunked, byte item) {
  switch (item) {
    case PAGE_INFO:
      chunked.print(F("System Info"));
      break;
    case PAGE_STATUS:
      chunked.print(F("Modbus Status"));
      break;
    case PAGE_IP:
      chunked.print(F("IP Settings"));
      break;
    case PAGE_TCP:
      chunked.print(F("TCP/UDP Settings"));
      break;
    case PAGE_RTU:
      chunked.print(F("RTU Settings"));
      break;
    case PAGE_TOOLS:
      chunked.print(F("Tools"));
      break;
    default:
      break;
  }
}

void stringStats(ChunkedPrint &chunked, const byte stat) {
  switch (stat) {
    case SLAVE_OK:
      chunked.print(F(" Slave Responded"));
      break;
    case SLAVE_ERROR_0X:
      chunked.print(F(" Slave Responded with Error (Codes 1~8)"));
      break;
    case SLAVE_ERROR_0A:
      chunked.print(F(" Gateway Overloaded (Code 10)"));
      break;
    case SLAVE_ERROR_0B:
    case SLAVE_ERROR_0B_QUEUE:
      chunked.print(F(" Slave Failed to Respond (Code 11)"));
      break;
    case ERROR_TIMEOUT:
      chunked.print(F(" Response Timeout"));
      break;
    case ERROR_RTU:
      chunked.print(F(" Invalid RTU Response"));
      break;
    case ERROR_TCP:
      chunked.print(F(" Invalid TCP/UDP Request"));
      break;
    default:
      break;
  }
  chunked.print(F("<br>"));
}

/**************************************************************************/
/*!
  @brief Provide JSON value to a corresponding JSON key. The value is printed
  in <span> and in JSON document fetched on the background.
  @param chunked Chunked buffer
  @param JSONKEY JSON key
*/
/**************************************************************************/
void jsonVal(ChunkedPrint &chunked, const byte JSONKEY) {
  switch (JSONKEY) {
#ifdef ENABLE_EXTENDED_WEBUI
    case JSON_TIME:
      chunked.print(seconds / (3600UL * 24L));
      chunked.print(F(" days, "));
      chunked.print((seconds / 3600UL) % 24L);
      chunked.print(F(" hours, "));
      chunked.print((seconds / 60UL) % 60L);
      chunked.print(F(" mins, "));
      chunked.print((seconds) % 60L);
      chunked.print(F(" secs"));
      break;
    case JSON_RTU_DATA:
      for (byte i = 0; i < DATA_LAST; i++) {
        chunked.print(data.rtuCnt[i]);
        switch (i) {
          case DATA_TX:
            chunked.print(F(" Tx bytes / "));
            break;
          case DATA_RX:
            chunked.print(F(" Rx bytes"));
            break;
        }
      }
      break;
    case JSON_ETH_DATA:
      for (byte i = 0; i < DATA_LAST; i++) {
        chunked.print(data.ethCnt[i]);
        switch (i) {
          case DATA_TX:
            chunked.print(F(" Tx bytes / "));
            break;
          case DATA_RX:
            chunked.print(F(" Rx bytes (excl. WebUI)"));
            break;
        }
      }
      break;
#endif /* ENABLE_EXTENDED_WEBUI */
    case JSON_RESPONSE:
      {
        for (byte i = 0; i < MAX_RESPONSE_LEN; i++) {
          chunked.print(F("<input value='"));
          if (i < responseLen) {
            chunked.print(hex(response[i]));
          }
          chunked.print(F("' disabled class=i>"));
        }
        chunked.print(F("h"));
        if (responseLen > MAX_RESPONSE_LEN) {
          chunked.print(F(" +"));
          chunked.print(byte(responseLen - MAX_RESPONSE_LEN));
          chunked.print(F(" bytes"));
        }
      }
      break;
    case JSON_QUEUE:
      chunked.print(queueDataSize);
      chunked.print(F(" / "));
      chunked.print(MAX_QUEUE_DATA);
      chunked.print(F(" bytes<br>"));
      chunked.print(queueHeadersSize);
      chunked.print(F(" / "));
      chunked.print(MAX_QUEUE_REQUESTS);
      chunked.print(F(" requests"));
      queueDataSize = queueData.size();
      queueHeadersSize = queueHeaders.size();
      break;
    case JSON_STATS:
      for (byte i = 0; i < ERROR_LAST; i++) {
        if (i == SLAVE_ERROR_0B_QUEUE) continue;  // SLAVE_ERROR_0B_QUEUE is not shown in web UI
        chunked.print(data.errorCnt[i]);
        stringStats(chunked, i);
      }
      break;
    case JSON_TCP_UDP_MASTERS:
      {
        for (byte s = 0; s < maxSockNum; s++) {
          byte remoteIParray[4];
          W5100.readSnDIPR(s, remoteIParray);
          if (remoteIParray[0] != 0) {
            if (W5100.readSnSR(s) == SnSR::UDP) {
              chunked.print(IPAddress(remoteIParray));
              chunked.print(F(" UDP<br>"));
            } else if (W5100.readSnSR(s) == SnSR::ESTABLISHED && W5100.readSnPORT(s) == data.config.tcpPort) {
              chunked.print(IPAddress(remoteIParray));
              chunked.print(F(" TCP<br>"));
            }
          }
        }
      }
      break;
    case JSON_SLAVES:
      {
        for (byte k = 1; k < MAX_SLAVES; k++) {
          for (byte s = 0; s <= SLAVE_ERROR_0B_QUEUE; s++) {
            if (getSlaveStatus(k, s) == true || k == scanCounter) {
              chunked.print(hex(k));
              chunked.print(F("h"));
              if (k == scanCounter) {
                chunked.print(F(" Scanning...<br>"));
                break;
              }
              stringStats(chunked, s);
            }
          }
        }
      }
      break;
    default:
      break;
  }
}
