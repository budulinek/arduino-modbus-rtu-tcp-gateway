# arduino-modbus-tcp-rtu-gateway
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
  o Modbus RTU
* Ethernet interface protocols:
  o Modbus TCP
  o Modbus UDP
  o Modbus RTU over TCP
  o Modbus RTU over UDP
* supports broadcast (slave address 0x00) and error codes
* supports all Modbus function codes
* settings can be changed via web interface, stored in EEPROM
* diagnostics and Modbus RTU scan via web interface
* optimized queue for Modbus requests
  o prioritization of requests to responding slaves
  o queue will accept only one requests to non-responding slaves

## How can I build it myself?
Get the hardware. Cheap clones from China are sufficient:

* Arduino (Nano, Uno, possibly other)
* W5500-based Ethernet shield (for Nano, I recommend W5500 Ethernet Shield from RobotDyn)
* MAX485 module

Connect the hardware:

* Arduino <-> MAX485

* Tx1 <-> DI

* Rx0 <-> RO

* Pin 6 <-> DE,RE

Download this repository (all *.ino files) and open arduino-modbus-rtu-tcp-gateway.ino in Arduino IDE. Download all required libraries (some of them are available in "library manager", other have to be manually downloaded from github). If you want, you can check the default factory settings (can be later changed via web interface) and advanced settings (can only be changed in sketch). Compile and upload your program to Arduino. Connect your Arduino to ethernet, connect your Modbus RTU slaves to MAX485 module. Use your web browser to access the web interface on default IP  http://192.168.1.254   Enjoy :-)

## Where can I learn more about Modbus protocols?

https://en.wikipedia.org/wiki/Modbus

https://modbus.org/specs.php

http://www.simplymodbus.ca/FAQ.htm

## Can I use just the web interface for my own project?
Feel free to use this sketch as a template for a web interface within your own project. Look into the main file (arduino-modbus-rtu-tcp-gateway.ino) for how settings are stored in and loaded from EEPROM during boot. Ethernet interface and Webserver is started via function in 01-interfaces.ino. All other functions related to the web server (reading from clients, sending pages to clients) can be found in separate files (04-webserver.ino , 05-pages.ino ). Feel free to adjust them.

The key to success is:

* use StreamLib https://github.com/jandrassy/StreamLib
* use F macros for your HTML code
* for W5500 ethernet modules, use Ethernet3 https://github.com/sstaub/Ethernet3
* use POST method (rather than GET) for your webforms, see this tutorial https://werner.rothschopf.net/202003_arduino_webserver_post_en.htm

Big thanks to the authors of these libraries and tutorials!

## Background

This project started as a simple "playground" where I learned things. However, it evolved into more serious project: Modbus gateway in full compliance with Modbus standards. Later on, web interface was added as a demonstration of what a simple Arduino Nano is capable of. 

Not everything could fit into the limited flash memory of Arduino Nano / Uno. The DHCP client within a ethernet library consumes too much memory. The code for automatic IP address is in the sketch but disabled by default. If you want to use auto IP functionality, you have to use something bigger (such as Arduino Mega) and uncomment #define ENABLE_DHCP. After that, new "Auto IP" setting will appear in the IP settings web interface.

<img src="/pics/modbus6.png" alt="06" style="zoom:100%;" />