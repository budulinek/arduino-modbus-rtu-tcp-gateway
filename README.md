# Arduino-Modbus-RTU---Modbus-TCP-UDP-Gateway
Transparent gateway Modbus RTU &lt;---> Modbus TCP / Modbus UDP for Arduino

## Hardware
* Arduino (Nano, Uno, possibly other)
* W5500 Ethernet shield
* MAX485 (or other RS485 TTL module)

## Features
* slaves are on RS485 line (Modbus RTU protocol)
* master(s) are on ethernet (Modbus TCP or Modbus UDP protocol)
* supports up to 247 Modbus RTU slaves
* supports Modbus UDP masters and up to 8 Modbus TCP masters connected at the same time
* supports broadcast (slave address 0x00)
* supports error codes (forwards error codes from slaves, sends its own error codes)
* supports all Modbus function codes (incl. proprietary)
* optimized queue for Modbus requests 
  * queue will accept only one requests to a slave which is not responding
  * requests to responding slaves are processed first

## Connections
Arduino <-> MAX485

Tx1 <-> DI

Rx0 <-> RO

Pin 6 <-> DE,RE

## Settings
* RS485 settings
  * BAUD
  * SERIAL_CONFIG   (sets data, parity, and stop bits)
  * SERIAL_TIMEOUT  (timeout for Modbus RTU request)
  * MAX_RETRY       (maximum number of retries)
* TCP and UDP settings
  * MAC, IP,  GATEWAY, SUBNET
  * TCP_PORT        (local Modbus TCP port)
  * ETH_MAX_RETRY   (maximum number of retries for sending UDP packet / TCP response)
  * UDP_PORT        (OPTIONAL: local Modbus UDP port, defaults to Modbus TCP port)
  
