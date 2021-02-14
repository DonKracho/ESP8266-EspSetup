# EspSetup - Arduino IDE Library for ESP8266

The ESP8266 has got my favorite micro controller for IoT and home automation purposes. I prefer using the tiny and cheap WeMos D1 mini (LOLIN(WEMOS) D1 R2 & mini). Setting up the WIFI and other stuff is always the same boring code to be implement when working with an ESP8266 device. To be able to focus on the functionality of a sketch I created this EspSetup libray to configure the ESP8266 device from scratch. EspSetup is a derivate of the ESP8266WebServer class. It is based on the great FSBrowser example enhanced with extensions I often require for my ESP8266 projects. Configuration files and Web pages are stored directly on the ESP8266 device on a SPIFFS LitleFS filesystem. Because LittleFS is used for storing configuration files this library has no support for the ESP32 framework right now!

Main Functions:
* WIFI Setup as AP- or Client STA-mode with automatic fallback to AP if the WiFi network is not reachable.
* DHCP / Static IP
* MDNS service
* OTA update service
* Esp8266Wbserver with build in FS editor, Setup page and support for Favicon.ico
* WebSocketServer (for fast interaction with a Browser using javascript) 
* Telnet server
* TCP / UDP sockets
* NTP client
* Debugging is configurable to Serial, Telnet or NoDebug (Nulldevice)
* Configuration files are stored in json format on SPIFFS (LittleFS)
* Loading Web pages from the SPIFFS allows to serve more complex pages without running out of heap memory.

## License

Copyright (c) 2020 Wolfgang Kracht. Published under GNU GPL V3 license https://www.gnu.org/licenses/gpl-3.0

## Dependencies

Unfortunatly library dependencies do not get installed automatically when the library is installed as a zip package. You will have to install these libraries manually.
https://github.com/bblanchon/ArduinoJson.git (Note: can be installed via the Arduino Libray manager: ArduinoJson. Must be at least version 1.6!)
https://github.com/Links2004/arduinoWebSockets (Attention: do not get messed up with the ArduinoWebsocket library listed in the Arduino Library manager)
https://github.com/PaulStoffregen/Time (Note: can be installed via the Arduino Library manager: Time)

## How to use

1. First of all you'll need the latest https://github.com/esp8266/Arduino core to be installed via the Arduiono boards manager. I guess you are here because you have already done this and you can skip this step. 
2. The EspSetup library is developed for the Arduino IDE. Download this git repository as zip file and install it via the Arduino IDE menue
*Sketch / Bibliothek einbinden / .ZIP Bibliothek Hinzufügen...*
3. For uploading data to SPIFFS LittleFS with ease install the *ESP8266LittleFS-2.6.0* plugin unter <Arduino IDE>/tools. Please refer to https://github.com/earlephilhower/arduino-esp8266littlefs-plugin and follow the instructions found there.
4. Load the example sketch EspSetup / **EspTemplate** (there is no need to edit or replace anything).
5. Before compiling the sketch configure the Flash Size to reserve space for the FileSystem. Usually the smalest amount offered will be sufficient to store the configuration data and some HTML web pages (e.g 1MB for the WeMos D1 mini).![Configuring Flash Size](/images/FlashSize.png)
6. Compile and upload the sketch to the ESP8266 device via a serial port. (later on you may use OTA updates)
7. Close the Serial monitor if running, and click on the menu entry "ESP8266 LittleFS Data Upload" This will upload all content of the Example sketch data folder to the SPIFFS (refer to paragraph 3.).
8. On the first usage the ESP8266 will not be able to connect to your WiFi network, because no credentials are configured. The ESP8266 device will start up as Acces Point named ESP_[last 6 bytes of the MAC adress] (e.g. ESP_0ED2A8) then. Connect jour Laptop or mobile device to this Wifi Network. Without configuration there is no password set.
 9. Open a web browser and type 10.0.0.1/setup into the address line. The browser may complain about an unsecure connection besause the ESP establishes a HTTP and not HTTPS connection. For local network you can ignore this and continue to the web site. You will see the setup page.![Setup HTML page](/images/SetupPage.png)
10. The MAC address shown on the top of Setup page is the client mode MAC. This may be usefull if you have to permit the device in the router WLAN MAC table. If You want to connect to an existing WiFi network don't forget to change the radio button to client mode. If jou choose to have telnet debugging the tcp port has to be set (telnet is usually assigned to port 23) When done with all setting push the Save button. The Reset button will restart the ESP device with you new network settings.
 
## Other functionalities

**[mDNS name]/edit** open a very usefull tool from the ESP8266 FSBrowser. On the left side ther is a tree view of the SPIFFS files and folders. Clicking on a text based file will open it in the wondow on the right side including a full featured editor to make local changes to files.

![Setup HTML page](/images/EditPage.png)
 
**[mDNS name]/** opens the EspTemplate example web page. This ma be a useful example using websockets for your own projects.

![Setup HTML page](/images/RootPage.png)

**NTPClientAsync ntp** Yet another NTPClient approach. I used this code sice I wanted to be able to read the local time on my ESP devices without having access to a RTC hardware. The main difference to many other NTP client implementations is that this client is not blocking while waiting for the ntp response package. Between the sync intervals the second counter is incremented based in the internlal millis() timer. Initializing and using the TimeLib in parallel is a kind of overkill, it is yust for convenience purposes. This NTPClient also has some conversion utils for IsoDateTime strings. Please configure the NTP server url and GMT offset via the setup page.

## Known limitations and issues:
* Telnet server: On the first connect you'll receive some garbage charaters in the callback function. I tried several things, like flush() and reading available bytes,  but could not get rid of this wired garbage.
* NTPClientAsync: The calculation of the DaylightSavingTime flag is hardcoded to the european standards.
* NTPClientAsync: ntp.getDateTimeString() returns a string localized to German language.
* NTPClientAsync: The NTP client requires an internet connection to be established. It can not syncronize when the ESP device runs as access point.
* Access Point: When configuring AP mode with a password set, the password has to be at least 8 characters long, otherwise the AP will not start. This is an issue of the esp core, I don't know if it has been solved meanwhile.
* The network configuration is stored unencrypted. All my ESP devices I use are accessible via the local trusted network only. You may set a user an password for the Web server. As consequence at least the setup and the edit web pages ask for credentials before shown. 
