/* *******************************************************************
   Modbus RTU functions

   sendSerial()
   - sends Modbus RTU requests to HW serial port (RS485 interface)

   recvSerial()
   - receives Modbus RTU replies
   - adjusts headers and forward messages as Modbus TCP/UDP or Modbus RTU over TCP/UDP
   - sends Modbus TCP/UDP error messages in case Modbus RTU response timeouts

   checkCRC()
   - checks an array and returns true if CRC is OK

   calculateCRC()

   ***************************************************************** */


void sendSerial() {
  if (!sendMicroTimer.isOver()) {
    return;
  }
  if (queueHeaders.isEmpty()) {
    return;
  }
  static uint16_t txNdx = 0;
  header myHeader = queueHeaders.first();
  switch (serialState) {
    case 0:  // IDLE: Optimize queue (prioritize requests from responding slaves) and trigger sending via serial
      while (priorityReqInQueue && (queueHeaders.first().requestType & PRIORITY_REQUEST) == false) {
        // move requests to non responding slaves to the tail of the queue
        for (byte i = 0; i < queueHeaders.first().msgLen; i++) {
          queueData.push(queueData.shift());
        }
        queueHeaders.push(queueHeaders.shift());
      }
      serialState++;
      break;
    case 1:  // SENDING:
      {
        if (mySerial.availableForWrite() > 0 && txNdx == 0) {
#ifdef RS485_CONTROL_PIN
          digitalWrite(RS485_CONTROL_PIN, RS485_TRANSMIT);  // Enable RS485 Transmit
#endif                                                      /* RS485_CONTROL_PIN */
          crc = 0xFFFF;
        }
        while (mySerial.availableForWrite() > 0 && txNdx < myHeader.msgLen) {
          mySerial.write(queueData[txNdx]);
          calculateCRC(queueData[txNdx]);
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
          mySerial.flush();
#ifdef RS485_CONTROL_PIN
          // sendMicroTimer.sleep(frameDelay());  // Short delay before we toggle the RS485_CONTROL_PIN and disable RS485 transmit. Not needed if we use flush()
#endif /* RS485_CONTROL_PIN */
          serialState++;
        }
      }
      break;
    case 2:  // DELAY:
      {
#ifdef ENABLE_EXTRA_DIAG
        rtuCount[DATA_TX] += myHeader.msgLen;
        rtuCount[DATA_TX] += 2;
#endif
#ifdef RS485_CONTROL_PIN
        digitalWrite(RS485_CONTROL_PIN, RS485_RECEIVE);  // Disable RS485 Transmit
#endif                                                   /* RS485_CONTROL_PIN */
        myHeader.atts++;
        queueHeaders.shift();
        queueHeaders.unshift(myHeader);
        uint32_t delay = localConfig.serialTimeout;
        if (myHeader.requestType & SCAN_REQUEST) delay = SCAN_TIMEOUT;  // fixed timeout for scan requests
        sendMicroTimer.sleep(delay * 1000UL);
        serialState++;
      }
      break;
    case 3:  // WAITING: Deal with Serial timeouts (i.e. Modbus RTU timeouts)
      {
        if (myHeader.requestType & SCAN_REQUEST) {  // Only one attempt for scan request (we do not count attempts)
          deleteRequest();
        } else if (myHeader.atts >= localConfig.serialAttempts) {
          // send modbus error 0x0B (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
          setSlaveStatus(queueData[0], SLAVE_ERROR_0B, true, false);
          byte MBAP[] = { myHeader.tid[0],
                          myHeader.tid[1],
                          0x00,
                          0x00,
                          0x00,
                          0x03 };
          byte PDU[5] = { queueData[0],
                          byte(queueData[1] + 0x80),
                          0x0B };
          crc = 0xFFFF;
          for (byte i = 0; i < 3; i++) {
            calculateCRC(PDU[i]);
          }
          PDU[3] = lowByte(crc);  // send CRC, low byte first
          PDU[4] = highByte(crc);
          sendResponse(MBAP, PDU, 5);
          errorCount[ERROR_TIMEOUT]++;
        } else {
          setSlaveStatus(queueData[0], SLAVE_ERROR_0B_QUEUE, true, false);
          errorCount[ERROR_TIMEOUT]++;
        }                 // if (myHeader.atts >= MAX_RETRY)
        serialState = 0;  // IDLE
      }
      break;
    default:
      break;
  }
}

void recvSerial() {
  static uint16_t rxNdx = 0;
  static byte serialIn[MODBUS_SIZE];
  while (mySerial.available() > 0) {
    byte b = mySerial.read();
    if (rxNdx < MODBUS_SIZE) {
      serialIn[rxNdx] = b;
      rxNdx++;
    }  // if frame longer than maximum allowed, CRC will fail and errorCount[ERROR_RTU] will be recorded down the road
    recvMicroTimer.sleep(charTimeOut());
    sendMicroTimer.sleep(localConfig.frameDelay * 1000UL);  // delay next serial write
  }
  if (recvMicroTimer.isOver() && rxNdx != 0) {
    // Process Serial data
    // Checks: 1) CRC; 2) address of incoming packet against first request in queue; 3) only expected responses are forwarded to TCP/UDP
    header myHeader = queueHeaders.first();
    if (checkCRC(serialIn, rxNdx) == true && serialIn[0] == queueData[0] && serialState == WAITING) {
      if (serialIn[1] > 0x80 && (myHeader.requestType & SCAN_REQUEST) == false) {
        setSlaveStatus(serialIn[0], SLAVE_ERROR_0X, true, false);
      } else {
        setSlaveStatus(serialIn[0], SLAVE_OK, true, myHeader.requestType & SCAN_REQUEST);
      }
      byte MBAP[] = {
        myHeader.tid[0],
        myHeader.tid[1],
        0x00,
        0x00,
        highByte(rxNdx - 2),
        lowByte(rxNdx - 2)
      };
      sendResponse(MBAP, serialIn, rxNdx);
      serialState = IDLE;
    } else {
      errorCount[ERROR_RTU]++;
    }
#ifdef ENABLE_EXTRA_DIAG
    rtuCount[DATA_RX] += rxNdx;
#endif /* ENABLE_EXTRA_DIAG */
    rxNdx = 0;
  }
}

void sendResponse(const byte MBAP[], const byte PDU[], const uint16_t pduLength) {
  header myHeader = queueHeaders.first();
  responseLen = 0;
  while (responseLen < pduLength) {  // include CRC
    if (responseLen < MAX_RESPONSE_LEN) {
      response[responseLen] = PDU[responseLen];
    }
    responseLen++;
  }
  if (myHeader.requestType & UDP_REQUEST) {
    Udp.beginPacket(myHeader.remIP, myHeader.remPort);
    if (localConfig.enableRtuOverTcp) Udp.write(PDU, pduLength);
    else {
      Udp.write(MBAP, 6);
      Udp.write(PDU, pduLength - 2);  //send without CRC
    }
    Udp.endPacket();
#ifdef ENABLE_EXTRA_DIAG
    ethCount[DATA_TX] += pduLength;
    if (!localConfig.enableRtuOverTcp) ethCount[DATA_TX] += 4;
#endif /* ENABLE_EXTRA_DIAG */
  } else if (myHeader.requestType & TCP_REQUEST) {
    byte sock = myHeader.requestType & TCP_REQUEST_MASK;
    EthernetClient client = EthernetClient(sock);
    if (W5100.readSnSR(sock) == SnSR::ESTABLISHED && W5100.readSnDPORT(sock) == myHeader.remPort) {  // Check remote port should be enough or check also rem IP?
      if (localConfig.enableRtuOverTcp) client.write(PDU, pduLength);
      else {
        client.write(MBAP, 6);
        client.write(PDU, pduLength - 2);  //send without CRC
      }
#ifdef ENABLE_EXTRA_DIAG
      ethCount[DATA_TX] += pduLength;
      if (!localConfig.enableRtuOverTcp) ethCount[DATA_TX] += 4;
#endif /* ENABLE_EXTRA_DIAG */
    }  // TODO TCP Connection Error
  }    // else SCAN_REQUEST (no ethCount[DATA_TX], but yes delete request)
  deleteRequest();
}

bool checkCRC(byte buf[], int16_t len) {
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
