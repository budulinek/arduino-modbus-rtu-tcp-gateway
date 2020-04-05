/* Transparent gateway Modbus RTU <---> Modbus TCP / Modbus UDP 
   - supports up to 247 Modbus RTU slaves
   - supports Modbus UDP masters and up to 8 Modbus TCP masters connected at the same time
   - supports broadcast (slave address 0x00) and error codes
   - supports all Modbus function codes
   - optimized queue for Modbus requests 
            o prioritization of requests to responding slaves
            o queue will accept only one requests to non-responding slaves

  Connections:
  Arduino <-> MAX485
  Tx1 <-> DI
  Rx0 <-> RO
  Pin 6 <-> DE,RE


*/


#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <CircularBuffer.h>
#include <BitBool.h>

// RS485 settings
#define BAUD 9600
#define SERIAL_CONFIG SERIAL_8N2              // sets data, parity, and stop bits (for valid values see https://www.arduino.cc/reference/en/language/functions/communication/serial/begin/)
//                                            // Modbus standard is SERIAL_8N2
#define SERIAL_TIMEOUT 200                    // timeout for Modbus RTU request (ms)
#define MAX_RETRY 5                          // maximum number of retries for sending Modbus RTU request

// TCP and UDP settings
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED  };     // MAC - change to something more random...
IPAddress ip(192, 168, 1, 10);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
#define TCP_PORT 502                                    // local Modbus TCP port, standard is 502
#define ETH_MAX_RETRY 1                                    // maximum number of retries for sending UDP packet / TCP response (set to 1 for maximum speed)
//                                                         // for more info see https://www.arduino.cc/en/Reference/EthernetSetRetransmissionCount
// #define UDP_PORT 503                                  // OPTIONAL: local Modbus UDP port, defaults to Modbus TCP port



// Advanced settings
#define REQUESTS_QUEUE 15                                       // max number of TCP or UDP requests stored in queue
#define REQUESTS_QUEUE_BYTES 512                                // max length of TCP or UDP requests stored in queue (in bytes)
#define MAX_SLAVES 256              // max number of Modbus slaves (Modbus supports up to 247 slaves, the rest is for including broadcast and reserved addresses
#define BUFFER_SIZE 256             // serial input (Rx) buffer size (Modbus message size is max 256 bytes)
#define SerialTxControl 6           // Arduino Pin for RS485 Direction control
#define ethResetPin      7          // OPTIONAL: Ethernet shield reset pin (deals with power on reset issue of the ethernet shield)


#define UDP_TX_PACKET_MAX_SIZE BUFFER_SIZE

typedef struct {
  byte buf[BUFFER_SIZE];
  int len;
} buffer;

buffer serialIn;

int rxNdx = 0;
int txNdx = 0;
boolean newData = false;
boolean expectingData = false;
boolean sendingData = false;
boolean txDelayRunning = false;
uint16_t crc;

BitBool<MAX_SLAVES> slavesResponding;

typedef struct {
  byte tid[2];            // MBAP Transaction ID
  byte uid;               // MBAP Unit ID (address)
  byte PDUlen;            // lenght of PDU (func + data) stored in queuePDUs
  IPAddress remIP;        // remote IP for UDP client (UDP response is sent back to remote IP)
  unsigned int remPort;   // remote port for UDP client (UDP response is sent back to remote port)
  byte clientNum;         // TCP client who sent the request, 0xFF designates UDP client
} header;

// each request is stored in 3 queues (all queues are written to, read and deleted in sync)
CircularBuffer<header, REQUESTS_QUEUE> queueHeaders;            // queue of requests' headers and metadata (MBAP transaction ID, MBAP unit ID, PDU length, remIP, remPort, TCP client)
CircularBuffer<byte, REQUESTS_QUEUE_BYTES> queuePDUs;           // queue of PDU data (function code, data)
CircularBuffer<byte, REQUESTS_QUEUE> queueRetries;              // queue of retry counters

EthernetUDP Udp;

EthernetServer server(TCP_PORT);
EthernetClient clients[8];

/*-----( Declare Constants and Pin Numbers )-----*/

#define RS485Transmit    HIGH
#define RS485Receive     LOW

class MicroTimer {
  private:
    unsigned long timestampLastHitMs;
    unsigned long sleepTimeMs;
  public:
    boolean isOver();
    void start(unsigned long sleepTimeMs);
};

boolean MicroTimer::isOver() {
  if (micros() - timestampLastHitMs < sleepTimeMs) {
    return false;
  }
  timestampLastHitMs = micros();
  return true;
}

void MicroTimer::start(unsigned long sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = micros();
}

class Timer {
  private:
    unsigned long timestampLastHitMs;
    unsigned long sleepTimeMs;
  public:
    boolean isOver();
    void start(unsigned long sleepTimeMs);
};

boolean Timer::isOver() {
  if (millis() - timestampLastHitMs < sleepTimeMs) {
    return false;
  }
  timestampLastHitMs = millis();
  return true;
}

void Timer::start(unsigned long sleepTimeMs) {
  this->sleepTimeMs = sleepTimeMs;
  timestampLastHitMs = millis();
}

Timer requestTimeout;
MicroTimer rxDelay;
MicroTimer txDelay;

const int T = 11000000L / BAUD;       // time to send 1 character over serial in microseconds

void setup()   /****** SETUP: RUNS ONCE ******/
{
  Serial.begin(BAUD, SERIAL_CONFIG);

  pinMode(SerialTxControl, OUTPUT);
  digitalWrite(SerialTxControl, RS485Receive);  // Init Transceiver



#ifdef ethResetPin
  pinMode(ethResetPin, OUTPUT);
  digitalWrite(ethResetPin, LOW);
  delay(25);
  digitalWrite(ethResetPin, HIGH);
  delay(500);
  pinMode(ethResetPin, INPUT);
  delay(500);
#endif

  Ethernet.begin(mac, ip);
  Ethernet.setRetransmissionTimeout(20);          // speed up ethernet
  Ethernet.setRetransmissionCount(ETH_MAX_RETRY);

#ifdef UDP_PORT
  Udp.begin(UDP_PORT);
#else
  Udp.begin(TCP_PORT);
#endif
  server.begin();
}


void loop()
{
  recvUdp();
  recvTcp();
  processUdpTcp();
  sendSerial();
  recvSerial();
  processSerial();
}

void recvUdp()
{
  if (Udp.parsePacket())
  {
    byte tempMBAP[7];                     // read MBAP: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address)
    for (byte i = 0; i < 7; i++) {
      tempMBAP[i] = Udp.read();
    }
    // check MBAP header: protocol ID and high byte of length should be 0x00 (max length of PDU is 253 bytes) and low byte of length should match length of data AFTER address
    if (tempMBAP[2] == 0x00 && tempMBAP[3] == 0x00 && tempMBAP[4] == 0x00 && tempMBAP[5] == Udp.available() + 1 && Udp.available() < 255) {

      if (queueHeaders.isEmpty() == false && slavesResponding[tempMBAP[6]] == false) {                       // allow only one request to non responding slaves
        for (byte j = queueHeaders.size(); j > 0 ; j--) {                              // requests to non-responsive slaves are usually towards the tail of the queue
          if (queueHeaders[j - 1].uid == tempMBAP[6]) {
            byte tempFunc = Udp.read();
            while (Udp.available()) {
              Udp.read();
            }
            Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
            for (byte k = 0; k < 5; k++) {
              Udp.write(tempMBAP[k]);
            }
            Udp.write(0x03);
            Udp.write(tempMBAP[6]);
            Udp.write(tempFunc + 0x80);
            Udp.write(0x0B);            // send modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
            Udp.endPacket();
            return;
          }
        }
      }
      if (queueHeaders.available() >= 1 && queuePDUs.available() >= tempMBAP[5]) {        // check if queue is not full
        // Store in request queue
        queueHeaders.push(header {{tempMBAP[0], tempMBAP[1]}, tempMBAP[6], tempMBAP[5] - 1, Udp.remoteIP(), Udp.remotePort(), 0xFF});
        queueRetries.push(1);
        while (Udp.available()) {
          queuePDUs.push(Udp.read());
        }
      } else {
        // MBAP header is OK, but queue is full: send modbus error 6 (Slave Device Busy) - try again later
        byte tempFunc = Udp.read();
        while (Udp.available()) {
          Udp.read();
        }
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        for (byte i = 0; i < 5; i++) {
          Udp.write(tempMBAP[i]);
        }
        Udp.write(0x03);
        Udp.write(tempMBAP[6]);
        Udp.write(tempFunc + 0x80);
        Udp.write(0x06);      // modbus error 6 (Slave Device Busy) - try again later
        Udp.endPacket();
      }
    } else {
      // not a correct MBAP header: do nothing
      while (Udp.available()) {
        Udp.read();
      }
    }
  }
}


void recvTcp()
{
  // store new client into our list of clients
  EthernetClient newClient = server.accept();
  if (newClient) {
    for (byte i = 0; i < 8; i++) {
      if (!clients[i]) {
        clients[i] = newClient;
        Serial.println(i);
        break;
      }
    }
  }
  for (byte i = 0; i < 8; i++) {
    while (clients[i] && clients[i].available() > 0) {
      byte tempMBAP[7];                     // read MBAP: [0][1] transaction ID, [2][3] protocol ID, [4][5] length and [6] unit ID (address)
      for (byte b = 0; b < 7; b++) {
        tempMBAP[b] = clients[i].read();
      }
      // check MBAP header: protocol ID and high byte of length should be 0x00 (max length of PDU is 253 bytes) and low byte of length should match length of data AFTER address
      if (tempMBAP[2] == 0x00 && tempMBAP[3] == 0x00 && tempMBAP[4] == 0x00 && tempMBAP[5] == clients[i].available() + 1 && clients[i].available() < 255) {
        if (queueHeaders.isEmpty() == false && slavesResponding[tempMBAP[6]] == false) {                       // allow only one request to non responding slaves
          for (byte j = queueHeaders.size(); j > 0 ; j--) {                              // requests to non-responsive slaves are usually towards the tail of the queue
            if (queueHeaders[j - 1].uid == tempMBAP[6]) {
              byte tempFunc = clients[i].read();
              while (clients[i].available()) {
                clients[i].read();
              }
              for (byte k = 0; k < 5; k++) {
                clients[i].write(tempMBAP[k]);
              }
              clients[i].write(0x03);
              clients[i].write(tempMBAP[6]);
              clients[i].write(tempFunc + 0x80);
              clients[i].write(0x0B);            // send modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
              return;
            }
          }
        }
        // check if queue is not full and allow only one request to non responding slaves
        if (queueHeaders.available() >= 1 && queuePDUs.available() >= tempMBAP[5]) {
          // Store in request queue
          queueHeaders.push(header {{tempMBAP[0], tempMBAP[1]}, tempMBAP[6], tempMBAP[5] - 1, {}, 0, i});
          queueRetries.push(1);
          while (clients[i].available()) {
            queuePDUs.push(clients[i].read());
          }
        } else {
          // MBAP header is OK, but queue is full: send modbus error 6 (Slave Device Busy) - try again later
          byte tempFunc = clients[i].read();
          while (clients[i].available()) {
            clients[i].read();
          }
          for (byte b = 0; b < 5; b++) {
            clients[i].write(tempMBAP[b]);
          }
          clients[i].write(0x03);
          clients[i].write(tempMBAP[6]);
          clients[i].write(tempFunc + 0x80);
          clients[i].write(0x06);      // modbus error 6 (Slave Device Busy) - try again later
        }
      } else {
        // not a correct MBAP header: do nothing
        while (clients[i].available()) {
          clients[i].read();
        }
      }
    }         // while (clients[i] && clients[i].available() > 0)
  }           // for (byte i = 0; i < 8; i++)
  // stop any clients which disconnect
  for (byte i = 0; i < 8; i++) {
    if (clients[i] && !clients[i].connected()) {
      clients[i].stop();
    }
  }
}

void processUdpTcp()
{
  if (expectingData == false && sendingData == false) {               // send new data over serial only if we are not waiting for response
    if (!queueHeaders.isEmpty()) {
      boolean queueHasRespondingSlaves;               // true if  queue holds at least one request to responding slaves
      for (byte i = 0; i < queueHeaders.size(); i++) {
        if (slavesResponding[queueHeaders[i].uid] == true) {
          queueHasRespondingSlaves = true;
          break;
        } else {
          queueHasRespondingSlaves = false;
        }
      }
      while (queueHasRespondingSlaves == true && slavesResponding[queueHeaders.first().uid] == false) {
        // move requests to non responding slaves to the tail of the queue
        for (byte i = 0; i < queueHeaders.first().PDUlen; i++) {
          queuePDUs.push(queuePDUs.shift());
        }
        queueRetries.push(queueRetries.shift());
        queueHeaders.push(queueHeaders.shift());
      }
      sendingData = true;                   // trigger sendSerial()
    }
  }
  if (requestTimeout.isOver() && expectingData == true) {
    slavesResponding[queueHeaders.first().uid] = false;     // flag slave as nonresponding
    if (queueRetries.first() >= MAX_RETRY + 1) {
      // send modbus error 11 (Gateway Target Device Failed to Respond) - usually means that target device (address) is not present
      if (queueHeaders.first().clientNum == 0xFF) {
        Udp.beginPacket(queueHeaders.first().remIP, queueHeaders.first().remPort);
        Udp.write(queueHeaders.first().tid[0]);
        Udp.write(queueHeaders.first().tid[1]);
        Udp.write((byte)0x00);
        Udp.write((byte)0x00);
        Udp.write((byte)0x00);
        Udp.write(0x03);
        Udp.write(queueHeaders.first().uid);
        Udp.write(queuePDUs[0] + 0x80);
        Udp.write(0x0B);
        Udp.endPacket();
      } else {
        clients[queueHeaders.first().clientNum].write(queueHeaders.first().tid[0]);
        clients[queueHeaders.first().clientNum].write(queueHeaders.first().tid[1]);
        clients[queueHeaders.first().clientNum].write((byte)0x00);
        clients[queueHeaders.first().clientNum].write((byte)0x00);
        clients[queueHeaders.first().clientNum].write((byte)0x00);
        clients[queueHeaders.first().clientNum].write(0x03);
        clients[queueHeaders.first().clientNum].write(queueHeaders.first().uid);
        clients[queueHeaders.first().clientNum].write(queuePDUs[0] + 0x80);
        clients[queueHeaders.first().clientNum].write(0x0B);
      }
      deleteRequest();
    }            // if (queueRetries.first() >= MAX_RETRY)
    expectingData = false;
  }              // if (requestTimeout.isOver() && expectingData == true)
}

void sendSerial()
{
  if (sendingData == true) {
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
      Serial.write(lowByte(crc));                            // send CRC, low byte first
      Serial.write(highByte(crc));
      txNdx++;
    }
    if (Serial.availableForWrite() == SERIAL_TX_BUFFER_SIZE - 1 && txNdx > queueHeaders.first().PDUlen) {
      // wait for last byte (incl. CRC) to be sent from serial Tx buffer
      // this if statement is not very reliable (too fast) => adding extra T to the txDelay
      // Serial.isFlushed() method is needed....see https://github.com/arduino/Arduino/pull/3737
      if (txDelayRunning == false) {
        if (BAUD <= 19200) {
          txDelay.start((unsigned long)(3.5 * T) + T);         // delay between Modbus packets should be at least 3,5T
        }
        else {
          txDelay.start(1750 + T);
        }
        txDelayRunning = true;
      }
      if (txDelay.isOver()) {
        sendingData = false;
        txNdx = 0;
        txDelayRunning = false;
        digitalWrite(SerialTxControl, RS485Receive);                                    // Disable RS485 Transmit
        if (queueHeaders.first().uid == 0x00) {           // Modbus broadcast
          deleteRequest();
        } else {
          expectingData = true;
          requestTimeout.start(SERIAL_TIMEOUT);          // delays next serial write
          queueRetries.unshift(queueRetries.shift() + 1);
        }
      }
    }
  }
}

void recvSerial()
{
  while (Serial.available() > 0 && newData == false) {
    if (rxNdx < BUFFER_SIZE) {
      serialIn.buf[rxNdx] = Serial.read();
      rxNdx++;
    } else {
      Serial.read();
    }
    if (BAUD <= 19200) {
      rxDelay.start(35 * (T / 10));
    }
    else {
      rxDelay.start(1750);
    }
  }
  if (rxDelay.isOver() && rxNdx != 0) {
    serialIn.len = rxNdx;
    rxNdx = 0;
    newData = true;
  }
}

void processSerial()
{
  if (newData == true) {
    if (checkCRC() == true) {                                     // check CRC
      // check address, function (incl. function modified in error responses) of incoming packet against first request in queue, only expected responses to requests are forwarded to TCP/UDP
      if (serialIn.buf[0] == queueHeaders.first().uid && (serialIn.buf[1] == queuePDUs[0] || serialIn.buf[1] == queuePDUs[0] + 0x80) && expectingData == true) {
        slavesResponding[serialIn.buf[0]] = true;               // flag slave as responding
        if (queueHeaders.first().clientNum == 0xFF) {
          Udp.beginPacket(queueHeaders.first().remIP, queueHeaders.first().remPort);
          Udp.write(queueHeaders.first().tid[0]);
          Udp.write(queueHeaders.first().tid[1]);
          Udp.write((byte)0x00);
          Udp.write((byte)0x00);
          Udp.write(highByte(serialIn.len - 2));
          Udp.write(lowByte(serialIn.len - 2));
          Udp.write(serialIn.buf, serialIn.len - 2);      //send without CRC
          Udp.endPacket();
        } else {
          clients[queueHeaders.first().clientNum].write(queueHeaders.first().tid[0]);
          clients[queueHeaders.first().clientNum].write(queueHeaders.first().tid[1]);
          clients[queueHeaders.first().clientNum].write((byte)0x00);
          clients[queueHeaders.first().clientNum].write((byte)0x00);
          clients[queueHeaders.first().clientNum].write(highByte(serialIn.len - 2));
          clients[queueHeaders.first().clientNum].write(lowByte(serialIn.len - 2));
          clients[queueHeaders.first().clientNum].write(serialIn.buf, serialIn.len - 2);      //send without CRC
        }
        deleteRequest();
        expectingData = false;
      }
    }           // if (checkCRC() == true)
    newData = false;
  }             // if (newData == true)
}

void deleteRequest()        // delete request from queue
{
  for (byte i = 0; i < queueHeaders.first().PDUlen; i++) {
    queuePDUs.shift();
  }
  queueHeaders.shift();
  queueRetries.shift();
}

bool checkCRC()                                       // Check CRC of packet stored in serial buffer
{
  crc = 0xFFFF;
  for (byte i = 0; i < serialIn.len - 2; i++) {
    calculateCRC(serialIn.buf[i]);
  }
  if (highByte(crc) == serialIn.buf[serialIn.len - 1] && lowByte(crc) == serialIn.buf[serialIn.len - 2]) {
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
