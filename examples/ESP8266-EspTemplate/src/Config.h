//=======================================================================
// Config.h example demonstrating the usage of the EspSetup library
// Author:  Wolfgang Kracht
// Date:    7/19/2020
// Licence: https://www.gnu.org/licenses/gpl-3.0
//=======================================================================

#pragma once
#include <EspSetup.h>

#define CONSOLE Serial  // Serial, Telnet or NoDebug to disable debugging
#define CONFIGFILEPATH "/esp/config.json"

class Device
{
public:
  Device() = default;
  virtual ~Device() = default;

  String Uuid;
  String Name;
};

class Config
{
  public:
  Config() = default;
  virtual ~Config() = default;

  void Setup();
  bool Ready() { return Done; }

  bool UseMqtt() const { return useMqtt && MqttIP4 != "" && MqttPORT != 0; }

  const std::vector<Device*> getDeviceList() { return  DeviceList; }

  Device* FindDeviceUuid(const String &uuid);
  Device* FindDeviceName(const String &name);
  
  String MqttIP4;
  int    MqttPORT = 0;
  String MqttUSER;
  String MqttPASS;

  private:
  bool WriteConfig();
  bool ReadConfig();
  
  bool Done = false;
  bool useMqtt = false;

  std::vector<Device*> DeviceList;
};

extern Config cfg;
extern EspSetup esp;
