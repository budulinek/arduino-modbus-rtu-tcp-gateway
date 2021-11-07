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
   - renders some repetitive HTML code for inputs

   send404, send204
   - send error messages

   ***************************************************************** */

const byte webOutBufferSize = 128;               // size of web server write buffer (used by StreamLib)

void sendPage(EthernetClient &client, byte reqPage)
{
  char webOutBuffer[webOutBufferSize];
  ChunkedPrint chunked(client, webOutBuffer, sizeof(webOutBuffer)); // the StreamLib object to replace client print
  dbgln(F("[web out] 200 response send"));
  chunked.print(F("HTTP/1.1 200 OK\r\n"
                  "Connection: close\r\n"
                  "Content-Type: text/html\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"));
  chunked.begin();
  chunked.print(F("<!doctype html>"            // the start of the HTTP Body - contains the HTML
                  "<html lang=en>"
                  "<head>"
                  "<meta charset=utf-8"));
  if (reqPage == PAGE_STATUS || reqPage == PAGE_WAIT) chunked.print(F(" http-equiv=refresh content=5"));
  if (reqPage == PAGE_WAIT) {                                 // redirect to new IP and web port
    chunked.print(F(";url=http://"));
    chunked.print((IPAddress)localConfig.ip);
    chunked.print(F(":"));
    chunked.print(localConfig.webPort);
  }
  chunked.print(F(">"
                  "<title>Modbus RTU &rArr; Modbus TCP/UDP Gateway</title>"
                  "<style>"
                  "a {text-decoration:none;color:white}"
                  "td:first-child {text-align:right;width:30%}"
                  "th {text-align:left;background-color:#0067AC;color:white;padding:10px}"
                  "table {width:100%}"
                  "</style>"
                  "</head>"
                  "<body style='font-family:sans-serif'"));
#ifdef ENABLE_DHCP
  chunked.print(F(" onload='dis(document.getElementById(&quot;box&quot;).checked)'>"
                  "<script>function dis(st) {var x = document.getElementsByClassName('ip');for (var i = 0; i < x.length; i++) {x[i].disabled = st}}</script"));
#endif /* ENABLE_DHCP */
  chunked.print(F(">"
                  "<table height=100% style='position:absolute;top:0;bottom:0;left:0;right:0'>"
                  "<tr style='height:10px'><th colspan=2>"
                  "<h1 style='margin:0px'>Modbus RTU &rArr; Modbus TCP/UDP Gateway</h1>"  // first row is header
                  "<tr>"                                           // second row is left menu (first cell) and main page (second cell)
                  "<th valign=top style=width:20%;padding:0px>"

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
  chunked.print(F("</table><td valign=top style=padding:0px>"));

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
void menuItem(ChunkedPrint& menu, byte item) {
  switch (item) {
    case PAGE_NONE:
      break;
    case PAGE_STATUS:
      menu.print(F("Current Status"));
      break;
    case PAGE_IP:
      menu.print(F("IP Settings"));
      break;
    case PAGE_TCP:
      menu.print(F("TCP/UDP Settings"));
      break;
    case PAGE_RTU:
      menu.print(F("RS485 Settings"));
      break;
    case PAGE_TOOLS:
      menu.print(F("Tools"));
      break;
    default:
      break;
  }
}

//        Current Status
void contentStatus(ChunkedPrint& content)
{
  content.print(F("<tr><td>SW Version:<td>"));
  content.print(version[0], HEX);
  content.print(F("."));
  content.print(version[1], HEX);
  content.print(F("<tr><td>Microcontroller:<td>"));
  content.print(BOARD);
  content.print(F("<tr><td>Ethernet Chip:<td>"));
  switch (Ethernet.hardwareStatus()) {
    case EthernetW5100:
      content.print(F("W5100"));
      break;
    case EthernetW5200:
      content.print(F("W5200"));
      break;
    case EthernetW5500:
      content.print(F("W5500"));
      break;
    default:        // TODO: add W6100 once it is included in Ethernet library
      content.print(F("unknown"));
      break;
  }
  content.print(F("<tr><td>Ethernet Sockets:<td>"));
  content.print(maxSockNum, HEX);
  content.print(F("<tr><td>MAC Address:<td>"));
  byte macBuffer[6];
  Ethernet.MACAddress(macBuffer);
  for (byte i = 0; i < 6; i++) {
    if (macBuffer[i] < 16) content.print(F("0"));
    content.print(macBuffer[i], HEX);
    if (i < 5) content.print(F(":"));
  }

#ifdef ENABLE_DHCP
  content.print(F("<tr><td>Auto IP:<td>"));
  if (!localConfig.enableDhcp) {
    content.print(F("DHCP disabled"));
  } else if (dhcpSuccess == true) {
    content.print(F("DHCP successful"));
  } else {
    content.print(F("DHCP failed, using fallback static IP"));
  }
#endif /* ENABLE_DHCP */

  content.print(F("<tr><td>IP Address:<td>"));
  content.print(Ethernet.localIP());

#ifdef ENABLE_EXTRA_DIAG
  content.print(F("<tr><td>Run Time:<td>"));
  byte mod_seconds = byte((seconds) % 60);
  byte mod_minutes = byte((seconds / 60) % 60);
  byte mod_hours = byte((seconds / (60 * 60)) % 24);
  int days = (seconds / (60U * 60U * 24));
  content.print(days);
  content.print(F(" days, "));
  content.print(mod_hours);
  content.print(F(" hours, "));
  content.print(mod_minutes);
  content.print(F(" mins, "));
  content.print(mod_seconds);
  content.print(F(" secs"));
#endif /* ENABLE_EXTRA_DIAG */

  content.print(F("<tr><td>RS485 Data:<td>"));
  content.print(serialTxCount);
  content.print(F(" Tx bytes / "));
  content.print(serialRxCount);
  content.print(F(" Rx bytes"
                  "<tr><td>Ethernet Data:<td>"));
  content.print(ethTxCount);
  content.print(F(" Tx bytes / "));
  content.print(ethRxCount);
  content.print(F(" Rx bytes  (excl. WebUI)"));

#ifdef ENABLE_EXTRA_DIAG
  content.print(F("<tr><td colspan=2>"
                  "<table style=border-collapse:collapse;text-align:center>"
                  "<tr><td><td>Socket Mode<td>Socket Status<td>Local Port<td>Remote IP<td>Remote Port"));
  for (byte i = 0; i < maxSockNum ; i++) {
    EthernetClient clientDiag = EthernetClient(i);
    content.print(F("<tr><td>Socket "));
    content.print(i);
    content.print(F(":<td>"));
    switch (W5100.readSnMR(i)) {
      case SnMR::CLOSE:
        content.print(F("CLOSE"));
        break;
      case SnMR::TCP:
        content.print(F("TCP"));
        break;
      case SnMR::UDP:
        content.print(F("UDP"));
        break;
      case SnMR::IPRAW:
        content.print(F("IPRAW"));
        break;
      case SnMR::MACRAW:
        content.print(F("MACRAW"));
        break;
      case SnMR::PPPOE:
        content.print(F("PPPOE"));
        break;
      case SnMR::ND:
        content.print(F("ND"));
        break;
      case SnMR::MULTI:
        content.print(F("MULTI"));
        break;
      default:
        break;
    }
    content.print(F("<td>"));
    switch (clientDiag.status()) {
      case SnSR::CLOSED:
        content.print(F("CLOSED"));
        break;
      case SnSR::INIT:
        content.print(F("INIT"));
        break;
      case SnSR::LISTEN:
        content.print(F("LISTEN"));
        break;
      case SnSR::SYNSENT:
        content.print(F("SYNSENT"));
        break;
      case SnSR::SYNRECV:
        content.print(F("SYNRECV"));
        break;
      case SnSR::ESTABLISHED:
        content.print(F("ESTABLISHED"));
        break;
      case SnSR::FIN_WAIT:
        content.print(F("FIN_WAIT"));
        break;
      case SnSR::CLOSING:
        content.print(F("CLOSING"));
        break;
      case SnSR::TIME_WAIT:
        content.print(F("TIME_WAIT"));
        break;
      case SnSR::CLOSE_WAIT:
        content.print(F("CLOSE_WAIT"));
        break;
      case SnSR::LAST_ACK:
        content.print(F("LAST_ACK"));
        break;
      case SnSR::UDP:
        content.print(F("UDP"));
        break;
      case SnSR::IPRAW:
        content.print(F("IPRAW"));
        break;
      case SnSR::MACRAW:
        content.print(F("MACRAW"));
        break;
      case SnSR::PPPOE:
        content.print(F("PPPOE"));
        break;
      default:
        break;
    }
    content.print(F("<td>"));
    content.print(clientDiag.localPort());
    content.print(F("<td>"));
    content.print(clientDiag.remoteIP());
    content.print(F("<td>"));
    content.print(clientDiag.remotePort());
  }
  content.print(F("</table>"));
#endif /* ENABLE_EXTRA_DIAG */

  content.print(F("<tr><td><br>"
                  "<tr><td>Modbus TCP/UDP Masters:"));
  byte countMasters = 0;
  for (byte i = 0; i < maxSockNum; i++) {
    EthernetClient clientDiag = EthernetClient(i);
    IPAddress ipTemp = clientDiag.remoteIP();
    if (ipTemp != 0 && ipTemp != 0xFFFFFFFF) {
      if (clientDiag.status() == SnSR::UDP && clientDiag.localPort() == localConfig.udpPort) {
        content.print(F("<td><tr><td>UDP:<td>"));
        content.print(ipTemp);
        countMasters++;
      } else if (clientDiag.localPort() == localConfig.tcpPort) {
        content.print(F("<td><tr><td>TCP:<td>"));
        content.print(ipTemp);
        countMasters++;
      }
    }
  }
  if (countMasters == 0) content.print(F("<td>none"));
  content.print(F("<tr><td><br>"
                  "<tr><td>Modbus RTU Slaves:<td><button name="));
  content.print(POST_ACTION);
  content.print(F(" value="));
  content.print(SCAN);
  content.print(F(">Scan</button>"));
  byte countSlaves = 0;
  for (int k = 1; k < maxSlaves; k++) {
    if (getSlaveResponding(k) == true || k == scanCounter) {
      content.print(F("<tr><td><td>0x"));
      if (k < 16) content.print(F("0"));
      content.print(k, HEX);
      if (getSlaveResponding(k) == true) {
        content.print(F(" OK"));
        countSlaves++;
      }
      else content.print(F(" scanning..."));
    }
  }
  if (countSlaves == 0 && scanCounter == 0) content.print(F("<tr><td><td>none"));
}


//            IP Settings
void contentIp(ChunkedPrint& content)
{
#ifdef ENABLE_DHCP
  content.print(F("<tr><td>Auto IP:"
                  "<td><input type=hidden name="));
  content.print(POST_DHCP);
  content.print(F("value=0>"
                  "<input type=checkbox id=box name="));
  content.print(POST_DHCP);
  content.print(F("onclick=dis(this.checked) value=1"));
  if (localConfig.enableDhcp) content.print(F(" checked"));
  content.print(F(">Enable DHCP"));
#endif /* ENABLE_DHCP */
  for (byte j = 0; j < 4; j++) {
    content.print(F("<tr><td>"));
    switch (j) {
      case 0:
        content.print(F("Static IP"));
        break;
      case 1:
        content.print(F("Submask"));
        break;
      case 2:
        content.print(F("Gateway"));
        break;
      case 3:
        content.print(F("DNS Server"));
        break;
      default:
        break;
    }
    content.print(F(":<td>"));
    for (byte i = 0; i < 4; i++) {
      content.print(F("<input name="));
      content.print(POST_IP + i + (j * 4));
      content.print(F(" type=tel class=ip required maxlength=3 size=3 pattern='^(&bsol;d{1,2}|1&bsol;d&bsol;d|2[0-4]&bsol;d|25[0-5])$' value="));
      switch (j) {
        case 0:
          content.print(localConfig.ip[i]);
          break;
        case 1:
          content.print(localConfig.subnet[i]);
          break;
        case 2:
          content.print(localConfig.gateway[i]);
          break;
        case 3:
          content.print(localConfig.dns[i]);
          break;
        default:
          break;
      }
      content.print(F(">"));
      if (i < 3) content.print(F("."));
    }
  }
}

//            TCP/UDP Settings
void contentTcp(ChunkedPrint& content)
{
  for (byte i = 0; i < 3; i++) {
    content.print(F("<tr><td>"));
    switch (i) {
      case 0:
        content.print(F("Modbus TCP"));
        break;
      case 1:
        content.print(F("Modbus UDP"));
        break;
      case 2:
        content.print(F("Web"));
        break;
      default:
        break;
    }
    content.print(F(" Port:"));
    helperInput(content);
    content.print(POST_TCP + i);
    content.print(F(" min=1 max=65535 value="));
    switch (i) {
      case 0:
        content.print(localConfig.tcpPort);
        break;
      case 1:
        content.print(localConfig.udpPort);
        break;
      case 2:
        content.print(localConfig.webPort);
        break;
      default:
        break;
    }
    content.print(F(">"));
  }
  content.print(F("<tr><td>Modbus Mode:<td><select name="));
  content.print(POST_RTU_OVER);
  content.print(F(">"));
  for (byte i = 0; i < 2; i++) {
    content.print(F("<option value="));
    content.print(i);
    if (localConfig.enableRtuOverTcp == i) content.print(F(" selected"));
    content.print(F(">"));
    switch (i) {
      case 0:
        content.print(F("Modbus TCP/UDP"));
        break;
      case 1:
        content.print(F("Modbus RTU over TCP/UDP"));
        break;
      default:
        break;
    }
    content.print(F("</option>"));
  }
  content.print(F("</select>"));
}

//            RS485 Settings
void contentRtu(ChunkedPrint& content)
{
  content.print(F("<tr><td>Baud Rate:"));
  helperInput(content);
  content.print(POST_BAUD);
  content.print(F(" min=300 max=250000 value="));
  content.print(localConfig.baud);
  content.print(F("> (300~250000) bps"
                  "<tr><td>Data Bits:<td><select name="));
  content.print(POST_DATA);
  content.print(F(">"));
  for (byte i = 5; i <= 8; i++) {
    content.print(F("<option value="));
    content.print(i);
    if ((((localConfig.serialConfig & 0x06) >> 1) + 5) == i) content.print(F(" selected"));
    content.print(F(">"));
    content.print(i);
    content.print(F("</option>"));
  }
  content.print(F("</select> bit"
                  "<tr><td>Parity:<td>"
                  "<select name="));
  content.print(POST_PARITY);
  content.print(F(">"));
  for (byte i = 0; i <= 3; i++) {
    if (i == 1) continue;       // invalid value, skip and continue for loop
    content.print(F("<option value="));
    content.print(i);
    if (((localConfig.serialConfig & 0x30) >> 4) == i) content.print(F(" selected"));
    content.print(F(">"));
    switch (i) {
      case 0:
        content.print(F("None"));
        break;
      case 2:
        content.print(F("Even"));
        break;
      case 3:
        content.print(F("Odd"));
        break;
      default:
        break;
    }
    content.print(F("</option>"));
  }
  content.print(F("</select>"
                  "<tr><td>Stop Bits:<td><select name="));
  content.print(POST_STOP);
  content.print(F(">"));
  for (byte i = 1; i <= 2; i++) {
    content.print(F("<option value="));
    content.print(i);
    if ((((localConfig.serialConfig & 0x08) >> 3) + 1) == i) content.print(F(" selected"));
    content.print(F(">"));
    content.print(i);
    content.print(F("</option>"));
  }
  content.print(F("</select> bit"
                  "<tr><td>Response Timeout:"));
  helperInput(content);
  content.print(POST_TIMEOUT);
  content.print(F(" min=100 max=9999 value="));
  content.print(localConfig.serialTimeout);
  content.print(F("> (100~9999) ms"
                  "<tr><td>Retry Attempts:"));
  helperInput(content);
  content.print(POST_RETRY);
  content.print(F(" min=1 max=10 value="));
  content.print(localConfig.serialRetry);
  content.print(F("> (1~10)"));
}

//        Tools
void contentTools(ChunkedPrint& content)
{
  content.print(F("<tr><td>Factory Defaults:<td><button name="));
  content.print(POST_ACTION);
  content.print(F(" value="));
  content.print(FACTORY);
  content.print(F(">Restore</button> (Static IP: "));
  content.print((IPAddress)defaultConfig.ip);
  content.print(F(")"
                  "<tr><td>MAC Address: <td><button name="));
  content.print(POST_ACTION);
  content.print(F(" value="));
  content.print(MAC);
  content.print(F(">Generate New</button>"
                  "<tr><td>Microcontroller: <td><button name="));
  content.print(POST_ACTION);
  content.print(F(" value="));
  content.print(REBOOT);
  content.print(F(">Reboot</button>"));
}

void contentWait(ChunkedPrint& content)
{
  content.print(F("<tr><td><td><br>Reloading. Please wait..."));
}

// Functions providing snippets of repetitive HTML code
void helperInput(ChunkedPrint& helper)
{
  helper.print(F("<td><input size=7 required type=number name="));
}

void send404(EthernetClient &client)
{
  dbgln(F("[web out] response 404 file not found"));
  client.println(F("HTTP/1.1 404 Not Found\r\n"
                   "Content-Length: 0"));
  client.stop();
}


void send204(EthernetClient &client)
{
  dbgln(F("[web out] response 204 no content"));
  client.println(F("HTTP/1.1 204 No content"));
  client.stop();
}
