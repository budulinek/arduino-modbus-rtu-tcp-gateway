/* *******************************************************************
   Pages for Webserver

   sendPage
   - displays main page, renders title and left menu using tables
   - calls content functions depending on the number (i.e. URL) of the requested web page
   - also displays buttons for some of the pages

   menuItem
   - returns menu item string depending on the the number (i.e. URL) of the requested web page

   contentStatus, contentIp, contentTcp, contentRtu, contentTools
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
                  "Connection: close\r\n"
                  "Content-Type: text/html\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"));
  chunked.begin();
  chunked.print(F("<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta"));
  if (reqPage == PAGE_STATUS || reqPage == PAGE_WAIT) chunked.print(F(" http-equiv=refresh content=5"));
  if (reqPage == PAGE_WAIT) {  // redirect to new IP and web port
    chunked.print(F(";url=http://"));
    chunked.print((IPAddress)localConfig.ip);
    chunked.print(F(":"));
    chunked.print(localConfig.webPort);
  }
  chunked.print(F(">"
                  "<title>Modbus RTU &rArr; Modbus TCP/UDP Gateway</title>"
                  "<style>"
                  "html,body{margin:0;height:100%;font-family:sans-serif}"
                  "a{text-decoration:none;color:white}"
                  "td:first-child {text-align:right;width:30%}"
                  "th{text-align:left;background-color:#0067AC;color:white;padding:10px;height:0}"
                  "table{width:100%}"
                  "h1{margin:0}"
                  "</style>"
                  "</head>"
                  "<body"));
#ifdef ENABLE_DHCP
  chunked.print(F(" onload='dis(document.getElementById(&quot;box&quot;).checked)'>"
                  "<script>function dis(st) {var x = document.getElementsByClassName('ip');for (var i = 0; i < x.length; i++) {x[i].disabled = st}}</script"));
#endif /* ENABLE_DHCP */
  chunked.print(F(">"
                  "<table height=100%>"
                  "<tr><th colspan=2>"
                  "<h1>Modbus RTU &rArr; Modbus TCP/UDP Gateway</h1>"  // first row is header
                  "<tr valign=top>"                                    // second row is left menu (first cell) and main page (second cell)
                  "<th style=width:20%;padding:0>"

                  // Left Menu
                  "<table>"));
  for (byte i = 1; i < PAGE_WAIT; i++) {
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
  chunked.print(F("</table><td style=padding:0>"));

  // Main Page
  chunked.print(F("<form action=/"));
  chunked.print(reqPage);
  chunked.print(F(".htm method=post>"
                  "<table style=border-collapse:collapse>"
                  "<tr><th><th>"));
  menuItem(chunked, reqPage);
  chunked.print(F("<tr><td><br>"));

  //   PLACE FUNCTIONS PROVIDING CONTENT HERE
  // content should start with new table row <tr>

  switch (reqPage) {
    case PAGE_NONE:
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
    case PAGE_STATUS:
      chunked.print(F("Current Status"));
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
    case PAGE_TOOLS:
      chunked.print(F("Tools"));
      break;
    default:
      break;
  }
}

//        Current Status
void contentStatus(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td>SW Version:<td>"));
  chunked.print(VERSION[0], HEX);
  chunked.print(F("."));
  chunked.print(VERSION[1], HEX);
  chunked.print(F("<tr><td>Microcontroller:<td>"));
  chunked.print(BOARD);
  // chunked.print(freeMemory());
//  chunked.print(serialState, HEX);

  chunked.print(F("<tr><td>Ethernet Chip:<td>"));
  switch (Ethernet.hardwareStatus()) {
    case EthernetW5100:
      chunked.print(F("W5100"));
      break;
    case EthernetW5200:
      chunked.print(F("W5200"));
      break;
    case EthernetW5500:
      chunked.print(F("W5500"));
      break;
    default:  // TODO: add W6100 once it is included in Ethernet library
      chunked.print(F("unknown"));
      break;
  }
  chunked.print(F("<tr><td>Ethernet Sockets:<td>"));
  chunked.print(maxSockNum, HEX);
  chunked.print(F("<tr><td>MAC Address:<td>"));
  byte macBuffer[6];
  Ethernet.MACAddress(macBuffer);
  for (byte i = 0; i < 6; i++) {
    if (macBuffer[i] < 16) chunked.print(F("0"));
    chunked.print(macBuffer[i], HEX);
    if (i < 5) chunked.print(F(":"));
  }

#ifdef ENABLE_DHCP
  chunked.print(F("<tr><td>Auto IP:<td>"));
  if (!localConfig.enableDhcp) {
    chunked.print(F("DHCP disabled"));
  } else if (dhcpSuccess == true) {
    chunked.print(F("DHCP successful"));
  } else {
    chunked.print(F("DHCP failed, using fallback static IP"));
  }
#endif /* ENABLE_DHCP */

  chunked.print(F("<tr><td>IP Address:<td>"));
  chunked.print(Ethernet.localIP());

#ifdef ENABLE_EXTRA_DIAG
  chunked.print(F("<tr><td>Run Time:<td>"));
  byte mod_seconds = byte((seconds) % 60);
  byte mod_minutes = byte((seconds / 60) % 60);
  byte mod_hours = byte((seconds / (60 * 60)) % 24);
  int days = (seconds / (60U * 60U * 24));
  chunked.print(days);
  chunked.print(F(" days, "));
  chunked.print(mod_hours);
  chunked.print(F(" hours, "));
  chunked.print(mod_minutes);
  chunked.print(F(" mins, "));
  chunked.print(mod_seconds);
  chunked.print(F(" secs"));
  chunked.print(F("<tr><td>RS485 Data:<td>"));
  chunked.print(serialTxCount);
  chunked.print(F(" Tx bytes / "));
  chunked.print(serialRxCount);
  chunked.print(F(" Rx bytes"));
  chunked.print(F("<tr><td>Ethernet Data:<td>"));
  chunked.print(ethTxCount);
  chunked.print(F(" Tx bytes / "));
  chunked.print(ethRxCount);
  chunked.print(F(" Rx bytes  (excl. WebUI)"
                  "<tr><td colspan=2>"
                  "<table style=border-collapse:collapse;text-align:center>"
                  "<tr><td><td>Socket Mode<td>Socket Status<td>Local Port<td>Remote IP<td>Remote Port"));
  for (byte i = 0; i < maxSockNum; i++) {
    EthernetClient clientDiag = EthernetClient(i);
    chunked.print(F("<tr><td>Socket "));
    chunked.print(i);
    chunked.print(F(":<td>"));
    switch (W5100.readSnMR(i)) {
      case SnMR::CLOSE:
        chunked.print(F("CLOSE"));
        break;
      case SnMR::TCP:
        chunked.print(F("TCP"));
        break;
      case SnMR::UDP:
        chunked.print(F("UDP"));
        break;
      case SnMR::IPRAW:
        chunked.print(F("IPRAW"));
        break;
      case SnMR::MACRAW:
        chunked.print(F("MACRAW"));
        break;
      case SnMR::PPPOE:
        chunked.print(F("PPPOE"));
        break;
      case SnMR::ND:
        chunked.print(F("ND"));
        break;
      case SnMR::MULTI:
        chunked.print(F("MULTI"));
        break;
      default:
        break;
    }
    chunked.print(F("<td>"));
    switch (clientDiag.status()) {
      case SnSR::CLOSED:
        chunked.print(F("CLOSED"));
        break;
      case SnSR::INIT:
        chunked.print(F("INIT"));
        break;
      case SnSR::LISTEN:
        chunked.print(F("LISTEN"));
        break;
      case SnSR::SYNSENT:
        chunked.print(F("SYNSENT"));
        break;
      case SnSR::SYNRECV:
        chunked.print(F("SYNRECV"));
        break;
      case SnSR::ESTABLISHED:
        chunked.print(F("ESTABLISHED"));
        break;
      case SnSR::FIN_WAIT:
        chunked.print(F("FIN_WAIT"));
        break;
      case SnSR::CLOSING:
        chunked.print(F("CLOSING"));
        break;
      case SnSR::TIME_WAIT:
        chunked.print(F("TIME_WAIT"));
        break;
      case SnSR::CLOSE_WAIT:
        chunked.print(F("CLOSE_WAIT"));
        break;
      case SnSR::LAST_ACK:
        chunked.print(F("LAST_ACK"));
        break;
      case SnSR::UDP:
        chunked.print(F("UDP"));
        break;
      case SnSR::IPRAW:
        chunked.print(F("IPRAW"));
        break;
      case SnSR::MACRAW:
        chunked.print(F("MACRAW"));
        break;
      case SnSR::PPPOE:
        chunked.print(F("PPPOE"));
        break;
      default:
        break;
    }
    chunked.print(F("<td>"));
    chunked.print(clientDiag.localPort());
    chunked.print(F("<td>"));
    chunked.print(clientDiag.remoteIP());
    chunked.print(F("<td>"));
    chunked.print(clientDiag.remotePort());
  }
  chunked.print(F("</table>"));
#endif /* ENABLE_EXTRA_DIAG */

  chunked.print(F("<tr><td>Requests Queue:<td>"));
  chunked.print(queueDataSize);
  chunked.print(F(" / "));
  chunked.print(MAX_QUEUE_DATA);
  chunked.print(F(" bytes"
                  "<tr><td><td>"));
  chunked.print(queueHeadersSize);
  chunked.print(F(" / "));
  chunked.print(MAX_QUEUE_REQUESTS);
  chunked.print(F(" requests"));
  queueDataSize = queueData.size();
  queueHeadersSize = queueHeaders.size();
  chunked.print(F("<tr><td>Modbus Stats:<td>"));
  for (byte i = 0; i < STAT_ERROR_0B_QUEUE; i++) {  // there is no counter for STAT_ERROR_0B_QUEUE
    chunked.print(errorCount[i]);
    helperStats(chunked, i);
    chunked.print(F("<tr><td><td>"));
  }
  chunked.print(errorTcpCount);
  chunked.print(F(" Invalid TCP/UDP Request"
                  "<tr><td><td>"));
  chunked.print(errorRtuCount);
  chunked.print(F(" Invalid RS485 Response"
                  "<tr><td><td>"));
  chunked.print(errorTimeoutCount);
  chunked.print(F(" Response Timeout"
                  "<tr><td>Modbus TCP/UDP Masters:"));
  byte countMasters = 0;
  for (byte i = 0; i < maxSockNum; i++) {
    EthernetClient clientDiag = EthernetClient(i);
    IPAddress ipTemp = clientDiag.remoteIP();
    if (ipTemp != 0 && ipTemp != 0xFFFFFFFF) {
      if (clientDiag.status() == SnSR::UDP && clientDiag.localPort() == localConfig.udpPort) {
        chunked.print(F("<td><tr><td>UDP:<td>"));
        chunked.print((IPAddress)ipTemp);
        countMasters++;
      } else if (clientDiag.localPort() == localConfig.tcpPort) {
        chunked.print(F("<td><tr><td>TCP:<td>"));
        chunked.print((IPAddress)ipTemp);
        countMasters++;
      }
    }
  }
  if (countMasters == 0) chunked.print(F("<td>None"));
  chunked.print(F("<tr><td>Modbus RTU Slaves:<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(SCAN);
  chunked.print(F(">Scan Slaves & Reset Stats</button>"));
  byte countSlaves = 0;
  for (int k = 1; k < MAX_SLAVES; k++) {
    for (int s = 0; s < STAT_NUM; s++) {
      if (getSlaveStatus(k, s) == true || k == scanCounter) {
        countSlaves++;
        chunked.print(F("<tr><td><td>0x"));
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
  if (countSlaves == 0 && scanCounter == 0) chunked.print(F("<tr><td><td>None"));
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
  if (localConfig.enableDhcp) chunked.print(F(" checked"));
  chunked.print(F(">Enable DHCP"));
#endif /* ENABLE_DHCP */
  for (byte j = 0; j < 4; j++) {
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
      case 3:
        chunked.print(F("DNS Server"));
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
        case 3:
          chunked.print(localConfig.dns[i]);
          break;
        default:
          break;
      }
      chunked.print(F(">"));
      if (i < 3) chunked.print(F("."));
    }
  }
}

//            TCP/UDP Settings
void contentTcp(ChunkedPrint &chunked) {
  for (byte i = 0; i < 3; i++) {
    chunked.print(F("<tr><td>"));
    switch (i) {
      case 0:
        chunked.print(F("Modbus TCP"));
        break;
      case 1:
        chunked.print(F("Modbus UDP"));
        break;
      case 2:
        chunked.print(F("Web"));
        break;
      default:
        break;
    }
    chunked.print(F(" Port:"));
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
                  "<tr><td>Response Timeout:"));
  helperInput(chunked);
  chunked.print(POST_TIMEOUT);
  chunked.print(F(" min=100 max=2000 value="));
  chunked.print(localConfig.serialTimeout);
  chunked.print(F("> (100~2000) ms"
                  "<tr><td>Attempts:"));
  helperInput(chunked);
  chunked.print(POST_ATTEMPTS);
  chunked.print(F(" min=1 max=5 value="));
  chunked.print(localConfig.serialAttempts);
  chunked.print(F("> (1~5)"));
}

//        Tools
void contentTools(ChunkedPrint &chunked) {
  chunked.print(F("<tr><td>Factory Defaults:<td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(FACTORY);
  chunked.print(F(">Restore</button> (Static IP: "));
  chunked.print((IPAddress)DEFAULT_CONFIG.ip);
  chunked.print(F(")"
                  "<tr><td>MAC Address: <td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(MAC);
  chunked.print(F(">Generate New</button>"
                  "<tr><td>Microcontroller: <td><button name="));
  chunked.print(POST_ACTION);
  chunked.print(F(" value="));
  chunked.print(REBOOT);
  chunked.print(F(">Reboot</button>"));
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

void send404(EthernetClient &client) {
  client.println(F("HTTP/1.1 404 Not Found\r\n"
                   "Content-Length: 0"));
  client.stop();
}

void send204(EthernetClient &client) {
  client.println(F("HTTP/1.1 204 No content"));
  client.stop();
}
