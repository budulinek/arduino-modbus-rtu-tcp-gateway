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

const byte webOutBufferSize = 64;               // size of web server write buffer (used by StreamLib)

void sendPage(EthernetClient &client, byte reqPage)
{
  dbgln(F("[web] 200 response send"));
  char webOutBuffer[webOutBufferSize];
  ChunkedPrint page(client, webOutBuffer, sizeof(webOutBuffer));
  page.println(F("HTTP/1.1 200 OK"));
  page.println(F("Connection: close"));
  page.println(F("Content-Type: text/html"));
  page.println(F("Transfer-Encoding: chunked"));
  page.println();
  page.begin();
  page.print(F("<!doctype html>"            // the start of the HTTP Body - contains the HTML
               "<html lang=en>"
               "<head>"
               "<meta charset=utf-8"));
  if (reqPage == 1 || reqPage == 0xFF) page.print(F(" http-equiv=refresh content=5"));
  if (reqPage == 0xFF) {
    page.print(F(";url=http://"));
    page.print((IPAddress)localConfig.ip);
    page.print(F(":"));
    page.print(localConfig.webPort);
  }
  page.print(F(">"
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
  page.print(F(" onload='dis(document.getElementById(&quot;box&quot;).checked)'>"
               "<script>function dis(st) {var x = document.getElementsByClassName('ip');for (var i = 0; i < x.length; i++) {x[i].disabled = st}}</script"));
#endif /* ENABLE_DHCP */
  page.print(F(">"
               "<table height=100% style='position:absolute;top:0;bottom:0;left:0;right:0'>"
               "<tr style='height:10px'><th colspan=2>"
               "<h1 style='margin:0px'>Modbus RTU &rArr; Modbus TCP/UDP Gateway</h1>"  // first row is header
               "<tr>"                                           // second row is left menu (first cell) and main page (second cell)
               "<th valign=top style=width:20%;padding:0px>"

               // Left Menu
               "<table>"));
  for (byte i = 0; i < pagesCnt; i++) {
    page.print(F("<tr><th"));
    if ((i + 1) == reqPage) {
      page.print(F(" style=background-color:#FF6600"));
    }
    page.print(F("><a href="));
    page.print(i + 1);
    page.print(F(".htm>"));
    menuItem(page, i + 1);
    page.print(F("</a>"));
  }
  page.print(F("</table><td valign=top style=padding:0px>"));

  // Main Page
  page.print(F("<form action=/"));
  page.print(reqPage);
  page.print(F(".htm method=post>"
               "<table style=border-collapse:collapse>"
               "<tr><th><th>"));
  menuItem(page, reqPage);
  page.print(F("<tr><td><br>"));

  //   PLACE FUNCTIONS PROVIDING CONTENT HERE
  // content should start with new table row <tr>

  switch (reqPage) {
    case 0:
      break;
    case 1:
      contentStatus(page);
      break;
    case 2:
      contentIp(page);
      break;
    case 3:
      contentTcp(page);
      break;
    case 4:
      contentRtu(page);
      break;
    case 5:
      contentTools(page);
      break;
    case 0xFF:
      contentWait(page);
      break;
    default:
      break;
  }
  if (reqPage >= 2 && reqPage <= 4) {
    page.print(F("<tr><td><br><input type=submit value='Save & Apply'><td><br><input type=reset value=Cancel>"));
  }
  page.print(F("</table></form>"
               "</table></body></html>"));
  page.end();
}

// Menu items, item corresponds to requested page number
void menuItem(ChunkedPrint& menu, byte item) {
  switch (item) {
    case 0:
      break;
    case 1:
      menu.print(F("Current Status"));
      break;
    case 2:
      menu.print(F("IP Settings"));
      break;
    case 3:
      menu.print(F("TCP/UDP Settings"));
      break;
    case 4:
      menu.print(F("RS485 Settings"));
      break;
    case 5:
      menu.print(F("Tools"));
      break;
    default:
      break;
  }
}

//        Current Status
void contentStatus(ChunkedPrint& content)
{
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
                  "<tr><td>Modbus RTU Slaves:<td><button name=a value=6>Scan</button>"));  // value=6 must correspond to enum action = SCAN
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
      else content.print(F(" waiting..."));
    }
  }
  if (countSlaves == 0 && scanCounter == 0) content.print(F("<tr><td><td>none"));
}


//            IP Settings
void contentIp(ChunkedPrint& content)
{
#ifdef ENABLE_DHCP
  content.print(F("<tr><td>Auto IP:"
                  "<td><input type=hidden name=i1 value=0>"
                  "<input type=checkbox id=box name=i1 onclick=dis(this.checked) value=1"));
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
      content.print(F("<input name=i"));
      content.print(i + 2 + (j * 4));
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
    content.print(F("t"));
    content.print(i + 1);
    content.print(F("min=1 max=65535 value="));
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
  content.print(F("<tr><td>Modbus Mode:<td><select name=t4>"));
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
  content.print(F("r1 min=300 max=250000 value="));
  content.print(localConfig.baud);
  content.print(F("> (300~250000) bps"
                  "<tr><td>Data Bits:<td><select name=r2>"));
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
                  "<select name=r3>"));
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
                  "<tr><td>Stop Bits:<td><select name=r4>"));
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
  content.print(F("r5 min=100 max=9999 value="));
  content.print(localConfig.serialTimeout);
  content.print(F("> (100~9999) ms"
                  "<tr><td>Retry Attempts:"));
  helperInput(content);
  content.print(F("r6 min=1 max=10 value="));
  content.print(localConfig.serialRetry);
  content.print(F("> (1~10)"));
}

//        Tools
void contentTools(ChunkedPrint& content)
{
  content.print(F("<tr><td>Factory Defaults:<td><button name=a value=1>Restore</button> (Static IP: "));
  content.print((IPAddress)defaultConfig.ip);
  content.print(F(")"
                  "<tr><td>MAC Address: <td><button name=a value=2>Generate New</button>"
                  "<tr><td>Microcontroller: <td><button name=a value=3>Reboot</button>"));
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
  dbgln(F("[web] response 404 file not found"));
  client.println(F("HTTP/1.0 404 Not Found"));
  client.println(F("Content-Type: text/plain"));  // simple plain text without html tags
  client.println();
  client.println(F("File Not Found"));
}


void send204(EthernetClient &client)
{
  dbgln(F("[web] response 204 no content"));
  client.println(F("HTTP/1.0 204 No content"));
}
