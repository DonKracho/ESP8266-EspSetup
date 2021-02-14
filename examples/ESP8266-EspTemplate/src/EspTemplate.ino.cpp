//=======================================================================
// ESP template example demonstrating the usage of the EspSetup library
// Author:  Wolfgang Kracht
// Date:    7/19/2020
// Licence: https://www.gnu.org/licenses/gpl-3.0
//=======================================================================

#include "Config.h"

EspSetup esp(CONSOLE);

#define EspTemplateFile "EspTemplate.txt"

//=== WebSocket server calback funtions ===
 
String text = "The quick brown fox jumps over the lazy dog.";
int slid = 0;

void getValues(const char* pJson) {
  StaticJsonDocument<256> doc;
  if (!deserializeJson(doc, pJson)) {
    text = doc["text"].as<String>();
    slid = doc["slid"];
  }
}

String replyString(bool all) {
  StaticJsonDocument<256> doc;
  
  doc["time"] = String(ntp.getDateTimeString());
  if (all) doc["text"] = text;
  doc["slid"] = second();

  String conf;
  serializeJson(doc, conf);
  return conf;
}

void ws_callback_fn(uint8_t num, WStype_t type, const char *payload, size_t len) {
  if (type == WStype_TEXT) {
    CONSOLE.printf("[%u] got Text: %s\n", num, payload);
    if (!strncmp(payload, "EspTemplate", 8)) { esp.WebSocketSend(num, replyString(true)); }
    else if (!strncmp(payload, "Save", 4))   { getValues((char*) &payload[4]); esp.WriteFile(EspTemplateFile, &payload[4]); }
    else if (!strncmp(payload, "Reboot", 6)) { ESP.reset(); }
  }
}

//=== Telnet server calback funtions ===

void tn_callback_fn(const String &txt) {
  CONSOLE.print(txt);
}

//=== Arduino setup ===

void setup() {
  Serial.begin(74880);
  Serial.println("\nbooting ...");

  
  esp.Setup();    // start EspSetup servers
  
  cfg.Setup();    // example for additional project configuration

  // register the web server uris you want to manage by your own
  esp.on("/", HTTP_GET, []() {
    if (esp.CheckWebServerCredentials()) {  // optional credential check eo access the page
      esp.handleFileRead(F("EspTemplate.htm"));
    }
  });

  // configure callback functions
  esp.WebSocketCallback(ws_callback_fn);
  esp.TelnetCallback(tn_callback_fn);

  // restore example web page content 
  String values;
  if (esp.ReadFile(EspTemplateFile, values)) {
    getValues(values.c_str());
  }
}

//=== Arduino loop ===

void loop() {
  static time_t last = 0;
  time_t curr = now();
  esp.Loop();
  if (last != curr) {
    last = curr;
    // update example web page exery second
    esp.WebSocketBroadcast(replyString(false));
  }
  //esp.DeepSleep(1000);
}
