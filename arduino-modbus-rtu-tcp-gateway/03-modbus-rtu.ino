/* *******************************************************************
   Modbus RTU functions

   sendSerial
   - sends Modbus RTU requests to HW serial port (RS485 interface)

   recvSerial
   - receives Modbus RTU replies
   - adjusts headers and forward messages as Modbus TCP/UDP or Modbus RTU over TCP/UDP
   - sends Modbus TCP/UDP error messages in case Modbus RTU response timeouts

   checkCRC
   - checks an array and returns true if CRC is OK

   calculateCRC

   ***************************************************************** */

int rxNdx = 0;
int txNdx = 0;
bool rxErr = false;

MicroTimer rxDelay;
MicroTimer rxTimeout;
MicroTimer txDelay;

void sendSerial()
{
  if (serialState == SENDING && rxNdx == 0) {        // avoid bus collision, only send when we are not receiving data
    if (Serial.availableForWrite() > 0 && txNdx == 0 && digitalRead(SerialTxControl) == RS485Receive) {
      digitalWrite(SerialTxControl, RS485Transmit);           // Enable RS485 Transmit
      crc = 0xFFFF;
      Serial.write(queueHeaders.first().uid);        // send uid (address)
      calculateCRC(queueHeaders.first().uid);
    }
    while (Serial.availableForWrite() > 0 && txNdx < queueHeaders.first().PDUlen && digitalRead(SerialTxControl) == RS485Transmit) {
      Serial.write(queuePDUs[txNdx]);                // send func and data
      calculateCRC(queuePDUs[txNdx]);
      txNdx++;
    }
    if (Serial.availableForWrite() > 1 && txNdx == queueHeaders.first().PDUlen) {
      // In Modbus TCP mode we must add CRC (in Modbus RTU over TCP, CRC is already in queuePDUs)
      if (!localConfig.enableRtuOverTcp || queueHeaders.first().clientNum == SCAN_REQUEST) {
        Serial.write(lowByte(crc));                            // send CRC, low byte first
        Serial.write(highByte(crc));
      }
      txNdx++;
    }
    if (Serial.availableForWrite() == SERIAL_TX_BUFFER_SIZE - 1 && txNdx > queueHeaders.first().PDUlen) {
      // wait for last byte (incl. CRC) to be sent from serial Tx buffer
      // this if statement is not very reliable (too fast)
      // Serial.isFlushed() method is needed....see https://github.com/arduino/Arduino/pull/3737
      txNdx = 0;
      txDelay.sleep(frameDelay);
      serialState = DELAY;
    }
  } else if (serialState == DELAY && txDelay.isOver()) {
    serialTxCount += queueHeaders.first().PDUlen + 1;    // in Modbus RTU over TCP, queuePDUs already contains CRC
    if (!localConfig.enableRtuOverTcp) serialTxCount += 2;  // in Modbus TCP, add 2 bytes for CRC
    digitalWrite(SerialTxControl, RS485Receive);                                    // Disable RS485 Transmit
    if (queueHeaders.first().uid == 0x00) {           // Modbus broadcast - we do not count attempts and delete immediatelly
      serialState = IDLE;
      deleteRequest();
    } else {
      serialState = WAITING;
      requestTimeout.sleep(localConfig.serialTimeout);          // delays next serial write
      queueRetries.unshift(queueRetries.shift() + 1);
    }
  }
}

void recvSerial()
{
  static byte serialIn[modbusSize];
  while (Serial.available() > 0) {
    if (rxTimeout.isOver() && rxNdx != 0) {
      rxErr = true;       // character timeout
    }
    if (rxNdx < modbusSize) {
      serialIn[rxNdx] = Serial.read();
      rxNdx++;
    } else {
      Serial.read();
      rxErr = true;       // frame longer than maximum allowed
    }
    rxDelay.sleep(frameDelay);
    rxTimeout.sleep(charTimeout);
  }
  if (rxDelay.isOver() && rxNdx != 0) {

    // Process Serial data
    // Checks: 1) RTU frame is without errors; 2) CRC; 3) address of incoming packet against first request in queue; 4) only expected responses are forwarded to TCP/UDP
    if (!rxErr && checkCRC(serialIn, rxNdx) == true && serialIn[0] == queueHeaders.first().uid && serialState == WAITING) {
      slavesResponding[serialIn[0]] = true;               // flag slave as responding
      byte MBAP[] = {queueHeaders.first().tid[0], queueHeaders.first().tid[1], 0x00, 0x00, highByte(rxNdx - 2), lowByte(rxNdx - 2)};
      if (queueHeaders.first().clientNum == UDP_REQUEST) {
        Udp.beginPacket(queueHeaders.first().remIP, queueHeaders.first().remPort);
        if (localConfig.enableRtuOverTcp) Udp.write(serialIn, rxNdx);
        else {
          Udp.write(MBAP, 6);
          Udp.write(serialIn, rxNdx - 2);      //send without CRC
        }
        Udp.endPacket();
        ethTxCount += rxNdx;
        if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
      } else if (queueHeaders.first().clientNum != SCAN_REQUEST) {
        EthernetClient client = EthernetClient(queueHeaders.first().clientNum);
        // make sure that this is really our socket
        if (Ethernet._server_port[queueHeaders.first().clientNum] == localConfig.tcpPort && (client.status() == SnSR::ESTABLISHED || client.status() == SnSR::CLOSE_WAIT)) {
          if (localConfig.enableRtuOverTcp) client.write(serialIn, rxNdx);
          else {
            client.write(MBAP, 6);
            client.write(serialIn, rxNdx - 2);      //send without CRC
          }
          client.stop();
          ethTxCount += rxNdx;
          if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
        }
      }
      deleteRequest();
      serialState = IDLE;
    }
    serialRxCount += rxNdx;
    rxNdx = 0;
    rxErr = false;
  }

  // Deal with Serial timeouts (i.e. Modbus RTU timeouts)
  if (serialState == WAITING && requestTimeout.isOver()) {
    slavesResponding[queueHeaders.first().uid] = false;     // flag slave as nonresponding
    if (queueRetries.first() >= localConfig.serialRetry) {
      // send modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
      byte MBAP[] = {queueHeaders.first().tid[0], queueHeaders.first().tid[1], 0x00, 0x00, 0x00, 0x03};
      byte PDU[] = {queueHeaders.first().uid, queuePDUs[0] + 0x80, 0x0B};
      crc = 0xFFFF;
      for (byte i = 0; i < sizeof(PDU); i++) {
        calculateCRC(PDU[i]);
      }
      if (queueHeaders.first().clientNum == UDP_REQUEST) {
        Udp.beginPacket(queueHeaders.first().remIP, queueHeaders.first().remPort);
        if (!localConfig.enableRtuOverTcp) {
          Udp.write(MBAP, 6);
        }
        Udp.write(PDU, 3);
        if (localConfig.enableRtuOverTcp) {
          Udp.write(lowByte(crc));        // send CRC, low byte first
          Udp.write(highByte(crc));
        }
        Udp.endPacket();
        ethTxCount += 5;
        if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
      } else {
        EthernetClient client = EthernetClient(queueHeaders.first().clientNum);
        // make sure that this is really our socket
        if (Ethernet._server_port[queueHeaders.first().clientNum] == localConfig.tcpPort && (client.status() == SnSR::ESTABLISHED || client.status() == SnSR::CLOSE_WAIT)) {
          if (!localConfig.enableRtuOverTcp) {
            client.write(MBAP, 6);
          }
          client.write(PDU, 3);
          if (localConfig.enableRtuOverTcp) {
            client.write(lowByte(crc));        // send CRC, low byte first
            client.write(highByte(crc));
          }
          client.stop();
          ethTxCount += 5;
          if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
        }
      }
      deleteRequest();
    }            // if (queueRetries.first() >= MAX_RETRY)
    serialState = IDLE;
  }              // if (requestTimeout.isOver() && expectingData == true)
}

bool checkCRC(byte buf[], int len)
{
  crc = 0xFFFF;
  for (byte i = 0; i < len - 2; i++) {
    calculateCRC(buf[i]);
  }
  if (highByte(crc) == buf[len - 1] && lowByte(crc) == buf[len - 2]) {
    return true;
  } else {
    return false;
  }
}

void calculateCRC(byte b)
{
  crc ^= (uint16_t)b;          // XOR byte into least sig. byte of crc
  for (byte i = 8; i != 0; i--) {    // Loop over each bit
    if ((crc & 0x0001) != 0) {      // If the LSB is set
      crc >>= 1;                    // Shift right and XOR 0xA001
      crc ^= 0xA001;
    }
    else                            // Else LSB is not set
      crc >>= 1;                    // Just shift right
  }
  // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
}
