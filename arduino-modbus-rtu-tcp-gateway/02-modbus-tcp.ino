/* *******************************************************************
   Modbus TCP/UDP functions

   recvUdp
   - receives Modbus UDP (or Modbus RTU over UDP) messages
   - calls checkRequest

   recvTcp
   - receives Modbus TCP (or Modbus RTU over TCP) messages
   - calls checkRequest

   processRequests
   - inserts scan request into queue
   - optimizes queue

   checkRequest
   - checks Modbus TCP/UDP requests (correct MBAP header, CRC in case of Modbus RTU over TCP/UDP)
   - checks availability of queue
   - stores requests in queue or returns an error

   deleteRequest
   - deletes requests from queue

   getSlaveStatus, setSlaveStatus
   - read from and write to bool array

   ***************************************************************** */

#define ADDRESS_POS (6 * !localConfig.enableRtuOverTcp)  // position of slave address in the incoming TCP/UDP message (0 for Modbus RTU over TCP/UDP and 6 for Modbus RTU over TCP/UDP)

enum status : byte {
  STAT_OK,              // Slave Responded
  STAT_ERROR_0X,        // Slave Responded with Error (Codes 1~8)
  STAT_ERROR_0A,        // Gateway Overloaded (Code 10)
  STAT_ERROR_0B,        // Slave Failed to Respond (Code 11)
  STAT_ERROR_0B_QUEUE,  // Slave Failed to Respond (Code 11) + in Queue
  STAT_NUM              // Number of status flags in this enum. Must be the last element within this enum!!
};

// bool arrays for storing Modbus RTU status of individual slaves
uint8_t stat[STAT_NUM][(maxSlaves + 1 + 7) / 8];

// Scan request is in queue
bool scanInQueue = false;

// array for storing error counts
uint16_t errorCount[STAT_NUM];
uint16_t errorInvalid;

uint16_t queueDataSize;
uint8_t queueHeadersSize;

uint8_t masks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

typedef struct {
  byte tid[2];           // MBAP Transaction ID
  byte msgLen;           // lenght of Modbus message stored in queueData
  IPAddress remIP;       // remote IP for UDP client (UDP response is sent back to remote IP)
  unsigned int remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  byte clientNum;        // TCP client who sent the request, UDP_REQUEST (0xFF) designates UDP client
  byte atts;             // attempts counter
} header;

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, maxQueueRequests> queueHeaders;  // queue of requests' headers and metadata (MBAP transaction ID, MBAP unit ID, PDU length, remIP, remPort, TCP client)
CircularBuffer<byte, maxQueueData> queueData;           // queue of PDU data (function code, data)

void recvUdp() {
  unsigned int msgLength = Udp.parsePacket();
  if (msgLength) {
#ifdef ENABLE_EXTRA_DIAG
    ethRxCount += msgLength;
#endif                              /* ENABLE_EXTRA_DIAG */
    byte inBuffer[modbusSize + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
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
#endif                              /* ENABLE_EXTRA_DIAG */
    byte inBuffer[modbusSize + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
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

void processRequests() {
  // Insert scan request into queue, allow only one scan request in a queue
  if (scanCounter != 0 && queueHeaders.available() > 1 && queueData.available() > sizeof(scanCommand) + 1 && scanInQueue == false) {
    scanInQueue = true;
    // Store scan request in request queue
    queueHeaders.push(header{
      { 0x00, 0x00 },                  // tid[2]
      sizeof(scanCommand) + 1,         // msgLen
      {},                              // remIP
      0,                               // remPort
      SCAN_REQUEST,                    // clientNum
      localConfig.serialAttempts - 1,  // atts
    });
    queueData.push(scanCounter);  // address of the scanned slave
    for (byte i = 0; i < sizeof(scanCommand); i++) {
      queueData.push(scanCommand[i]);
    }
    scanCounter++;
    if (scanCounter == maxSlaves + 1) scanCounter = 0;
  }
  // Optimize queue (prioritize requests from responding slaves) and trigger sending via serial
  if (serialState == IDLE) {  // send new data over serial only if we are not waiting for response
    if (!queueHeaders.isEmpty()) {
      // boolean queueHasRespondingSlaves;  // true if  queue holds at least one request to responding slaves
      // for (byte i = 0; i < queueHeaders.size(); i++) {
      //   if (getSlaveStatus(queueHeaders[i].uid, statOk) == true) {
      //     queueHasRespondingSlaves = true;
      //     break;
      //   } else {
      //     queueHasRespondingSlaves = false;
      //   }
      // }
      serialState = SENDING;  // trigger sendSerial()
    }
  }
}

byte checkRequest(const byte inBuffer[], unsigned int msgLength, const IPAddress remoteIP, const unsigned int remotePort, const byte clientNum) {
  byte addressPos = 6 * !localConfig.enableRtuOverTcp;  // position of slave address in the incoming TCP/UDP message (0 for Modbus RTU over TCP/UDP and 6 for Modbus RTU over TCP/UDP)
  if (localConfig.enableRtuOverTcp) {                   // check CRC for Modbus RTU over TCP/UDP
    if (checkCRC(inBuffer, msgLength) == false) {
      errorInvalid++;
      return 0;  // drop request and do not return an code
    }
  } else {  // check MBAP header structure for Modbus TCP/UDP
    if (inBuffer[2] != 0x00 || inBuffer[3] != 0x00 || inBuffer[4] != 0x00 || inBuffer[5] != msgLength - 6) {
      errorInvalid++;
      return 0;  // drop request and do not return an code
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
    setSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B, true);
    return 0x0B;  // return modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
  } else if (getSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B)) {
    setSlaveStatus(inBuffer[addressPos], STAT_ERROR_0B_QUEUE, true);
  }
  
  // all checkes passed OK, we can store the incoming data in request queue
  // Store in request queue
  queueHeaders.push(header{
    { inBuffer[0], inBuffer[1] },  // tid[2] (ignored in Modbus RTU over TCP/UDP)
    (byte)msgLength,               // msgLen
    (IPAddress)remoteIP,           // remIP
    (unsigned int)remotePort,      // remPort
    (byte)clientNum,               // clientNum
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
  if (queueHeaders.first().clientNum == SCAN_REQUEST) scanInQueue = false;
  for (byte i = 0; i < queueHeaders.first().msgLen; i++) {
    queueData.shift();
  }
  queueHeaders.shift();
}

bool getSlaveStatus(const uint8_t slave, const byte status) {
  if (slave >= maxSlaves) return false;  // error
  return (stat[status][slave / 8] & masks[slave & 7]) > 0;
}

void setSlaveStatus(const uint8_t slave, byte status, const bool value) {
  if (slave >= maxSlaves) return;  // error
  if (value == 0) {
    stat[status][slave / 8] &= ~masks[slave & 7];
  } else {
    for (byte i = 0; i < STAT_NUM; i++) {
      stat[i][slave / 8] &= ~masks[slave & 7];  // set all other flags to false
    }
    stat[status][slave / 8] |= masks[slave & 7];
    errorCount[status]++;
  }
}
