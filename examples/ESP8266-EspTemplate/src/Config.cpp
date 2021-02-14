//=======================================================================
// Config.cpp example demonstrating the usage of the EspSetup library
// Author:  Wolfgang Kracht
// Date:    7/19/2020
// Licence: https://www.gnu.org/licenses/gpl-3.0
//=======================================================================

#include "Config.h"

Config cfg;

void Config::Setup()
{
  if (!esp.GetFS()->exists(CONFIGFILEPATH)) {
    MqttIP4  = "xxx.xxx.xxx.xxx";
    MqttPORT = 1883;
    MqttUSER = "mqtt_usr";
    MqttPASS = "mqtt_pwd";
    useMqtt = true;
    Device *dev = new Device();
    dev->Uuid = "Hue01";
    dev->Name = "Rainbow";
    DeviceList.push_back(dev);
    WriteConfig();
  }
  Done = ReadConfig();
  if (!Done) {
    CONSOLE.printf("failed reading configuration file: %s\n", CONFIGFILEPATH);
  }
}

Device* Config::FindDeviceUuid(const String &uuid)
{
  for (Device *device : DeviceList) {
    if (device->Uuid == uuid) return device;
  }
  return nullptr;
}

Device* Config::FindDeviceName(const String &name)
{
  for (Device *device : DeviceList) {
    if (device->Name == name) return device;
  }
  return nullptr;
}

bool Config::ReadConfig()
{
  DynamicJsonDocument doc(8192);
  bool ret = esp.ReadFile(CONFIGFILEPATH, doc);
  
  if (ret)
  {
    CONSOLE.println("reading configuration:");

    JsonObject obj = doc.as<JsonObject>();
    String Config = obj["Config"].as<String>();
    String Version = obj["Version"].as<String>();
    if (Config != "Config" || Version.toDouble() > 1.0 || Version.toDouble() == 0)
    {
      CONSOLE.println("config type or version not supported");
      return false;
    }
          
    if (obj.containsKey("MqttIP4")) MqttIP4 = obj["MqttIP4"].as<String>();
    if (obj.containsKey("MqttPORT")) MqttPORT = obj["MqttPORT"];
    if (obj.containsKey("MqttUSER")) MqttUSER = obj["MqttUSER"].as<String>();
    if (obj.containsKey("MqttPASS")) MqttPASS = obj["MqttPASS"].as<String>();
    if (obj.containsKey("UseMqtt")) useMqtt = obj["UseMqtt"];
    if (UseMqtt()) CONSOLE.println("Mqtt is active");

    int cnt = 0;
    if (obj.containsKey("DeviceList")) {
      for (const JsonObject dev_obj : obj["DeviceList"].as<JsonArray>())
      {
        Device *dev = new Device();
        dev->Uuid = (dev_obj.containsKey("Uuid")) ? dev_obj["Uuid"].as<String>() : String(cnt);
        if (dev_obj.containsKey("Name")) dev->Name = dev_obj["Name"].as<String>();
        DeviceList.push_back(dev);
        cnt++;
      }
    }
    CONSOLE.printf("found %d devices\n\r", cnt);
  }
  return ret;
}

bool Config::WriteConfig()
{
  DynamicJsonDocument doc(8192);

  doc["Config"] = "Config";
  doc["Version"] = 1.0;

  doc["MqttIP4"]  = MqttIP4;
  doc["MqttPORT"] = MqttPORT;
  doc["MqttUSER"] = MqttUSER;
  doc["MqttPASS"] = MqttPASS;
  doc["UseMqtt"] = UseMqtt();

  JsonArray dev_list = doc.createNestedArray("DeviceList");
  for (Device *device : DeviceList) {
    JsonObject dev = dev_list.createNestedObject();
    dev["Uuid"]     = device->Uuid;
    dev["Name"]     = device->Name;
  }
  return esp.WriteFile(CONFIGFILEPATH, doc);
}
