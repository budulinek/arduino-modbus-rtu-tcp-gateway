/* *******************************************************************
   Modbus TCP/UDP functions

   recvUdp
   - receives Modbus UDP (or Modbus RTU over UDP) messages
   - calls checkRequest

   recvTcp
   - receives Modbus TCP (or Modbus RTU over TCP) messages
   - calls checkRequest

   scanRequest
   - inserts scan request into queue

   checkRequest
   - checks Modbus TCP/UDP requests (correct MBAP header, CRC in case of Modbus RTU over TCP/UDP)
   - checks availability of queue
   - stores requests in queue or returns an error

   deleteRequest
   - deletes requests from queue

   getSlaveStatus, setSlaveStatus
   - read from and write to bool array

   ***************************************************************** */

// Stored in "header.requestType"
#define PRIORITY_REQUEST B10000000  // Request to slave which is not "nonresponding"
#define SCAN_REQUEST B01000000      // Request triggered by slave scanner
#define UDP_REQUEST B00100000       // UDP request
#define TCP_REQUEST B00001111       // TCP request, also stores TCP client number

// bool arrays for storing Modbus RTU status of individual slaves
uint8_t stat[STAT_NUM][(MAX_SLAVES + 1 + 7) / 8];

// Scan request is in the queue
bool scanReqInQueue = false;
// Counter for priority requests in the queue
byte priorityReqInQueue;

uint8_t masks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

typedef struct {
  byte tid[2];           // MBAP Transaction ID
  byte msgLen;           // lenght of Modbus message stored in queueData
  IPAddress remIP;       // remote IP for UDP client (UDP response is sent back to remote IP)
  unsigned int remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  byte requestType;      // TCP client who sent the request
  byte atts;             // attempts counter
} header;

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, MAX_QUEUE_REQUESTS> queueHeaders;  // queue of requests' headers and metadata
CircularBuffer<byte, MAX_QUEUE_DATA> queueData;           // queue of PDU data

byte addressPos;

void recvUdp() {
  unsigned int msgLength = Udp.parsePacket();
  if (msgLength) {
#ifdef ENABLE_EXTRA_DIAG
    ethRxCount += msgLength;
#endif                               /* ENABLE_EXTRA_DIAG */
    byte inBuffer[MODBUS_SIZE + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
    // Modbus TCP/UDP frame: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address)..... no CRC
    // Modbus RTU frame: [0] address.....[n-1][n] CRC
    Udp.read(inBuffer, sizeof(inBuffer));
    Udp.flush();
    byte errorCode = checkRequest(inBuffer, msgLength, (IPAddress)Udp.remoteIP(), Udp.remotePort(), UDP_REQUEST);
    if (errorCode) {
      // send back message with error code
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      if (!localConfig.enableRtuOverTcp) {
        Udp.write(inBuffer, 5);
        Udp.write(0x03);
      }
      byte addressPos = 6 * !localConfig.enableRtuOverTcp;  // position of slave address in the incoming TCP/UDP message (0 for Modbus RTU over TCP/UDP and 6 for Modbus RTU over TCP/UDP)
      Udp.write(inBuffer[addressPos]);                      // address
      Udp.write(inBuffer[addressPos + 1] + 0x80);           // function + 0x80
      Udp.write(errorCode);
      if (localConfig.enableRtuOverTcp) {
        crc = 0xFFFF;
        calculateCRC(inBuffer[addressPos]);
        calculateCRC(inBuffer[addressPos + 1] + 0x80);
        calculateCRC(errorCode);
        Udp.write(lowByte(crc));  // send CRC, low byte first
        Udp.write(highByte(crc));
      }
      Udp.endPacket();
#ifdef ENABLE_EXTRA_DIAG
      ethTxCount += 5;
      if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
    }
  }
}

void recvTcp() {
  EthernetClient client = modbusServer.available();
  if (client) {
    unsigned int msgLength = client.available();
#ifdef ENABLE_EXTRA_DIAG
    ethRxCount += msgLength;
#endif                               /* ENABLE_EXTRA_DIAG */
    byte inBuffer[MODBUS_SIZE + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
    // Modbus TCP/UDP frame: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address).....
    // Modbus RTU frame: [0] address.....
    client.read(inBuffer, sizeof(inBuffer));
    client.flush();
    byte errorCode = checkRequest(inBuffer, msgLength, (IPAddress){}, 0, client.getSocketNumber());
    if (errorCode) {
      // send back message with error code
      if (!localConfig.enableRtuOverTcp) {
        client.write(inBuffer, 5);
        client.write(0x03);
      }
      byte addressPos = 6 * !localConfig.enableRtuOverTcp;  // position of slave address in the incoming TCP/UDP message (0 for Modbus RTU over TCP/UDP and 6 for Modbus RTU over TCP/UDP)
      client.write(inBuffer[addressPos]);                   // address
      client.write(inBuffer[addressPos + 1] + 0x80);        // function + 0x80
      client.write(errorCode);
      if (localConfig.enableRtuOverTcp) {
        crc = 0xFFFF;
        calculateCRC(inBuffer[addressPos]);
        calculateCRC(inBuffer[addressPos + 1] + 0x80);
        calculateCRC(errorCode);
        client.write(lowByte(crc));  // send CRC, low byte first
        client.write(highByte(crc));
      }
#ifdef ENABLE_EXTRA_DIAG
      ethTxCount += 5;
      if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
    }
  }
}

void scanRequest() {
  // Insert scan request into queue, allow only one scan request in a queue
  if (scanCounter != 0 && queueHeaders.available() > 1 && queueData.available() > sizeof(SCAN_COMMAND) + 1 && scanReqInQueue == false) {
    scanReqInQueue = true;
    // Store scan request in request queue
    queueHeaders.push(header{
      { 0x00, 0x00 },            // tid[2]
      sizeof(SCAN_COMMAND) + 1,  // msgLen
      {},                        // remIP
      0,                         // remPort
      SCAN_REQUEST,              // requestType
      0,                         // atts
    });
    queueData.push(scanCounter);  // address of the scanned slave
    for (byte i = 0; i < sizeof(SCAN_COMMAND); i++) {
      queueData.push(SCAN_COMMAND[i]);
    }
    scanCounter++;
    if (scanCounter == MAX_SLAVES + 1) scanCounter = 0;
  }
}

byte checkRequest(const byte inBuffer[], unsigned int msgLength, const IPAddress remoteIP, const unsigned int remotePort, byte requestType) {
  byte addressPos = 6 * !localConfig.enableRtuOverTcp;  // position of slave address in the incoming TCP/UDP message (0 for Modbus RTU over TCP/UDP and 6 for Modbus RTU over TCP/UDP)
  if (localConfig.enableRtuOverTcp) {                   // check CRC for Modbus RTU over TCP/UDP
    if (checkCRC(inBuffer, msgLength) == false) {
      errorTcpCount++;
      return 0;  // drop request and do not return any error code
    }
  } else {  // check MBAP header structure for Modbus TCP/UDP
    if (inBuffer[2] != 0x00 || inBuffer[3] != 0x00 || inBuffer[4] != 0x00 || inBuffer[5] != msgLength - 6) {
      errorTcpCount++;
      return 0;  // drop request and do not return any error code
    }
  }
  msgLength = msgLength - addressPos - (2 * localConfig.enableRtuOverTcp);  // in Modbus RTU over TCP/UDP do not store CRC
  // check if we have space in request queue
  if (queueHeaders.available() < 1 || queueData.available() < msgLength) {
    setSlaveStatus(inBuffer[addressPos], STAT_ERROR_0A, true);
    return 0x0A;  // return modbus error 0x0A (Gateway Overloaded)
  }
  // allow only one request to non responding slaves
  if (getSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B_QUEUE)) {
    errorCount[STAT_ERROR_0B]++;
    return 0x0B;  // return modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
  } else if (getSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B)) {
    setSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B_QUEUE, true);
  } else {
    // Add PRIORITY_REQUEST flag to requests for responding slaves
    requestType = requestType | PRIORITY_REQUEST;
    priorityReqInQueue++;
  }
  if (inBuffer[addressPos] == 0x00) {          // Modbus Broadcast
    requestType = requestType | SCAN_REQUEST;  // Treat broadcast as scan (only one attempt, short timeout, do not expect response)
  }
  // all checkes passed OK, we can store the incoming data in request queue
  // Store in request queue
  queueHeaders.push(header{
    { inBuffer[0], inBuffer[1] },  // tid[2] (ignored in Modbus RTU over TCP/UDP)
    (byte)msgLength,               // msgLen
    (IPAddress)remoteIP,           // remIP
    (unsigned int)remotePort,      // remPort
    (byte)(requestType),           // requestType
    0,                             // atts
  });
  for (byte i = 0; i < msgLength; i++) {
    queueData.push(inBuffer[i + addressPos]);
  }
  if (queueData.size() > queueDataSize) queueDataSize = queueData.size();
  if (queueHeaders.size() > queueHeadersSize) queueHeadersSize = queueHeaders.size();
  return 0;
}

void deleteRequest()  // delete request from queue
{
  header myHeader = queueHeaders.first();
  if (myHeader.requestType & SCAN_REQUEST) scanReqInQueue = false;
  if (myHeader.requestType & PRIORITY_REQUEST) priorityReqInQueue--;
  for (byte i = 0; i < myHeader.msgLen; i++) {
    queueData.shift();
  }
  queueHeaders.shift();
}

bool getSlaveStatus(const uint8_t slave, const byte status) {
  if (slave >= MAX_SLAVES) return false;  // error
  return (stat[status][slave / 8] & masks[slave & 7]) > 0;
}

void setSlaveStatus(const uint8_t slave, byte status, const bool value) {
  if (slave >= MAX_SLAVES) return;  // error
  if (value == 0) {
    stat[status][slave / 8] &= ~masks[slave & 7];
  } else {
    for (byte i = 0; i < STAT_NUM; i++) {
      stat[i][slave / 8] &= ~masks[slave & 7];  // set all other flags to false
    }
    stat[status][slave / 8] |= masks[slave & 7];
    if (status != STAT_ERROR_0B_QUEUE) errorCount[status]++; // there is no counter for STAT_ERROR_0B_QUEUE
  }
}
