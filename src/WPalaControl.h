#ifndef WPalaControl_h
#define WPalaControl_h

#include "Main.h"
#include "base/WifiMan.h"
#include "base/MQTTMan.h"
#include "base/EventSourceMan.h"
#include "base/Application.h"

const char appDataPredefPassword[] PROGMEM = "ewcXoCt4HHjZUvY1";

#include "data/status2.html.gz.h"
#include "data/config2.html.gz.h"

#include <Palazzetti.h>
#include <WiFiUdp.h>

class WPalaControl : public Application
{
private:
  enum HaMqttType : byte
  {
    Generic = 0,
    GenericJson = 1,
    GenericCategorized = 2
  };
  enum HaProtocol : byte
  {
    Disabled = 0,
    Mqtt = 1
  };
  enum HwDetection : byte
  {
    AutoDetect = 0,
    ForcedV1 = 1,
    ForcedV2 = 2
  };
  enum HwVersion : byte
  {
    Unknown = 0,
    V1 = 1,
    V2 = 2
  };

  typedef struct
  {
    HaMqttType type = HaMqttType::GenericJson;
    uint32_t port = 1883;
    char username[32 + 1] = {0};
    char password[64 + 1] = {0};
    struct
    {
      char baseTopic[64 + 1] = {0};
    } generic;
    bool hassDiscoveryEnabled = true;
    char hassDiscoveryPrefix[32 + 1] = {0};
  } MQTT;

  typedef struct
  {
    HaProtocol protocol = HaProtocol::Disabled;
    char hostname[64 + 1] = {0};
    uint16_t uploadPeriod = 60;
    MQTT mqtt;
  } HomeAutomation;

  HwDetection _hwDetection = HwDetection::AutoDetect;
  HwVersion _detectedHwVersion = HwVersion::Unknown;

  HomeAutomation _ha;
  int _haSendResult = 0;
  WiFiClient _wifiClient;
  MQTTMan _mqttMan;
  char _preparedMqttBaseTopic[64 + 4 + 12 - 7 + sizeof(CUSTOM_APP_MODEL) + 1] = {0}; // _ha.mqtt.generic.baseTopic.baseTopic length + one of each placeholder (+4 sn) (+12 mac) (+X model)
  EventSourceMan _eventSourceMan;
  WiFiUDP _udpServer;

  Palazzetti _Pala;
  unsigned long _lastAllStatusRefreshMillis = 0;

  bool _needPublish = false;
  Ticker _publishTicker;
  bool _publishedStoveConnected = false;
  bool _needPublishHassDiscovery = false;
  bool _needPublishUpdate = false;
  Ticker _publishUpdateTicker;

  int myOpenSerial(uint32_t baudrate);
  void myCloseSerial();
  int mySelectSerial(unsigned long timeout);
  size_t myReadSerial(void *buf, size_t count);
  size_t myWriteSerial(const void *buf, size_t count);
  int myDrainSerial();
  int myFlushSerial();
  void myUSleep(unsigned long usecond);

  void mqttConnectedCallback(MQTTMan *mqttMan, bool firstConnection);
  void mqttDisconnectedCallback();
  void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void mqttPublishStoveConnected(bool stoveConnected);
  bool mqttPublishData(const String &baseTopic, const String &palaCategory, const JsonDocument &jsonDoc);
  bool mqttPublishHassDiscovery();
  bool mqttPublishUpdate();
  bool executePalaCmd(const String &cmd, JsonDocument &jsonDoc, bool publish = false);
  Palazzetti::CommandResult executeCmdPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed);
  Palazzetti::CommandResult executeGetPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executeSetPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executeExtPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);

  void publishTick();
  void udpRequestHandler(WiFiUDP &udpServer);

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage);
  void fillConfigJSON(JsonVariant json, bool forSaveFile);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit);
  const PROGMEM char *getHTMLContent(WebPageForPlaceHolder wp);
  size_t getHTMLContentSize(WebPageForPlaceHolder wp);
  void appInitWebServer(WebServer &server);
  void appRun();

public:
  WPalaControl();
};

#endif
