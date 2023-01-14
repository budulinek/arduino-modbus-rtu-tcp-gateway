/* *******************************************************************
   Pages for Webserver

   sendPage
   - displays main page, renders title and left menu using tables
   - calls content functions depending on the number (i.e. URL) of the requested web page
   - also displays buttons for some of the pages
   - in order to save flash memory, some HTML closing tags are omitted, new lines in HTML code are also omitted

   menuItem
   - returns menu item string depending on the the number (i.e. URL) of the requested web page

   contentInfo, contentStatus, contentIp, contentTcp, contentRtu
   - render the content of the requested page

   contentWait
   - renders the "please wait" message instead of the content (= request page number 0xFF, will be forwarded to home page after 5 seconds)

   helperInput
   helperStats
   - renders some repetitive HTML code for inputs

   send404, send204
   - send error messages

   ***************************************************************** */

const byte WEB_OUT_BUFFER_SIZE = 128;  // size of web server write buffer (used by StreamLib)

void sendPage(EthernetClient &client, byte reqPage) {
  char webOutBuffer[WEB_OUT_BUFFER_SIZE];
  ChunkedPrint chunked(client, webOutBuffer, sizeof(webOutBuffer));  // the StreamLib object to replace client print
  chunked.print(F("HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"));
  chunked.begin();
  chunked.print(F("<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta"));
  if (reqPage == PAGE_WAIT) {  // redirect to new IP and web port
    chunked.print(F(" http-equiv=refresh content=5;url=http://"));
    chunked.print(IPAddress(localConfig.ip));
    chunked.print(F(":"));
    chunked.print(localConfig.webPort);
  }
  chunked.print(F(">"
                  "<title>Modbus RTU &rArr; Modbus TCP/UDP Gateway</title>"
                  "<style>"
                  "html,body{margin:0;height:100%;font-family:sans-serif}"
                  "a{text-decoration:none;color:white}"
                  "table{width:100%;height:100%}"
                  "th{height:0;text-align:left;background-color:#0067AC;color:white;padding:10px}"
                  "td:first-child{text-align:right;width:30%}"
                  "#x td:nth-child(2){width:1px;white-space:nowrap}"
                  "h1{margin:0}"
                  "#x{padding:0;display:block;height:calc(100vh - 70px);overflow-y:auto}"
                  "</style>"
                  "</head>"
                  "<body"));
#ifdef ENABLE_DHCP
  chunked.print(F(" onload='dis(document.getElementById(&quot;box&quot;).checked)'>"
                  "<script>function dis(st) {var x = document.getElementsByClassName('ip');for (var i = 0; i < x.length; i++) {x[i].disabled = st}}</script"));
#endif /* ENABLE_DHCP */
  if (reqPage == PAGE_STATUS) {
    chunked.print(F("><script>"
                    "const renew=()=>{"
                    "fetch('data.json')"  // Call the fetch function passing the url of the API as a parameter
                    ".then(resp=>{return resp.json();})"
                    ".then(jo=>{"
                    "for(var i in jo){if(document.getElementById(i))document.getElementById(i).innerHTML=jo[i];}});"
                    "};"
                    "setInterval(()=>renew(),"));
    chunked.print(FETCH_INTERVAL);
    chunked.print(F(");"
                    "</script"));
  }
  chunked.print(F(">"
                  "<table>"
                  "<tr><th colspan=2>"
                  "<h1>Modbus RTU &rArr; Modbus TCP/UDP Gateway</h1>"  // first row is header
                  "<tr>"                                               // second row is left menu (first cell) and main page (second cell)
                  "<th style=width:20%;padding:0>"

                  // Left Menu
                  "<table>"));
  for (byte i = 1; i <= PAGE_RTU; i++) {  // RS485 Settings are the last item in the left menu
    chunked.print(F("<tr><th"));
    if ((i) == reqPage) {
      chunked.print(F(" style=background-color:#FF6600"));
    }
    chunked.print(F("><a href="));
    chunked.print(i);
    chunked.print(F(".htm>"));
    menuItem(chunked, i);
    chunked.print(F("</a>"));
  }
  chunked.print(F("<tr><td></table><td id=x>"
                  "<form action=/"));  // Main Page
  chunked.print(reqPage);
  chunked.print(F(".htm method=post>"
                  "<table style=border-collapse:collapse>"
                  "<tr><th><th colspan=2>"));
  menuItem(chunked, reqPage);
  chunked.print(F("<tr><td><br>"));

  //   PLACE FUNCTIONS PROVIDING CONTENT HERE
  // content should start with new table row <tr>

  switch (reqPage) {
    case PAGE_NONE:
      break;
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
    case PAGE_WAIT:
      contentWait(chunked);
      break;
    default:
      break;
  }
  if (reqPage == PAGE_IP || reqPage == PAGE_TCP || reqPage == PAGE_RTU) {
    chunked.print(F("<tr><td><br><input type=submit value='Save & Apply'><td><br><input type=reset value=Cancel>"));
  }
  chunked.print(F("</table></form>"
                  "</table></body></html>"));
  chunked.end();
}

// Menu item strings
void menuItem(ChunkedPrint &chunked, byte item) {
  switch (item) {
    case PAGE_NONE:
      break;
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
      chunked.print(F("RS485 Settings"));
      break;
    default:
      break;
  }
}

//        System Info
void contentInfo(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td>SW Version:<td>"));
  chunked.print(VERSION[0]);
  chunked.print(F("."));
  chunked.print(VERSION[1]);
  chunked.print(F("<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(FACTORY);
  chunked.print(F(">Load Default Settings</button>"  //  chunked.print(IPAddress(DEFAULT_CONFIG.ip));
                  "<tr><td>Microcontroller:<td>"));
  chunked.print(BOARD);
  chunked.print(F("<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(REBOOT);
  chunked.print(F(">Reboot</button>"
                  "<tr><td>Ethernet Chip:<td>"));
  switch (Ethernet.hardwareStatus()) {
    case EthernetW5100:
      chunked.print(F("W5100 (4"));
      break;
    case EthernetW5200:
      chunked.print(F("W5200 (8"));
      break;
    case EthernetW5500:
      chunked.print(F("W5500 (8"));
      break;
    default:  // TODO: add W6100 once it is included in Ethernet library
      chunked.print(F("Unknown (8"));
      break;
  }
  chunked.print(F(" sockets)<tr><td>MAC Address:<td>"));
  byte macBuffer[6];
  Ethernet.MACAddress(macBuffer);
  for (byte i = 0; i < 6; i++) {
    if (macBuffer[i] < 16) chunked.print(F("0"));
    chunked.print(macBuffer[i], HEX);
    if (i < 5) chunked.print(F(":"));
  }
  chunked.print(F("<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(MAC);
  chunked.print(F(">Generate New MAC</button>"));

#ifdef ENABLE_DHCP
  chunked.print(F("<tr><td>Auto IP:<td>"));
  if (!extraConfig.enableDhcp) {
    chunked.print(F("DHCP disabled"));
  } else if (dhcpSuccess == true) {
    chunked.print(F("DHCP successful"));
  } else {
    chunked.print(F("DHCP failed, using fallback static IP"));
  }
#endif /* ENABLE_DHCP */

  chunked.print(F("<tr><td>IP Address:<td>"));
  chunked.print(IPAddress(Ethernet.localIP()));
}

//        Modbus Status
void contentStatus(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td>Run Time:<td>"));
  helperFetch(chunked, JSON_DAYS);
  chunked.print(F(" days, "));
  helperFetch(chunked, JSON_HOURS);
  chunked.print(F(" hours, "));
  helperFetch(chunked, JSON_MINS);
  chunked.print(F(" mins, "));
  helperFetch(chunked, JSON_SECS);
  chunked.print(F(" secs"));

#ifdef ENABLE_EXTRA_DIAG
  chunked.print(F("<tr><td>RS485 Data:<td>"));
  helperFetch(chunked, JSON_RS485_TX);
  chunked.print(F(" Tx bytes / "));
  helperFetch(chunked, JSON_RS485_RX);
  chunked.print(F(" Rx bytes"));
  chunked.print(F("<tr><td>Ethernet Data:<td>"));
  helperFetch(chunked, JSON_ETH_TX);
  chunked.print(F(" Tx bytes / "));
  helperFetch(chunked, JSON_ETH_RX);
  chunked.print(F(" Rx bytes  (excl. WebUI)"));
#endif /* ENABLE_EXTRA_DIAG */

  chunked.print(F("<tr><td>Modbus Statistics:<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(RST_STATS);
  chunked.print(F(">Reset Stats</button>"
                  "<tr><td><td>"));
  for (byte i = 0; i < 4; i++) {  // only first four Modbus status counters are displayed (there is no counter for STAT_ERROR_0B_QUEUE)
    helperFetch(chunked, JSON_ERROR + i);
    helperStats(chunked, i);
    chunked.print(F("<tr><td><td>"));
  }
  helperFetch(chunked, JSON_ERROR_TCP);
  chunked.print(F(" Invalid TCP/UDP Request"
                  "<tr><td><td>"));
  helperFetch(chunked, JSON_ERROR_RTU);
  chunked.print(F(" Invalid RS485 Response"
                  "<tr><td><td>"));
  helperFetch(chunked, JSON_ERROR_TIMEOUT);
  chunked.print(F(" Response Timeout"
                  "<tr><td>Requests Queue:<td>"));
  helperFetch(chunked, JSON_QUEUE_DATA);
  chunked.print(F(" / "));
  chunked.print(MAX_QUEUE_DATA);
  chunked.print(F(" bytes"
                  "<tr><td><td>"));
  helperFetch(chunked, JSON_QUEUE_REQUESTS);
  chunked.print(F(" / "));
  chunked.print(MAX_QUEUE_REQUESTS);
  chunked.print(F(" requests"));
  chunked.print(F("<tr><td>Modbus TCP/UDP Masters:"
                  "<tr><td><td>"));
  helperFetch(chunked, JSON_MASTERS);
  chunked.print(F("<tr><td>Modbus RTU Slaves:<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(SCAN);
  chunked.print(F(">Scan Slaves</button>"
                  "<tr><td><td>"));
  helperFetch(chunked, JSON_SLAVES);
}

//            IP Settings
void contentIp(ChunkedPrint &chunked) {

#ifdef ENABLE_DHCP
  chunked.print(F("<tr><td>Auto IP:"
                  "<td><input type=hidden name="));
  chunked.print(POST_DHCP);
  chunked.print(F(" value=0>"
                  "<input type=checkbox id=box name="));
  chunked.print(POST_DHCP);
  chunked.print(F(" onclick=dis(this.checked) value=1"));
  if (extraConfig.enableDhcp) chunked.print(F(" checked"));
  chunked.print(F(">Enable DHCP"));
#endif /* ENABLE_DHCP */

  for (byte j = 0; j < 3; j++) {
    chunked.print(F("<tr><td>"));
    switch (j) {
      case 0:
        chunked.print(F("Static IP"));
        break;
      case 1:
        chunked.print(F("Submask"));
        break;
      case 2:
        chunked.print(F("Gateway"));
        break;
      default:
        break;
    }
    chunked.print(F(":<td>"));
    for (byte i = 0; i < 4; i++) {
      chunked.print(F("<input name="));
      chunked.print(POST_IP + i + (j * 4));
      chunked.print(F(" type=tel class=ip required maxlength=3 size=3 pattern='^(&bsol;d{1,2}|1&bsol;d&bsol;d|2[0-4]&bsol;d|25[0-5])$' value="));
      switch (j) {
        case 0:
          chunked.print(localConfig.ip[i]);
          break;
        case 1:
          chunked.print(localConfig.subnet[i]);
          break;
        case 2:
          chunked.print(localConfig.gateway[i]);
          break;
        default:
          break;
      }
      chunked.print(F(">"));
      if (i < 3) chunked.print(F("."));
    }
  }
#ifdef ENABLE_DHCP
  chunked.print(F("<tr><td>"));
  chunked.print(F("DNS Server"));
  chunked.print(F(":<td>"));
  for (byte i = 0; i < 4; i++) {
    chunked.print(F("<input name="));
    chunked.print(POST_DNS + i);
    chunked.print(F(" type=tel class=ip required maxlength=3 size=3 pattern='^(&bsol;d{1,2}|1&bsol;d&bsol;d|2[0-4]&bsol;d|25[0-5])$' value="));
    chunked.print(extraConfig.dns[i]);
    chunked.print(F(">"));
    if (i < 3) chunked.print(F("."));
  }
#endif /* ENABLE_DHCP */
}

//            TCP/UDP Settings
void contentTcp(ChunkedPrint &chunked) {
  for (byte i = 0; i < 3; i++) {
    chunked.print(F("<tr><td>"));
    switch (i) {
      case 0:
        chunked.print(F("Modbus TCP Port:"));
        break;
      case 1:
        chunked.print(F("Modbus UDP Port:"));
        break;
      case 2:
        chunked.print(F("WebUI Port:"));
        break;
      default:
        break;
    }
    helperInput(chunked);
    chunked.print(POST_TCP + i);
    chunked.print(F(" min=1 max=65535 value="));
    switch (i) {
      case 0:
        chunked.print(localConfig.tcpPort);
        break;
      case 1:
        chunked.print(localConfig.udpPort);
        break;
      case 2:
        chunked.print(localConfig.webPort);
        break;
      default:
        break;
    }
    chunked.print(F(">"));
  }
  chunked.print(F("<tr><td>Modbus Mode:<td><select name="));
  chunked.print(POST_RTU_OVER);
  chunked.print(F(">"));
  for (byte i = 0; i < 2; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if (localConfig.enableRtuOverTcp == i) chunked.print(F(" selected"));
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
}

//            RS485 Settings
void contentRtu(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td>Baud Rate:"));
  helperInput(chunked);
  chunked.print(POST_BAUD);
  chunked.print(F(" min=300 max=250000 value="));
  chunked.print(localConfig.baud);
  chunked.print(F("> (300~250000) bps"
                  "<tr><td>Data Bits:<td><select name="));
  chunked.print(POST_DATA);
  chunked.print(F(">"));
  for (byte i = 5; i <= 8; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if ((((localConfig.serialConfig & 0x06) >> 1) + 5) == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    chunked.print(i);
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select> bit"
                  "<tr><td>Parity:<td>"
                  "<select name="));
  chunked.print(POST_PARITY);
  chunked.print(F(">"));
  for (byte i = 0; i <= 3; i++) {
    if (i == 1) continue;  // invalid value, skip and continue for loop
    chunked.print(F("<option value="));
    chunked.print(i);
    if (((localConfig.serialConfig & 0x30) >> 4) == i) chunked.print(F(" selected"));
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
  chunked.print(F("</select>"
                  "<tr><td>Stop Bits:<td><select name="));
  chunked.print(POST_STOP);
  chunked.print(F(">"));
  for (byte i = 1; i <= 2; i++) {
    chunked.print(F("<option value="));
    chunked.print(i);
    if ((((localConfig.serialConfig & 0x08) >> 3) + 1) == i) chunked.print(F(" selected"));
    chunked.print(F(">"));
    chunked.print(i);
    chunked.print(F("</option>"));
  }
  chunked.print(F("</select> bit"
                  "<tr><td>Inter-frame Delay:"));
  helperInput(chunked);
  chunked.print(POST_FRAMEDELAY);
  chunked.print(F(" min="));
  byte minFrameDelay = (byte)(frameDelay() / 1000UL) + 1;
  chunked.print(minFrameDelay);
  chunked.print(F(" max=250 value="));
  chunked.print(localConfig.frameDelay);
  chunked.print(F("> ("));
  chunked.print(minFrameDelay);
  chunked.print(F("~250) ms"
                  "<tr><td>Response Timeout:"));
  helperInput(chunked);
  chunked.print(POST_TIMEOUT);
  chunked.print(F(" min=50 max=2000 value="));
  chunked.print(localConfig.serialTimeout);
  chunked.print(F("> (50~2000) ms"
                  "<tr><td>Attempts:"));
  helperInput(chunked);
  chunked.print(POST_ATTEMPTS);
  chunked.print(F(" min=1 max=5 value="));
  chunked.print(localConfig.serialAttempts);
  chunked.print(F("> (1~5)"));
}


void contentWait(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td><td><br>Reloading. Please wait..."));
}

// Functions providing snippets of repetitive HTML code
void helperInput(ChunkedPrint &chunked) {
  chunked.print(F("<td><input size=7 required type=number name="));
}

void helperStats(ChunkedPrint &chunked, const byte stat) {
  switch (stat) {
    case STAT_OK:
      chunked.print(F(" Slave Responded"));
      break;
    case STAT_ERROR_0X:
      chunked.print(F(" Slave Responded with Error (Codes 1~8)"));
      break;
    case STAT_ERROR_0A:
      chunked.print(F(" Gateway Overloaded (Code 10)"));
      break;
    case STAT_ERROR_0B:
    case STAT_ERROR_0B_QUEUE:
      chunked.print(F(" Slave Failed to Respond (Code 11)"));
      break;
    default:
      break;
  }
}

void helperFetch(ChunkedPrint &chunked, const byte JSONKEY) {
  chunked.print(F("<span id="));
  chunked.print(JSONKEY);
  chunked.print(F(">"));
  jsonVal(chunked, JSONKEY);
  chunked.print(F("</span>"));
}

void send404(EthernetClient &client) {
  client.println(F("HTTP/1.1 404 Not Found\r\n"
                   "Content-Length: 0"));
  client.stop();
}

void send204(EthernetClient &client) {
  client.println(F("HTTP/1.1 204 No content"));
  client.stop();
}

void sendJson(EthernetClient &client) {
  char webOutBuffer[WEB_OUT_BUFFER_SIZE];
  ChunkedPrint chunked(client, webOutBuffer, sizeof(webOutBuffer));  // the StreamLib object to replace client print
  chunked.print(F("HTTP/1.1 200\r\n"
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
}


void jsonVal(ChunkedPrint &chunked, const byte JSONKEY) {
  unsigned long temp;
  switch (JSONKEY) {
    case JSON_MASTERS:
      {
        bool masters = false;
        if (Udp.remoteIP()) {
          chunked.print(IPAddress(Udp.remoteIP()));
          chunked.print(F(" UDP Master"));
          masters = true;
        }
        for (byte i = 1; i < maxSockNum; i++) {  // socket 0 is always UDP so we can start the for loop with socket 1
          EthernetClient client = EthernetClient(i);
          if (client.remoteIP() && client.localPort() == localConfig.tcpPort) {
            if (masters) chunked.print(F("<br>"));
            chunked.print(IPAddress(client.remoteIP()));
            chunked.print(F(" TCP Master"));
            masters = true;
          }
        }
      }
      return;
    case JSON_SLAVES:
      {
        bool slaves = false;
        for (int k = 1; k < MAX_SLAVES; k++) {
          for (int s = 0; s < STAT_NUM; s++) {
            if (getSlaveStatus(k, s) == true || k == scanCounter) {
              if (slaves) chunked.print(F("<br>"));
              slaves = true;
              chunked.print(F("0x"));
              if (k < 16) chunked.print(F("0"));
              chunked.print(k, HEX);
              if (k == scanCounter) {
                chunked.print(F(" Scanning..."));
                break;
              }
              helperStats(chunked, s);
            }
          }
        }
      }
      return;
    case JSON_SECS:
      temp = (seconds) % 60L;
      break;
    case JSON_MINS:
      temp = (seconds / 60UL) % 60L;
      break;
    case JSON_HOURS:
      temp = (seconds / 3600UL) % 24L;
      break;
    case JSON_DAYS:
      temp = seconds / (3600UL * 24L);
      break;
    case JSON_ERROR ... JSON_ERROR_3:
      temp = errorCount[JSONKEY - JSON_ERROR];
      break;
    case JSON_ERROR_TCP:
      temp = errorTcpCount;
      break;
    case JSON_ERROR_RTU:
      temp = errorRtuCount;
      break;
    case JSON_ERROR_TIMEOUT:
      temp = errorTimeoutCount;
      break;
    case JSON_QUEUE_DATA:
      temp = (unsigned long)queueDataSize;
      queueDataSize = queueData.size();
      break;
    case JSON_QUEUE_REQUESTS:
      temp = (unsigned long)queueHeadersSize;
      queueHeadersSize = queueHeaders.size();
      break;
#ifdef ENABLE_EXTRA_DIAG
    case JSON_RS485_TX:
      temp = serialTxCount;
      break;
    case JSON_RS485_RX:
      temp = serialRxCount;
      break;
    case JSON_ETH_TX:
      temp = ethTxCount;
      break;
    case JSON_ETH_RX:
      temp = ethRxCount;
      break;
#endif /* ENABLE_EXTRA_DIAG */
    default:
      break;
  }
  chunked.print(temp);
}
