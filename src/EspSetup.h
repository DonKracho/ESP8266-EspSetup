//=======================================================================
// EspSetup.cpp Arduino EspSetup library ESP8266 / ESP32 
// Author:  Wolfgang Kracht
// Date:    7/19/2020
// Licence: https://www.gnu.org/licenses/gpl-3.0
//=======================================================================
#pragma once

#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

typedef std::function<void(uint8_t num, WStype_t type, uint8_t *payload, size_t len)> WebSocketServerEvent;
typedef std::function<void(const String &txt)> TelnetCallbackFn;

#define NETWORK_CONFIGURATION_PATH "/esp/network.json"
#define MAX_TELNET_CLIENTS 2
#define DEFAULT_APIP "192.168.4.1"
#define DEFAULT_WLIP "DHCP"

class WiFiUDP;
class WiFiServer;

class NTPClient
{
public:
  NTPClient() {};
  virtual ~NTPClient();

  bool Setup(String &rUrl, int gmt);
  bool Loop();                                                            // has to be called in main loop to update the time
  time_t UtcTime() { return seconds; }                                    // get current UTC time in seconds
  time_t LocalTime() { return seconds + gmtOffset + dstOffset; }		      // get current local time in seconds
  
  time_t fromIsoDateTimeString(const String &dateTime);
  String getIsoDateTimeString(time_t t, const char *ext = "");
  String getUtcIsoDateTimeString(bool appendZ = false);
  String getLocalIsoDateTimeString(bool appendGmtOffs = false);
  String getDateTimeString(time_t const t);                               // return a char string with fomated date and time
  String getDateTimeString() { return getDateTimeString(LocalTime()); }   // conveniance method (use class time)
  int    getElapsedSecsToday(time_t seconds);

  bool isDaylightSavingTime(time_t const seconds);                        // returns if it is EU dayligh saving time
  bool isDaylightSavingTime() { return isDaylightSavingTime(LocalTime()); }// conveniance method (use class time)

  bool isValid() { return sync; }                                         // tells if there has been valid request response 
  void setGmtOffset(int hours) { gmtOffset = hours * 3600; }              // GMT to UTC offset in hours
  int  getGmtOffset() { return gmtOffset / 3600; }                        // returns hours
  
private:
  time_t adjustSeconds();                                                 // called from Loop()
  void sendRequest();                                                     // sent a NTP request called from Loop()
  void receiveTime();                                                     // wait for reply and decode when a NTP packet is received
  void decodePacket(const byte* buf, int len);                            // decode NTP packet and set the class time base

  WiFiUDP *pUdp = nullptr;
  String url;                                                             // ntp dns name
  int    gmtOffset = 0;                                                   // GMT offset in seconds
  int    dstOffset = 0;                                                   // DST offset in seconds
  time_t seconds = 0;                                                     // UST time in seconds
  time_t lastMillis = 0;                                                  // used to count 1000 ms for accumulating seconds in Loop() 
  time_t nextRequest = 0;                                                 // timestamp to request time from NTP server

  bool   isDst = false;                                                   // true if daylight saving is active
  bool   doSync = false;                                                  // set to true for valid setup
  bool   sync = false;                                                    // true if synced with NTP server
};

class EspSetup : public ESP8266WebServer
{
  public:
  EspSetup(Stream& s, int port = 80);
  virtual ~EspSetup();

  void Setup();
  void Loop();
  void DeepSleep(uint32_t msDelay = 0);
  void OverideDeepSleepPin(int pin) { dsOveridePin = pin; } 

  bool IsAP() { return isApMode; }
  bool IsNTP() { return ntpEnab && !IsAP() && !ntpHost.isEmpty(); }
  bool IsDeepSleep() { return (dsOveridePin >= 0) ? digitalRead(dsOveridePin) & dsEnab : dsEnab; }
  WiFiUDP* UDP() { return pUdp; }
  WiFiServer* TCP() { return pTcp; }
  FS* GetFS();
  bool CheckWebServerCredentials();

  bool   SaveNetworkConfiguration(char *pJson);
  String DumpNetworkConfiguration();
  String GetUniqueDeviceName();
  String GetDeviceName() { return hstName; }
  String GetContentType(String filename);
  
  bool WebSocketConnected();
  void WebSocketSend(int num, String text);
  void WebSocketBroadcast(String text);
  void AddWebSocketCallback(WebSocketServerEvent pFunction) { WebSocketCallbackList.push_back(pFunction); }
  std::vector<WebSocketServerEvent> GetWebSocketCallbackList() { return WebSocketCallbackList; }

  void TelnetCallback(TelnetCallbackFn pFunction) { pTelnetCallbackFn = pFunction; }

  bool WriteFile(const String &rFilePath, const String &rData);
  bool WriteFile(const String &rFilePath, const JsonDocument &rDoc);
  bool ReadFile(const String &rFilePath, String &rData);
  bool ReadFile(const String &rFilePath, JsonDocument &rDoc);

  static bool handleFileRead(String path);
  
  private:
  void OTASetup();
  void TcpLoop();
  void NtpLoop();
  bool StartAPMode();
  bool StartClientMode();
  bool LoadNetworkConfiguration();
  bool UpdateNetworkConfiguration(const char *pJson);
  String formatBytes(size_t bytes);

  Stream& console;
  
  static void handleFileUpload();
  static void handleStatus();
  static void handleFileStatus();
  static void handleFileList();
  static void deleteRecursive(String path);
  static void handleFileDelete();
  static void handleFileCreate();
 
  bool retryloop = true;

  // Servers (only instatiated when their correspondent ports are not zero)
  WiFiUDP    *pUdp = nullptr;
  WiFiServer *pTcp = nullptr;
  WiFiClient TelnetClient[MAX_TELNET_CLIENTS];

  std::vector<WebSocketServerEvent> WebSocketCallbackList;
  TelnetCallbackFn pTelnetCallbackFn = nullptr;

  int    dsOveridePin = -1;
  bool   isApMode = false;

  bool   apMode = true;
  String wlSsid;
  String wlPass;
  String wlSip4 = DEFAULT_WLIP;
  String apName;
  String apPass;
  String apSip4 = DEFAULT_APIP;
  int    apChan = 9;
  String hstName;
  int    webPort = 80;
  String webUser;
  String webPass;
  int    udpPort = 0;
  int    tcpPort = 0;
  bool   ntpEnab = false;
  String ntpHost;
  int    gmtOffs = 0;
  bool   dsEnab = false;  // Deep Sleep enable
  uint32_t dsLoop = 0;    // Deep Sleep wake loop [ms]
};

class NullSerial : public Stream
{
private:

public:
  // public methods
  NullSerial() {}
  ~NullSerial() {}
  void begin(long speed) {}
  bool listen() { return true; }
  void end() {}
  bool isListening() { return true; }
  bool overflow() { return false; }
  int peek() { return -1; }

  virtual size_t write(uint8_t byte) { return 0; }
  virtual int read() { return -1; }
  virtual int available() { return 0; }
  virtual void flush() {}
  
  size_t write(const char *str) { return 0; }
  size_t write(const uint8_t *buffer, size_t size) { return 0; }
    
  size_t print(const __FlashStringHelper *) { return 0; }
  size_t print(const String &) { return 0; }
  size_t print(const char[]) { return 0; }
  size_t print(char) { return 0; }
  size_t print(unsigned char, int = DEC) { return 0; }
  size_t print(int, int = DEC) { return 0; }
  size_t print(unsigned int, int = DEC) { return 0; }
  size_t print(long, int = DEC) { return 0; }
  size_t print(unsigned long, int = DEC) { return 0; }
  size_t print(double, int = 2) { return 0; }
  size_t print(const Printable&) { return 0; }

  size_t println(const __FlashStringHelper *) { return 0; }
  size_t println(const String &s) { return 0; }
  size_t println(const char[]) { return 0; }
  size_t println(char) { return 0; }
  size_t println(unsigned char, int = DEC) { return 0; }
  size_t println(int, int = DEC) { return 0; }
  size_t println(unsigned int, int = DEC) { return 0; }
  size_t println(long, int = DEC) { return 0; }
  size_t println(unsigned long, int = DEC) { return 0; }
  size_t println(double, int = 2) { return 0; }
  size_t println(const Printable&) { return 0; }
  size_t println(void) { return 0; }

  // public only for easy access by interrupt handlers
  static inline void handle_interrupt();
};

//extern EspSetup esp;
extern NTPClient ntp;
extern NullSerial NoDebug;
extern WiFiClient Telnet;
