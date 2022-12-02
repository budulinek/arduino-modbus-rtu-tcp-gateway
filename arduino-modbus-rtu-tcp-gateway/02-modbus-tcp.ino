/* *******************************************************************
   Modbus TCP/UDP functions

   recvUdp
   - receives Modbus UDP (or Modbus RTU over UDP) messages
   - calls checkRequest
   - stores requests in queue or replies with error

   recvTcp
   - receives Modbus TCP (or Modbus RTU over TCP) messages
   - calls checkRequest
   - stores requests in queue or replies with error

   processRequests
   - inserts scan request into queue
   - optimizes queue

   checkRequest
   - checks Modbus TCP/UDP requests (correct MBAP header, CRC in case of Modbus RTU over TCP/UDP)
   - checks availability of queue

   deleteRequest
   - deletes requests from queue

   getSlaveResponding, setSlaveResponding
   - read from and write to bool array

   ***************************************************************** */

// bool array for storing Modbus RTU status (responging or not responding). Array index corresponds to slave address.
uint8_t slavesResponding[(maxSlaves + 1 + 7) / 8];
uint8_t masks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

typedef struct {
  byte tid[2];           // MBAP Transaction ID
  byte uid;              // MBAP Unit ID (address)
  byte PDUlen;           // lenght of PDU (func + data) stored in queuePDUs
  IPAddress remIP;       // remote IP for UDP client (UDP response is sent back to remote IP)
  unsigned int remPort;  // remote port for UDP client (UDP response is sent back to remote port)
  byte clientNum;        // TCP client who sent the request, UDP_REQUEST (0xFF) designates UDP client
} header;

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, reqQueueCount> queueHeaders;  // queue of requests' headers and metadata (MBAP transaction ID, MBAP unit ID, PDU length, remIP, remPort, TCP client)
CircularBuffer<byte, reqQueueSize> queuePDUs;        // queue of PDU data (function code, data)
CircularBuffer<byte, reqQueueCount> queueRetries;    // queue of retry counters

void recvUdp() {
  unsigned int packetSize = Udp.parsePacket();
  if (packetSize) {
#ifdef ENABLE_EXTRA_DIAG
    ethRxCount += packetSize;
#endif /* ENABLE_EXTRA_DIAG */
    byte udpInBuffer[modbusSize + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
    // Modbus TCP/UDP frame: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address).....
    // Modbus RTU frame: [0] address.....
    Udp.read(udpInBuffer, sizeof(udpInBuffer));
    Udp.flush();

    byte errorCode = checkRequest(udpInBuffer, packetSize);
    byte pduStart;                                   // first byte of Protocol Data Unit (i.e. Function code)
    if (localConfig.enableRtuOverTcp) pduStart = 1;  // In Modbus RTU, Function code is second byte (after address)
    else pduStart = 7;                               // In Modbus TCP/UDP, Function code is 8th byte (after address)
    if (errorCode == 0) {
      // Store in request queue: 2 bytes MBAP Transaction ID (ignored in Modbus RTU over TCP); MBAP Unit ID (address); PDUlen (func + data);remote IP; remote port; TCP client Number (socket) - 0xFF for UDP
      queueHeaders.push(header{ { udpInBuffer[0], udpInBuffer[1] }, udpInBuffer[pduStart - 1], (byte)(packetSize - pduStart), Udp.remoteIP(), Udp.remotePort(), UDP_REQUEST });
      queueRetries.push(0);
      for (byte i = 0; i < (byte)(packetSize - pduStart); i++) {
        queuePDUs.push(udpInBuffer[i + pduStart]);
      }
    } else if (errorCode != 0xFF) {
      // send back message with error code
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      if (!localConfig.enableRtuOverTcp) {
        Udp.write(udpInBuffer, 5);
        Udp.write(0x03);
      }
      Udp.write(udpInBuffer[pduStart - 1]);     // address
      Udp.write(udpInBuffer[pduStart] + 0x80);  // function + 0x80
      Udp.write(errorCode);
      if (localConfig.enableRtuOverTcp) {
        crc = 0xFFFF;
        calculateCRC(udpInBuffer[pduStart - 1]);
        calculateCRC(udpInBuffer[pduStart] + 0x80);
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
    unsigned int packetSize = client.available();
#ifdef ENABLE_EXTRA_DIAG
    ethRxCount += packetSize;
#endif /* ENABLE_EXTRA_DIAG */
    byte tcpInBuffer[modbusSize + 4];  // Modbus TCP frame is 4 bytes longer than Modbus RTU frame
    // Modbus TCP/UDP frame: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address).....
    // Modbus RTU frame: [0] address.....
    client.read(tcpInBuffer, sizeof(tcpInBuffer));
    client.flush();
    byte errorCode = checkRequest(tcpInBuffer, packetSize);
    byte pduStart;                                   // first byte of Protocol Data Unit (i.e. Function code)
    if (localConfig.enableRtuOverTcp) pduStart = 1;  // In Modbus RTU, Function code is second byte (after address)
    else pduStart = 7;                               // In Modbus TCP/UDP, Function code is 8th byte (after address)
    if (errorCode == 0) {
      // Store in request queue: 2 bytes MBAP Transaction ID (ignored in Modbus RTU over TCP); MBAP Unit ID (address); PDUlen (func + data);remote IP; remote port; TCP client Number (socket) - 0xFF for UDP
      queueHeaders.push(header{ { tcpInBuffer[0], tcpInBuffer[1] }, tcpInBuffer[pduStart - 1], (byte)(packetSize - pduStart), {}, 0, client.getSocketNumber() });
      queueRetries.push(0);
      for (byte i = 0; i < packetSize - pduStart; i++) {
        queuePDUs.push(tcpInBuffer[i + pduStart]);
      }
    } else if (errorCode != 0xFF) {
      // send back message with error code
      if (!localConfig.enableRtuOverTcp) {
        client.write(tcpInBuffer, 5);
        client.write(0x03);
      }
      client.write(tcpInBuffer[pduStart - 1]);     // address
      client.write(tcpInBuffer[pduStart] + 0x80);  // function + 0x80
      client.write(errorCode);
      if (localConfig.enableRtuOverTcp) {
        crc = 0xFFFF;
        calculateCRC(tcpInBuffer[pduStart - 1]);
        calculateCRC(tcpInBuffer[pduStart] + 0x80);
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
  // Insert scan request into queue
  if (scanCounter != 0 && queueHeaders.available() > 1 && queuePDUs.available() > 1) {
    // Store scan request in request queue
    queueHeaders.push(header{ { 0x00, 0x00 }, scanCounter, sizeof(scanCommand), {}, 0, SCAN_REQUEST });
    queueRetries.push(localConfig.serialAttempts - 1);  // scan requests are only sent once, so set "queueRetries" to one attempt below limit
    for (byte i = 0; i < sizeof(scanCommand); i++) {
      queuePDUs.push(scanCommand[i]);
    }
    scanCounter++;
    if (scanCounter == maxSlaves + 1) scanCounter = 0;
  }

  // Optimize queue (prioritize requests from responding slaves) and trigger sending via serial
  if (serialState == IDLE) {  // send new data over serial only if we are not waiting for response
    if (!queueHeaders.isEmpty()) {
      boolean queueHasRespondingSlaves;  // true if  queue holds at least one request to responding slaves
      for (byte i = 0; i < queueHeaders.size(); i++) {
        if (getSlaveResponding(queueHeaders[i].uid) == true) {
          queueHasRespondingSlaves = true;
          break;
        } else {
          queueHasRespondingSlaves = false;
        }
      }
      serialState = SENDING;  // trigger sendSerial()
    }
  }
}

byte checkRequest(byte buffer[], unsigned int bufferSize) {
  byte address;
  if (localConfig.enableRtuOverTcp) address = buffer[0];
  else address = buffer[6];

  if (localConfig.enableRtuOverTcp) {  // check CRC for Modbus RTU over TCP/UDP
    if (checkCRC(buffer, bufferSize) == false) {
      return 0xFF;  // reject: do nothing and return no error code
    }
  } else {  // check MBAP header structure for Modbus TCP/UDP
    if (buffer[2] != 0x00 || buffer[3] != 0x00 || buffer[4] != 0x00 || buffer[5] != bufferSize - 6) {
      return 0xFF;  // reject: do nothing and return no error code
    }
  }
  // check if we have space in request queue
  if (queueHeaders.available() < 1 || (localConfig.enableRtuOverTcp && queuePDUs.available() < bufferSize - 1) || (!localConfig.enableRtuOverTcp && queuePDUs.available() < bufferSize - 7)) {
    return 0x06;  // return modbus error 6 (Slave Device Busy) - try again later
  }
  // al checkes passed OK, we can store the incoming data in request queue
  return 0;
}

void deleteRequest()  // delete request from queue
{
  for (byte i = 0; i < queueHeaders.first().PDUlen; i++) {
    queuePDUs.shift();
  }
  queueHeaders.shift();
  queueRetries.shift();
}


bool getSlaveResponding(const uint8_t index) {
  if (index >= maxSlaves) return false;  // error
  return (slavesResponding[index / 8] & masks[index & 7]) > 0;
}


void setSlaveResponding(const uint8_t index, const bool value) {
  if (index >= maxSlaves) return;  // error
  if (value == 0) slavesResponding[index / 8] &= ~masks[index & 7];
  else slavesResponding[index / 8] |= masks[index & 7];
}
