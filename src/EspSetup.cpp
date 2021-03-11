//=======================================================================
// EspSetup.cpp Arduino EspSetup library ESP8266 / ESP32
// Credits: ESP8266WebServer FSBrowser example from ESp8266 core 
// Author:  Wolfgang Kracht
// Date:    7/19/2020
// Licence: https://www.gnu.org/licenses/gpl-3.0
//=======================================================================

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "EspSetup.h"

//#define DEBUG_VERBOSE
#define FileSystemName "LittleFS"

FS* EspFileSytem = &LittleFS;
LittleFSConfig EspFileSytemConfig = LittleFSConfig();

NullSerial NoDebug;
WiFiClient Telnet;
NTPClient  ntp;

//== used for statics and global functions ===

EspSetup *pEspSetup;
Stream   *pEspConsole;

//=== Telnet Server ===

void OnTcpReceive(char* txt) {
  pEspConsole->println(txt);
}

//=== Web Socket Server ===

WebSocketsServer EspWebSocket = WebSocketsServer(81);

int webSocketsConnected = 0;

void EspWebSocketCallback(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
  for (WebSocketServerEvent WsCbFn : pEspSetup->GetWebSocketCallbackList()) {
    WsCbFn(num, type, payload, len);
  }
}

void EspWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
  switch(type) {
    case WStype_DISCONNECTED:
      pEspConsole->printf("[%u] Disconnected!\n", num);
      webSocketsConnected -= 1;
      break;
    case WStype_CONNECTED: {
      IPAddress ip = EspWebSocket.remoteIP(num);
      pEspConsole->printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      webSocketsConnected += 1;
      break;
    }
    case WStype_TEXT:
      if (len > 12) {
        if (!strncmp((const char*) payload, "EspSetupPage", 12)) { EspWebSocket.sendTXT(num, pEspSetup->DumpNetworkConfiguration().c_str()); }
        else if (!strncmp((const char*) payload, "EspSetupSave", 12)) { pEspSetup->SaveNetworkConfiguration((char*)&payload[12]); }
        else if (!strncmp((const char*) payload, "EspSetupReset", 13)) { ESP.reset(); }
      }
      break;
    default:
      break;
  }
}

//=== static Web Server callbacks ===

static bool fsOK;
String unsupportedFiles = String();

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

////////////////////////////////
// Utils to return HTTP codes, and determine content-type

void replyOK() {
  pEspSetup->send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg) {
  pEspSetup->send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg) {
  pEspSetup->send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg) {
  pEspConsole->println(msg);
  pEspSetup->send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  pEspConsole->println(msg);
  pEspSetup->send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

/*
   Return the FS type, status and size info
*/
void EspSetup::handleStatus() {
  pEspConsole->println("handleStatus");
  FSInfo fs_info;
  String json;
  json.reserve(128);

  json = "{\"type\":\"";
  json += FileSystemName;
  json += "\", \"isOk\":";
  if (fsOK) {
    EspFileSytem->info(fs_info);
    json += F("\"true\", \"totalBytes\":\"");
    json += fs_info.totalBytes;
    json += F("\", \"usedBytes\":\"");
    json += fs_info.usedBytes;
    json += "\"";
  } else {
    json += "\"false\"";
  }
  json += F(",\"unsupportedFiles\":\"");
  json += unsupportedFiles;
  json += "\"}";

  pEspSetup->send(200, "application/json", json);
}

/*
   Read the given file from the filesystem and stream it back to the client
*/
bool EspSetup::handleFileRead(String path) {
  pEspConsole->println(String("handleFileRead: ") + path);
  if (!fsOK) {
    replyServerError(FPSTR(FS_INIT_ERROR));
    return true;
  }

  if (path.endsWith("/")) {
    path += "index.htm";
  }

  String contentType;
  if (pEspSetup->hasArg("download")) {
    contentType = F("application/octet-stream");
  } else {
    contentType = mime::getContentType(path);
  }

  if (!EspFileSytem->exists(path)) {
    // File not found, try gzip version
    path = path + ".gz";
  }
  if (EspFileSytem->exists(path)) {
    File file = EspFileSytem->open(path, "r");
    if (pEspSetup->streamFile(file, contentType) != file.size()) {
      pEspConsole->println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}

/*
   As some FS (e.g. LittleFS) delete the parent folder when the last child has been removed,
   return the path of the closest parent still existing
*/
String lastExistingParent(String path) {
  while (!path.isEmpty() && !EspFileSytem->exists(path)) {
    if (path.lastIndexOf('/') > 0) {
      path = path.substring(0, path.lastIndexOf('/'));
    } else {
      path = String();  // No slash => the top folder does not exist
    }
  }
  pEspConsole->println(String("Last existing parent: ") + path);
  return path;
}

/*
   Handle the creation/rename of a new file
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Create file    | parent of created file
   Create folder  | parent of created folder
   Rename file    | parent of source file
   Move file      | parent of source file, or remaining ancestor
   Rename folder  | parent of source folder
   Move folder    | parent of source folder, or remaining ancestor
*/
void EspSetup::handleFileCreate() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = pEspSetup->arg("path");
  if (path.isEmpty()) {
    return replyBadRequest(F("PATH ARG MISSING"));
  }

  if (path == "/") {
    return replyBadRequest("BAD PATH");
  }
  if (EspFileSytem->exists(path)) {
    return replyBadRequest(F("PATH FILE EXISTS"));
  }

  String src = pEspSetup->arg("src");
  if (src.isEmpty()) {
    // No source specified: creation
    pEspConsole->println(String("handleFileCreate: ") + path);
    if (path.endsWith("/")) {
      // Create a folder
      path.remove(path.length() - 1);
      if (!EspFileSytem->mkdir(path)) {
        return replyServerError(F("MKDIR FAILED"));
      }
    } else {
      // Create a file
      File file = EspFileSytem->open(path, "w");
      if (file) {
        file.write((const char *)0);
        file.close();
      } else {
        return replyServerError(F("CREATE FAILED"));
      }
    }
    if (path.lastIndexOf('/') > -1) {
      path = path.substring(0, path.lastIndexOf('/'));
    }
    replyOKWithMsg(path);
  } else {
    // Source specified: rename
    if (src == "/") {
      return replyBadRequest("BAD SRC");
    }
    if (!EspFileSytem->exists(src)) {
      return replyBadRequest(F("SRC FILE NOT FOUND"));
    }

    pEspConsole->println(String("handleFileCreate: ") + path + " from " + src);

    if (path.endsWith("/")) {
      path.remove(path.length() - 1);
    }
    if (src.endsWith("/")) {
      src.remove(src.length() - 1);
    }
    if (!EspFileSytem->rename(src, path)) {
      return replyServerError(F("RENAME FAILED"));
    }
    replyOKWithMsg(lastExistingParent(src));
  }
}

/*
   Delete the file or folder designed by the given path.
   If it's a file, delete it.
   If it's a folder, delete all nested contents first then the folder itself

   IMPORTANT NOTE: using recursion is generally not recommended on embedded devices and can lead to crashes (stack overflow errors).
   This use is just for demonstration purpose, and FSBrowser might crash in case of deeply nested filesystems.
   Please don't do this on a production system.
*/
void EspSetup::deleteRecursive(String path) {
  File file = EspFileSytem->open(path, "r");
  bool isDir = file.isDirectory();
  file.close();

  // If it's a plain file, delete it
  if (!isDir) {
    EspFileSytem->remove(path);
    return;
  }

  // Otherwise delete its contents first
  Dir dir = EspFileSytem->openDir(path);

  while (dir.next()) {
    deleteRecursive(path + '/' + dir.fileName());
  }

  // Then delete the folder itself
  EspFileSytem->rmdir(path);
}

/*
   Handle a file deletion request
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Delete file    | parent of deleted file, or remaining ancestor
   Delete folder  | parent of deleted folder, or remaining ancestor
*/
void EspSetup::handleFileDelete() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = pEspSetup->arg(0);
  if (path.isEmpty() || path == "/") {
    return replyBadRequest("BAD PATH");
  }

  pEspConsole->println(String("handleFileDelete: ") + path);
  if (!EspFileSytem->exists(path)) {
    return replyNotFound(FPSTR(FILE_NOT_FOUND));
  }
  deleteRecursive(path);

  replyOKWithMsg(lastExistingParent(path));
}

/*
   Handle a file upload request
*/
// holds the currently running upload
File fsUploadFile;
bool uploadActive = false;

void EspSetup::handleFileUpload() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }
  if (pEspSetup->uri() != "/edit") {
    return;
  }
  uploadActive = true;
  HTTPUpload& upload = pEspSetup->upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    // Make sure paths always start with "/"
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    pEspConsole->println(String("handleFileUpload Name: ") + filename);
    fsUploadFile = EspFileSytem->open(filename, "w");
    if (!fsUploadFile) {
      return replyServerError(F("CREATE FAILED"));
    }
    pEspConsole->println(String("Upload: START, filename: ") + filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      size_t bytesWritten = fsUploadFile.write(upload.buf, upload.currentSize);
      if (bytesWritten != upload.currentSize) {
        return replyServerError(F("WRITE FAILED"));
      }
    }
    pEspConsole->println(String("Upload: WRITE, Bytes: ") + upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      uploadActive = false;
    }
    pEspConsole->println(String("Upload: END, Size: ") + upload.totalSize);
  }
}


/*
   Return the list of files in the directory specified by the "dir" query string parameter.
   Also demonstrates the use of chuncked responses.
*/
void EspSetup::handleFileList() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  if (!pEspSetup->hasArg("dir")) {
    return replyBadRequest(F("DIR ARG MISSING"));
  }

  String path = pEspSetup->arg("dir");
  if (path != "/" && !EspFileSytem->exists(path)) {
    return replyBadRequest("BAD PATH");
  }

  pEspConsole->println(String("handleFileList: ") + path);
  Dir dir = EspFileSytem->openDir(path);
  path.clear();

  // use HTTP/1.1 Chunked response to avoid building a huge temporary string
  if (!pEspSetup->chunkedResponseModeStart(200, "text/json")) {
    pEspSetup->send(505, F("text/html"), F("HTTP1.1 required"));
    return;
  }

  // use the same string for every line
  String output;
  output.reserve(64);
  while (dir.next()) {
    if (output.length()) {
      // send string from previous iteration
      // as an HTTP chunk
      pEspSetup->sendContent(output);
      output = ',';
    } else {
      output = '[';
    }

    output += "{\"type\":\"";
    if (dir.isDirectory()) {
      output += "dir";
    } else {
      output += F("file\",\"size\":\"");
      output += dir.fileSize();
    }

    output += F("\",\"name\":\"");
    // Always return names without leading "/"
    if (dir.fileName()[0] == '/') {
      output += &(dir.fileName()[1]);
    } else {
      output += dir.fileName();
    }

    output += "\"}";
  }

  // send last string
  output += "]";
  pEspSetup->sendContent(output);
  pEspSetup->chunkedResponseFinalize();
}

// === class EspSetup ===

// Ctor without initialization
EspSetup::EspSetup(Stream& s, int port) : ESP8266WebServer(port), console(s)
{
  pEspSetup = this;
  pEspConsole = &console;
}

// Dtor delete UDP/TCP sockets
EspSetup::~EspSetup()
{
  if (pUdp) delete pUdp;
  if (pTcp) delete pTcp;
}

FS* EspSetup::GetFS()
{
  return EspFileSytem;
}

bool EspSetup::CheckWebServerCredentials() {
  if (webUser != "" && webPass != "") {
    if (!authenticate(webUser.c_str(), webPass.c_str())) {
      requestAuthentication();
      return false;
    }
  }
  return true;
}

void EspSetup::Setup(void){
  console.println("\nbooting...\n");

  EspFileSytemConfig.setAutoFormat(true);
  EspFileSytem->setConfig(EspFileSytemConfig);
  fsOK = EspFileSytem->begin();
  console.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  LoadNetworkConfiguration();
#ifdef DEBUG_VERBOSE
  console.println(DumpNetworkConfiguration());
#endif
  if (apMode) {
    // settings do not exist, failed or
    // the user configured to run in AP mode anyway.
    // start in AP mode
    StartAPMode();
  } else {
    // settings were loaded successfully,
    // and the user has setup to run as a WLAN client,
    // so try to connect to the given router ssid/password.
    if (!StartClientMode()) {
      // failed to connect to the router,
      // so force AP mode start
      StartAPMode();
    }
  }
  
  if (udpPort != 0) {
    pUdp = new WiFiUDP();
    pUdp->begin(udpPort);
    console.print("UDP server started on port: ");
    console.println(udpPort);
  }

  if (tcpPort != 0) {
    pTcp = new WiFiServer(tcpPort);
    pTcp->begin();
    pTcp->setNoDelay(true);
    console.print("TCP server started on port: ");
    console.println(tcpPort);
  }

  if (IsNTP()) {
    ntp.Setup(ntpHost, gmtOffs);
    console.print("NTP client started on url: ");
    console.println(ntpHost);
  }

  // Multicast Domain Name System
  if (hstName.length() > 0) {
    MDNS.begin(hstName);
    console.printf("mDNS started use http:/%s:%d/setup\n", hstName.c_str(), webPort);
  }

  // SERVER INIT
  // filesystem status
  on("/status", HTTP_GET, handleStatus);
  // list directory
  on("/list", HTTP_GET, handleFileList);
  // load editor
  on("/edit", HTTP_GET, [this]() {
    if (CheckWebServerCredentials()) {
      if (!handleFileRead(F("/esp/edit.htm"))) {
        replyNotFound(FPSTR(FILE_NOT_FOUND));;
      }
    }
  });
  // create file
  on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  on("/edit", HTTP_POST, [this]() {
    send(200, "text/plain", "");
  }, handleFileUpload);

  on("/setup", HTTP_GET, [this]() {
     // return WiFi setup page (setup.htm)
	if (CheckWebServerCredentials()) {
      if (!handleFileRead(F("/esp/setup.htm"))) {
        replyNotFound(FPSTR(FILE_NOT_FOUND));;
      }
    }
  });

  // editor reset
  on("/cmd/ESP-Reboot", HTTP_GET, [this]() {
    replyNotFound(FPSTR(FILE_NOT_FOUND));
    delay(100);
    ESP.reset();
  });

  //get heap status, analog input value and all GPIO statuses in one json call
  on("/all", HTTP_GET, [this]() {
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += ", \"analog\":" + String(analogRead(A0));
    json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    send(200, "text/json", json);
    json = String();
  });

  //called when the url is not defined here
  //use it to load content from SPIFFS
  onNotFound([this]() {
    if (!handleFileRead(uri())) {
      String message = "File Not Found\n";
      message += "\nuri: ";
      message += uri();
      message += "\nmethod: ";
      message += (method() == HTTP_GET) ? "GET" : "POST";
      message += "\narguments: ";
      message += args();
      message += "\n";
      for (uint8_t i = 0; i < args(); i++) {
	    message += " " + argName(i) + ": " + arg(i) + "\n";
      }
      send(404, "text/plain", message);
    }
  });

  //the web interface is always listening, validate port
  if (webPort <= 0 || webPort > 65535) webPort = 80;

  begin(webPort);
  console.print("HTTP server started on port: ");
  console.println(webPort);

  // start webSocket server
  EspWebSocket.begin();
  EspWebSocket.onEvent(EspWebSocketCallback);
  AddWebSocketCallback(EspWebSocketEvent);
  console.println("WebSocket server started");

  OTASetup();
}

void EspSetup::Loop(void) {
  if (WiFi.status() != WL_CONNECTED ) WiFi.reconnect();
  ArduinoOTA.handle();
  handleClient();
  MDNS.update();
  EspWebSocket.loop();
  TcpLoop();
  NtpLoop();
}

void EspSetup::DeepSleep(uint32_t msDelay)
{
  if (dsEnab) {
    delay(msDelay);
    console.println("\nGoing to deep sleep...");
    uint64_t uptime = micros();
    uint64_t sleeptime = dsLoop * 1000;
    // substract current uptime to achieve more accurate loop time
    if (sleeptime > uptime) sleeptime -= uptime; 
    ESP.deepSleep(sleeptime);
    delay(0);
  }
}

void EspSetup::NtpLoop()
{
  if (IsNTP()) {
    ntp.Loop();
  }
}

void EspSetup::TcpLoop()
{
  if (!TCP()) return;

  // Cleanup disconnected session
  for(int i = 0; i < MAX_TELNET_CLIENTS; i++) {
    if (TelnetClient[i] && !TelnetClient[i].connected()) {
      console.print("Client disconnected ... terminate session "); console.println(i+1); 
      TelnetClient[i].stop();
    }
  }

  // Check new client connections
  if (TCP()->hasClient()) {
    bool ConnectionEstablished = false;

    for(int i = 0; i < MAX_TELNET_CLIENTS; i++) {
      // find free socket
      if (!TelnetClient[i]) {
        Telnet = TCP()->available();  // last connected socket is used as console output
        while(Telnet.available()) Telnet.read();
        Telnet.flush();               // clear input buffer, else you get strange characters
        TelnetClient[i] = Telnet;

        console.print("New Telnet client connected to session "); console.println(i+1);

        TelnetClient[i].println("Welcome!");
        TelnetClient[i].print("Millis since start: ");
        TelnetClient[i].println(millis());
        TelnetClient[i].print("Free Heap RAM: ");
        TelnetClient[i].println(ESP.getFreeHeap());
        TelnetClient[i].println("----------------------------------------------------------------");

        ConnectionEstablished = true; 
        break;
      }
    }

    if (ConnectionEstablished == false) {
      console.println("No free sessions ... drop connection");
      TCP()->available().stop();
    }
  }

  for(int i = 0; i < MAX_TELNET_CLIENTS; i++) {
    if (TelnetClient[i] && TelnetClient[i].connected()) {
      if(TelnetClient[i].available()) { 
        //get data from the telnet client
        String input;
        while(TelnetClient[i].available()) {
          char c = TelnetClient[i].read();
          input += c;
        }
        if (pTelnetCallbackFn) {
          pTelnetCallbackFn(input);
        }
      }
    }
  }
}

String EspSetup::formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String EspSetup::GetContentType(String filename) {
  if (pEspSetup->hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".json")) {
    return "text/json";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

String EspSetup::GetUniqueDeviceName()
{
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return ((hstName != "") ? hstName : String("ESP")) + "_" + mac.substring(6);
}

// Start AP Mode
bool EspSetup::StartAPMode()
{
  console.print("MAC: ");
  console.println(WiFi.macAddress());
  console.printf("Staring in AP mode with IP: %s and SSID: %s\n", apSip4.c_str(), apName.c_str());

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);

  IPAddress ip, mask;  

  if (!ip.fromString(apSip4)) ip.fromString(DEFAULT_APIP);
  mask.fromString("255.255.255.0");

  WiFi.disconnect();
  WiFi.softAPConfig(ip, ip, mask);
  WiFi.softAP(apName.c_str(), apPass.c_str(), apChan);
  isApMode = true;

  return true;
}

// Start STA Mode
bool EspSetup::StartClientMode()
{
  byte retries = 80;
  console.print("MAC: ");
  console.println(WiFi.macAddress());
  console.print(String("Starting in STA mode. Connecting to: ") + wlSsid + " ");
#ifdef WIFI_MULTI
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(wlSsid.c_str(), wlPass.c_str());
  while (WiFiMulti.run() != WL_CONNECTED && --retries) {
    console.print(".");
    delay(500);
  }
#else
  IPAddress ip, gw, mask, dns;  
  if (ip.fromString(wlSip4))
  {
    gw.fromString("192.168.0.1");
    mask.fromString("255.255.255.0");
    dns.fromString("192.168.0.1");
    WiFi.config(ip, gw, mask, dns);
  }

  if (String(WiFi.SSID()) != wlSsid) {
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(wlSsid.c_str(), wlPass.c_str());
    WiFi.persistent(true);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }

  while (WiFi.status() != WL_CONNECTED && --retries) {
    console.print(".");
    delay(100);
  }
#endif
  if (retries > 0) {
    console.print(" success IP: ");
    console.println(WiFi.localIP());
  }
  else {
    console.println(" failed!");
    // retry with disconnect
    if (retryloop) {
      retryloop = false;
      WiFi.disconnect();
      return StartClientMode();
    }
  }
  isApMode = (retries <= 0);

  return !isApMode;
}

void EspSetup::OTASetup()
{
  // OTA

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"1234");
  
  ArduinoOTA.onStart([this]() {
    console.println("Start");
  });
  
  ArduinoOTA.onEnd([this]() {
    console.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    console.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    console.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) console.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) console.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) console.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) console.println("Receive Failed");
    else if (error == OTA_END_ERROR) console.println("End Failed");
  });
  
  ArduinoOTA.begin();
}

bool EspSetup::LoadNetworkConfiguration() {
  String netconf;
  bool ret = ReadFile(NETWORK_CONFIGURATION_PATH, netconf);
  if (ret) {
    ret = UpdateNetworkConfiguration(netconf.c_str());
  }
  if (!ret) {
    console.println("Failed to load network configuration");
    // set reasonable defaults
    apName = GetUniqueDeviceName();
    apMode = true;    
  }
  return ret;
}

bool EspSetup::SaveNetworkConfiguration(char *pJson) {
  WriteFile(NETWORK_CONFIGURATION_PATH, pJson);
  return UpdateNetworkConfiguration(pJson);
}

bool EspSetup::UpdateNetworkConfiguration(const char *pJson) {
  bool ret = false;
  StaticJsonDocument<1024> doc;
  if (!deserializeJson(doc, pJson)) {
    JsonObject obj = doc.as<JsonObject>();
    apMode = obj["apMode"];
    if (obj.containsKey("wlSsid")) wlSsid = obj["wlSsid"].as<String>();
    if (obj.containsKey("wlPass")) wlPass = obj["wlPass"].as<String>();
    if (obj.containsKey("wlSip4")) wlSip4 = obj["wlSip4"].as<String>();
    if (obj.containsKey("apName")) apName = obj["apName"].as<String>();
    if (obj.containsKey("apPass")) apPass = obj["apPass"].as<String>();
    if (obj.containsKey("apSip4")) apSip4 = obj["apSip4"].as<String>();
    apChan = obj["apChan"];
    if (obj.containsKey("hstName")) hstName = obj["hstName"].as<String>();
    webPort = obj["webPort"];
    if (obj.containsKey("webUser")) webUser = obj["webUser"].as<String>();
    if (obj.containsKey("webPass")) webPass = obj["webPass"].as<String>();
    udpPort = obj["udpPort"];
    tcpPort = obj["tcpPort"];
    if (obj.containsKey("ntpEnab")) ntpEnab  = obj["ntpEnab"];
    if (obj.containsKey("ntpHost")) ntpHost  = obj["ntpHost"].as<String>();
    gmtOffs = obj["gmtOffs"];
    if (obj.containsKey("dsEnab")) dsEnab = obj["dsEnab"];
    dsLoop = obj["dsLoop"];

    if (apName == "") apName = GetUniqueDeviceName();
    ret = true;
  }
  return ret;
}

String EspSetup::DumpNetworkConfiguration() {
  StaticJsonDocument<512> doc;
  
  doc["apMode"]  = apMode;
  doc["wlSsid"]  = wlSsid;
  doc["wlPass"]  = wlPass;
  doc["wlSip4"]  = wlSip4;
  doc["apName"]  = apName;
  doc["apPass"]  = apPass;
  doc["apChan"]  = apChan;
  doc["apSip4"]  = apSip4;
  doc["hstName"] = hstName;
  doc["webPort"] = webPort;
  doc["webUser"] = webUser;
  doc["webPass"] = webPass;
  doc["udpPort"] = udpPort;
  doc["tcpPort"] = tcpPort;
  doc["ntpEnab"] = ntpEnab;
  doc["ntpHost"] = ntpHost;
  doc["gmtOffs"] = gmtOffs;
  doc["dsEnab"]  = dsEnab;
  doc["dsLoop"]  = dsLoop;
  doc["mac"] = WiFi.macAddress();

  String conf;
  serializeJson(doc, conf);
  return conf;
}

bool EspSetup::WebSocketConnected() {
  return webSocketsConnected > 0;
}

void EspSetup::WebSocketSend(int num, String text) {
  EspWebSocket.sendTXT(num, text.c_str()); 
}

void EspSetup::WebSocketBroadcast(String text) {
  EspWebSocket.broadcastTXT(text.c_str()); 
}

bool EspSetup::WriteFile(const String &rFilePath, const String &rData) {
  bool ret = false;
  if (EspFileSytem) {
    File file = EspFileSytem->open(rFilePath, "w");
    if(file)
    {
      ret = file.write(rData.c_str()) == rData.length();
      file.close();
    }
  }
  return ret;
}

bool EspSetup::WriteFile(const String &rFilePath, const JsonDocument &rDoc) {
  bool ret = false;
  if (EspFileSytem) {
    File file = EspFileSytem->open(rFilePath, "w");
    if(file)
    {
      ret = serializeJsonPretty(rDoc, file) > 0;
      file.close();
    }
  }
  return ret;
}

bool EspSetup::ReadFile(const String &rFilePath, String &rData) {
  bool ret = false;
  if (EspFileSytem && EspFileSytem->exists(rFilePath)) {
    File file = EspFileSytem->open(rFilePath, "r");
    if(file)
    {
      rData = file.readString();
      file.close();
      ret = true;
    }
  }
  return ret;  
}

bool EspSetup::ReadFile(const String &rFilePath, JsonDocument &rDoc) {
  bool ret = false;
  if (EspFileSytem && EspFileSytem->exists(rFilePath)) {
    File file = EspFileSytem->open(rFilePath, "r");
    if(file)
    {
      ret = file.size() > 0 && !deserializeJson(rDoc, file);
      file.close();
    }
  }
  return ret;
}

//=== class NTPClient ===

#define NTP_INTERVAL 3600                 // updating intervall: 1 hour shoud be sufficient

#define NTP_DEBUG
//#define NTP_DEBUG_VERBOSE
#define TIMELIB_INIT

#define NTP_PACKET_SIZE 48
#define NTP_PORT 123
#define NTP_LOCAL_PORT 4711

NTPClient::~NTPClient() {
  if (pUdp) delete pUdp;
}

bool NTPClient::Setup(String &rUrl, int gmt) {
  url = rUrl;
  setGmtOffset(gmt);
  if (url.length() > 0) {
    pUdp = new WiFiUDP();
    pUdp->begin(NTP_LOCAL_PORT);
    doSync = true;
  }
  return doSync;
}

bool NTPClient::Loop() {
  if (!isValid()) {                         // not initialized jet or request send
    receiveTime();                          // check for returned ntp packege
  }
  if (adjustSeconds() >= nextRequest  && !(pEspSetup->IsAP())) {
    sendRequest();
  }
  return isValid();
}

time_t NTPClient::adjustSeconds() {
  time_t curr = millis(); 
  if (millis() - lastMillis >= 1000ul) {    // subtraction is overflow save 
    lastMillis = curr;
    seconds++;
    isDaylightSavingTime();
  }
  return UtcTime();
}

void NTPClient::sendRequest() {
  IPAddress timeServerIP;

  if (doSync && WiFi.hostByName(url.c_str(), timeServerIP)) {
#ifdef NTP_DEBUG
    pEspConsole->print("NTPClient::sendRequest");
    pEspConsole->print(" to \"");
    pEspConsole->print(url);
    pEspConsole->print("\" IP:");
    pEspConsole->println(timeServerIP);
#endif
    //pUdp->begin(NTP_LOCAL_PORT);
    // prepare packet and send request 
    byte packet[NTP_PACKET_SIZE];
    memset(packet, 0, NTP_PACKET_SIZE); 
    packet[0]  = 0xE3;  // LI, Version, Mode
    packet[1]  = 0;     // Stratum, or type of clock
    packet[2]  = 6;     // Polling Interval
    packet[3]  = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packet[12] = 49;
    packet[13] = 0x4E;
    packet[14] = 49;
    packet[15] = 52;
    while (pUdp->parsePacket() > 0); // discard any previously received packets
    pUdp->beginPacket(timeServerIP, NTP_PORT);
    pUdp->write(packet, NTP_PACKET_SIZE);
    pUdp->endPacket();
    sync = false;
    nextRequest = UtcTime() + 10;    // repeat request if there gets no resonse detected within 10s
  }
}

void NTPClient::receiveTime() {
  if (!isValid() && doSync) {
#ifdef NTP_DEBUG_VERBOSE
    pEspConsole->print("NTPClient::receiveTime ");
    pEspConsole->println(millis(),DEC);
#endif
    if (pUdp->parsePacket()) {
      byte packet[NTP_PACKET_SIZE];
      int len = pUdp->read(packet, NTP_PACKET_SIZE);
      decodePacket(packet, len);
    }
  }
}

void NTPClient::decodePacket(const byte* buf, int len) {
  if (len == NTP_PACKET_SIZE) {
    unsigned long highWord, lowWord, epoch;
    highWord = word(buf[40], buf[41]);
    lowWord  = word(buf[42], buf[43]);  
    epoch = highWord << 16 | lowWord;                             // this is UTC time in seconds since 1900
    seconds = epoch - 2208988800ul;                               // convert to unix base (subtract 70 years)
    isDaylightSavingTime();                                       // check for daylight saving
#ifdef TIMELIB_INIT
    setTime(LocalTime());                                         // required for TimeLib calls to now() or without parameter time_t
#endif
    sync = true;                                                  // time is synced now
    nextRequest = UtcTime() + NTP_INTERVAL;                       // timestanp to send next request
#ifdef NTP_DEBUG
    pEspConsole->print("NTPClient::decodePacket : ");
    pEspConsole->println(getDateTimeString());
#endif
  }
}

bool NTPClient::isDaylightSavingTime(time_t const t)
{
  // In Europe the summer time begins the last week of March and ends last Weekend of Octoberand  switches on sunday 2 AM
  // see this link http://www.webexhibits.org/daylightsaving/i.html
  // European Economic Community:
  // Begin DST: Sunday March (31 - (5*y/4 + 4) mod 7) at 1h U.T.
  // End DST: Sunday October (31 - (5*y/4 + 1) mod 7) at 1h U.T.
  // Since 1996, valid through 2099

  static bool dst = true;

  int beginDSTDate=  (31 - (5* year(t) / 4 + 4) % 7);                         // last sunday of March
  int beginDSTMonth=3;                                                        // March
  int endDSTDate= (31 - (5 * year(t) / 4 + 1) % 7);                           // last sunday of October
  int endDSTMonth=10;                                                         // October
  isDst = (                                                                   // calculate DST state
      (month(t) >  beginDSTMonth && month(t) < endDSTMonth)                   // month is in range for sure
   || (month(t) == beginDSTMonth && day(t) > beginDSTDate)                    // begin day is in range for sure
   || (month(t) == endDSTMonth && day(t) < endDSTDate)                        // end day is in range for sure
   || (month(t) == beginDSTMonth && day(t) == beginDSTDate && hour(t) >= 2)   // it is begin day and clock jumps forward fom 2:00am to 3:00am
   || (month(t) == endDSTMonth && day(t) == endDSTDate && dst && hour(t) < 3) // it is end day and clock jumps back from 3:00am to 2:00am
   );
  dst = isDst;                                                                // do not return to daylight saving if just switched to normal
  dstOffset = isDst ? 3600 : 0;                                               // offset has unit seconds

  return isDst;
}

String NTPClient::getDateTimeString(time_t const t)
{
  // ToDo: adapt the string retured to your needs
  char *date_time = (char*) "   00.00.0000 00:00:00";
#if USE_SPRINTF
  const char * day_names[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
  sprintf(date_time, "%s %02d.%02d.%04d %02d:%02d:%02d", day_name[weekday(t)],day(t),month(t),year(t),hour(t),minute(t),second(t));
#else
  // does not look nice but it is much faster than using sprintf and saves memory (may be relevant for Arduino Uno)
  switch (weekday(t))
  {
    case 1:
      date_time[0] = 'S';
      date_time[1] = 'o';
      break;
    case 2:
      date_time[0] = 'M';
      date_time[1] = 'o';
      break;
    case 3:
      date_time[0] = 'D';
      date_time[1] = 'i';
      break;
    case 4:
      date_time[0] = 'M';
      date_time[1] = 'i';
      break;
    case 5:
      date_time[0] = 'D';
      date_time[1] = 'o';
      break;
    case 6:
      date_time[0] = 'F';
      date_time[1] = 'r';
      break;
    case 7:
      date_time[0] = 'S';
      date_time[1] = 'a';
      break;
  }

  date_time[3] += day(t) / 10;
  date_time[4] += day(t) % 10;
  date_time[6] += month(t) / 10;
  date_time[7] += month(t) % 10;
  date_time[9] += (year(t) / 1000);
  date_time[10] += (year(t) % 1000) / 100;
  date_time[11] += (year(t) % 100) / 10;
  date_time[12] += year(t) % 10;
  
  date_time[14] += hour(t) / 10;
  date_time[15] += hour(t) % 10;
  date_time[17] += minute(t) / 10;
  date_time[18] += minute(t) % 10;
  date_time[20] += second(t) / 10;
  date_time[21] += second(t) % 10;
#endif
  return String(date_time);
}

time_t NTPClient::fromIsoDateTimeString(const String &isoDateTime) {
  tmElements_t tm;
  tm.Second = atoi(isoDateTime.substring(17,19).c_str());
  tm.Minute = atoi(isoDateTime.substring(14,16).c_str());
  tm.Hour = atoi(isoDateTime.substring(11,13).c_str());
  tm.Day = atoi(isoDateTime.substring(8,10).c_str());
  tm.Month = atoi(isoDateTime.substring(5,7).c_str());
  tm.Year = atoi(isoDateTime.substring(0,4).c_str()) - 1970;
  return makeTime(tm);
}

String NTPClient::getIsoDateTimeString(time_t t, const char *ext) {
  char buf[64];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d%s", year(t), month(t), day(t), hour(t), minute(t), second(t), ext);
  return String(buf);
}

String NTPClient::getUtcIsoDateTimeString(bool appendZ) {
  return getIsoDateTimeString(UtcTime(), appendZ ? "Z" : "");
}

String NTPClient::getLocalIsoDateTimeString(bool appendGmtOffset) {
  char gmt[8];
  if (appendGmtOffset)
     sprintf(gmt, "%s%02d", gmtOffset >= 0 ? "+" : "-", abs(gmtOffset/3600));
  else
     gmt[0] = '\0';
  return getIsoDateTimeString(LocalTime(), gmt);
}

int NTPClient::getElapsedSecsToday(time_t secs) {
  return elapsedSecsToday(secs);
}
