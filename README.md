# ch554_esp_uploader
This is an example how to use CH552 to upload firmware to ESP8266 or ESP8285
(and possibly to ESP32 as well). CH552 is used for USB communication with
your PC and streams the Firmware binary data (ESP firmware) to the ESP chip.
The CH554 is not exposed to your PC as a serial port, but uses libusb to transfer
the data. This approach has some benefits compared to the traditional
uploads via serial port (for example via CH340 serial port IC):
* no fiddling with the serial ports (which COM port is used etc.)
* automatic detection that your board is plugged in (via USB)
* CH554 can do other tasks like checking battery voltage, using I2C to connect
  to other peripherals (RTC, accelerometer, temp. sensor etc.), disable ESP  when
  needed (sleep modes), toggle other GPIOs etc.
* CH554 can nicely handle the dreaded 74880 baud rate. So if your esp program does not
  boot for some reason, you can use CH554 to read the boot log and pass it over USB to
  your PC console.
* CH554 handles toggling of the Reset, Enable and Boot-mode pins on the ESP chip. No need
  to have physical buttons, the FW upload is automated and the ESP is reset after the 
  upload is finished.

Building and running:
---------------------
1) setup the ch554_sdcc sdk (https://github.com/Blinkinlabs/ch554_sdcc.git) and make sure you
   can compile and run the examples in the example directory
2) copy the 'projects' directory from this repo into the root of the ch554_sdcc sdk (examples
   and projects directories will be on the same level)
3) copy the 'include' directory from this repo into the root of the ch554_sdcc sdk (you
   may want to backup the original debug.h first). 
4) enter projects/esp_uploader directory and run 'make' to build, then 'make flash' to upload
   the binary to your CH55x device. The flashing requires 'chprog' CH55X uploader, that you
   can get from my github project: https://github.com/ole00/chprog
5) disconnect and connect CH55x device from your PC
6) build the ESP PC uploader app by running './build_pc_linux.sh'. It will produce 'pc_upl'
   executable
7) run './test.sh' to upload an esp8266 demo firmware via CH552 to ESP8266 or ESP8285

CH552 and ESP8266 connection
----------------------------
There is a schematic of an example connection in the 'schematic' directory.

Q&A
---
Q: what is the origin of the ESP uploader app code?

A: it's a customised fork of the following project: https://github.com/espressif/esp-serial-flasher.git 


Q: the upload is slow

A: yes, uploading uses 115200 baud rate (~10kbytes/s) and does not use compression. Esp8266 bootloader
   does not support compression, and the advanced upload techniques (using the upload stubs) are not used.
   This upload mechanism is not meant for frequent development uploads, but rather as a way how to
   allow hasle-free and inferquent FW upgrades of your product by a non-technical user (providing that
   your PC app will provide some user friendly FW upgrade interface).
   
