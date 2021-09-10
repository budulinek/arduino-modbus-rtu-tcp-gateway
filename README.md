# arduino-modbus-rtu-tcp-gateway
Arduino-based Modbus RTU to Modbus TCP/UDP gateway with web interface. Allows you to connect Modbus RTU slaves (such as sensors, energy meters, HVAC devices) to Modbus TCP/UDP masters (such as home automation systems). You can adjust settings through web interface.

## What is it good for?

Allows you to connect your Modbus RTU slaves (such as sensors, energy meters, HVAC devices) to Modbus TCP/UDP masters (such as monitoring systems, home automation systems). You do not need commercial Modbus gateways. Arduino (with an ethernet shield and a cheap MAX485 module) can do the job!

Change settings of your Arduino-based Modbus RTU to Modbus TCP/UDP gateway via web interface (settings are automatically stored in EEPROM).

Screenshots of the web interface:

<img src="/pics/modbus1.png" alt="01" style="zoom:100%;" />

<img src="/pics/modbus2.png" alt="02" style="zoom:100%;" />

<img src="/pics/modbus3.png" alt="03" style="zoom:100%;" />

<img src="/pics/modbus4.png" alt="04" style="zoom:100%;" />

<img src="/pics/modbus5.png" alt="05" style="zoom:100%;" />

## What are the technical specifications?

* slaves are connected via RS485 interface
* master(s) are connected via ethernet interface
* up to 247 Modbus RTU slaves
* up to 8 TCP/UDP sockets for Modbus TCP/UDP masters and for the web interface
* RS485 interface protocols:
  - Modbus RTU
* Ethernet interface protocols:
  - Modbus TCP
  - Modbus UDP
  - Modbus RTU over TCP
  - Modbus RTU over UDP
* supports broadcast (slave address 0x00) and error codes
* supports all Modbus function codes
* settings can be changed via web interface, stored in EEPROM
* diagnostics and Modbus RTU scan via web interface
* optimized queue for Modbus requests
  - prioritization of requests to responding slaves
  - queue will accept only one requests to non-responding slaves

## How can I build it myself?
Get the hardware (cheap clones from China are sufficient) and connect together:

* Arduino Nano, Uno or Mega (and possibly other). On Mega you have to configure Serial in ADVANCED SETTINGS in the sketch.
* W5100, W5200 or W5500 based Ethernet shield (for Nano, I recommend W5500 Ethernet Shield from RobotDyn)
* TTL to RS485 module:
  - with hardware automatic flow control (recommended)<br>
      Arduino <-> Module<br>
      Tx1 <-> Tx<br>
      Rx0 <-> Rx
  - with flow controlled by pin (such as MAX485 module)<br>
      Arduino <-> MAX485<br>
      Tx1 <-> DI<br>
      Rx0 <-> RO<br>
      Pin 6 <-> DE,RE
      
Here is my setup:
Terminal shield + Arduino Nano + W5500 eth shield (RobotDyn) + TTL to RS485 module (HW automatic flow control)
<img src="/pics/HW.jpg" alt="01" style="zoom:100%;" />

Download this repository (all *.ino files) and open arduino-modbus-rtu-tcp-gateway.ino in Arduino IDE. Download all required libraries (both are available in "library manager"). If you want, you can check the default factory settings (can be later changed via web interface) and advanced settings (can only be changed in sketch). Compile and upload your program to Arduino. Connect your Arduino to ethernet, connect your Modbus RTU slaves to MAX485 module. Use your web browser to access the web interface on default IP  http://192.168.1.254   Enjoy :-)

## Where can I learn more about Modbus protocols?

https://en.wikipedia.org/wiki/Modbus

https://modbus.org/specs.php

http://www.simplymodbus.ca/FAQ.htm

## Can I use just the web interface for my own project?
Feel free to use this sketch as a template for a web interface within your own project. Look into the main file (arduino-modbus-rtu-tcp-gateway.ino) for how settings are stored in and loaded from EEPROM during boot. Ethernet interface and Webserver is started via function in 01-interfaces.ino. All other functions related to the web server (reading from clients, sending pages to clients) can be found in separate files (04-webserver.ino , 05-pages.ino ). Feel free to adjust them.

The key to success is:

* use StreamLib https://github.com/jandrassy/StreamLib
* use F macros for your HTML code
* use for() loop for repetitive code
* use POST method (rather than GET) for your webforms, see this tutorial https://werner.rothschopf.net/202003_arduino_webserver_post_en.htm

Big thanks to the authors of these libraries and tutorials!

## Limitations

#### Portability

The code was tested on Arduino Nano, Uno and Mega, ethernet chips W5100 and W5500. It may work on other platforms, but:

* The pseudorandom generator (for random MAC) is seeded through watch dog timer interrupt - this will work only on Arduino (credits to https://sites.google.com/site/astudyofentropy/project-definition/timer-jitter-entropy-sources/entropy-library/arduino-random-seed)
* The restart function will also work only on Arduino.

#### Ethernet socket

The default Ethernet.h library determines MAX_SOCK_NUM by microcontroller RAM (not by Ethernet chip type). So if you use W5500 (which has 8 sockets available) on Arduino Nano, only 4 sockets will be used. If you want to force the library to use 8 sockets, edit https://github.com/arduino-libraries/Ethernet/blob/master/src/Ethernet.h#L36 

#### Memory

Not everything could fit into the limited flash memory of Arduino Nano / Uno. If you have a microcontroller with more memory (such as Mega), you can enable extra features in the main sketch by uncommenting:

* #define ENABLE_DHCP will allow you to set "Auto IP" via DHCP in the IP settings web interface. Leased IP is automatically renewed.

<img src="/pics/modbus6.png" alt="06" style="zoom:100%;" />

* #define ENABLE_EXTRA_DIAG  shows extra info on "Current status" page: per socket diagnostics, run time counter.

<img src="/pics/modbus1x.png" alt="01x" style="zoom:100%;" />
