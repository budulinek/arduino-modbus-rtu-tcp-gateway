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

void sendSerial() {
  if (serialState == SENDING && rxNdx == 0) {  // avoid bus collision, only send when we are not receiving data
    header myHeader = queueHeaders.first();
    if (mySerial.availableForWrite() > 0 && txNdx == 0) {
#ifdef RS485_CONTROL_PIN
      digitalWrite(RS485_CONTROL_PIN, RS485_TRANSMIT);  // Enable RS485 Transmit
#endif                                                  /* RS485_CONTROL_PIN */
      crc = 0xFFFF;
    }
    while (mySerial.availableForWrite() > 0 && txNdx < myHeader.msgLen) {
      mySerial.write(queuePDUs[txNdx]);
      calculateCRC(queuePDUs[txNdx]);
      txNdx++;
    }
    if (mySerial.availableForWrite() > 1 && txNdx == myHeader.msgLen) {
      mySerial.write(lowByte(crc));  // send CRC, low byte first
      mySerial.write(highByte(crc));
      txNdx++;
    }
    if (mySerial.availableForWrite() == SERIAL_TX_BUFFER_SIZE - 1 && txNdx > myHeader.msgLen) {
      // wait for last byte (incl. CRC) to be sent from serial Tx buffer
      // this if statement is not very reliable (too fast)
      // Serial.isFlushed() method is needed....see https://github.com/arduino/Arduino/pull/3737
      txNdx = 0;
      txDelay.sleep(frameDelay);
      serialState = DELAY;
    }
  } else if (serialState == DELAY && txDelay.isOver()) {
    header myHeader = queueHeaders.first();
    serialTxCount += myHeader.msgLen;
    serialTxCount += 2;
#ifdef RS485_CONTROL_PIN
    digitalWrite(RS485_CONTROL_PIN, RS485_RECEIVE);  // Disable RS485 Transmit
#endif                                               /* RS485_CONTROL_PIN */
    if (queuePDUs[0] == 0x00) {                      // Modbus broadcast - we do not count attempts and delete immediatelly
      serialState = IDLE;
      deleteRequest();
    } else {
      serialState = WAITING;
      requestTimeout.sleep(localConfig.serialTimeout);  // delays next serial write
      myHeader.atts++;
      queueHeaders.shift();
      queueHeaders.unshift(myHeader);
    }
  }
}

void recvSerial() {
  static byte serialIn[modbusSize];
  while (mySerial.available() > 0) {
    if (rxTimeout.isOver() && rxNdx != 0) {
      rxErr = true;  // character timeout
    }
    if (rxNdx < modbusSize) {
      serialIn[rxNdx] = mySerial.read();
      rxNdx++;
    } else {
      mySerial.read();
      rxErr = true;  // frame longer than maximum allowed
    }
    rxDelay.sleep(frameDelay);
    rxTimeout.sleep(charTimeout);
  }
  if (rxDelay.isOver() && rxNdx != 0) {
    // Process Serial data
    // Checks: 1) RTU frame is without errors; 2) CRC; 3) address of incoming packet against first request in queue; 4) only expected responses are forwarded to TCP/UDP
    header myHeader = queueHeaders.first();
    if (!rxErr && checkCRC(serialIn, rxNdx) == true && serialIn[0] == queuePDUs[0] && serialState == WAITING) {
      if (serialIn[1] > 0x80 && myHeader.clientNum != SCAN_REQUEST) {
        setSlaveStatus(serialIn[0], STAT_ERROR_0X, true);
      } else {
        setSlaveStatus(serialIn[0], STAT_OK, true);
      }
      byte MBAP[] = { myHeader.tid[0], myHeader.tid[1], 0x00, 0x00, highByte(rxNdx - 2), lowByte(rxNdx - 2) };
      if (myHeader.clientNum == UDP_REQUEST) {
        Udp.beginPacket(myHeader.remIP, myHeader.remPort);
        if (localConfig.enableRtuOverTcp) Udp.write(serialIn, rxNdx);
        else {
          Udp.write(MBAP, 6);
          Udp.write(serialIn, rxNdx - 2);  //send without CRC
        }
        Udp.endPacket();
#ifdef ENABLE_EXTRA_DIAG
        ethTxCount += rxNdx;
        if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
      } else if (myHeader.clientNum != SCAN_REQUEST) {
        EthernetClient client = EthernetClient(myHeader.clientNum);
        if (client.connected()) {
          if (localConfig.enableRtuOverTcp) client.write(serialIn, rxNdx);
          else {
            client.write(MBAP, 6);
            client.write(serialIn, rxNdx - 2);  //send without CRC
          }
#ifdef ENABLE_EXTRA_DIAG
          ethTxCount += rxNdx;
          if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
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
    header myHeader = queueHeaders.first();
    if (myHeader.atts >= localConfig.serialAttempts) {
      if (myHeader.clientNum != SCAN_REQUEST) {
        // send modbus error 0x0B (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
        setSlaveStatus(queuePDUs[0], STAT_ERROR_0B, true);
        byte MBAP[] = { myHeader.tid[0], myHeader.tid[1], 0x00, 0x00, 0x00, 0x03 };
        byte PDU[] = { queuePDUs[0], (byte)(queuePDUs[1] + 0x80), 0x0B };
        crc = 0xFFFF;
        for (byte i = 0; i < sizeof(PDU); i++) {
          calculateCRC(PDU[i]);
        }
        switch (myHeader.clientNum) {
          case SCAN_REQUEST:
            break;
          case UDP_REQUEST:
            Udp.beginPacket(myHeader.remIP, myHeader.remPort);
            if (!localConfig.enableRtuOverTcp) {
              Udp.write(MBAP, 6);
            }
            Udp.write(PDU, 3);
            if (localConfig.enableRtuOverTcp) {
              Udp.write(lowByte(crc));  // send CRC, low byte first
              Udp.write(highByte(crc));
            }
            Udp.endPacket();
#ifdef ENABLE_EXTRA_DIAG
            ethTxCount += 5;
            if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
            break;
          default:  // Ethernet client
            {
              EthernetClient client = EthernetClient(myHeader.clientNum);
              if (client.connected()) {
                if (!localConfig.enableRtuOverTcp) {
                  client.write(MBAP, 6);
                }
                client.write(PDU, 3);
                if (localConfig.enableRtuOverTcp) {
                  client.write(lowByte(crc));  // send CRC, low byte first
                  client.write(highByte(crc));
                }
#ifdef ENABLE_EXTRA_DIAG
                ethTxCount += 5;
                if (!localConfig.enableRtuOverTcp) ethTxCount += 4;
#endif /* ENABLE_EXTRA_DIAG */
              }
              break;
            }
        }
      }
      deleteRequest();
    }  // if (myHeader.atts >= MAX_RETRY)
    serialState = IDLE;
  }  // if (requestTimeout.isOver() && expectingData == true)
}

bool checkCRC(byte buf[], int len) {
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

void calculateCRC(byte b) {
  crc ^= (uint16_t)b;              // XOR byte into least sig. byte of crc
  for (byte i = 8; i != 0; i--) {  // Loop over each bit
    if ((crc & 0x0001) != 0) {     // If the LSB is set
      crc >>= 1;                   // Shift right and XOR 0xA001
      crc ^= 0xA001;
    } else        // Else LSB is not set
      crc >>= 1;  // Just shift right
  }
  // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
}
