#ifndef WPalaControl_h
#define WPalaControl_h

#include "Main.h"
#include "base/WifiMan.h"
#include "base/MQTTMan.h"
#include "base/SSEServer.h"
#include "base/Application.h"

#include <Palazzetti.h>
#include <WiFiUdp.h>

class WPalaControl : public Application
{
private:
  enum HaMqttType : uint8_t
  {
    Generic = 0,
    GenericJson = 1,
    GenericCategorized = 2
  };
  enum HaProtocol : uint8_t
  {
    Disabled = 0,
    Mqtt = 1
  };
  enum HwDetection : uint8_t
  {
    AutoDetect = 0,
    ForcedV1 = 1,
    ForcedV2 = 2
  };
  enum HwVersion : uint8_t
  {
    Unknown = 0,
    V1 = 1,
    V2 = 2
  };

  struct MQTT
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
  };

  struct HomeAutomation
  {
    HaProtocol protocol = HaProtocol::Disabled;
    char hostname[64 + 1] = {0};
    uint16_t uploadPeriod = 60;
    MQTT mqtt;
  };

  HwDetection _hwDetection = HwDetection::AutoDetect;
  HwVersion _detectedHwVersion = HwVersion::Unknown;

  HomeAutomation _ha;
  int _haSendResult = 0;
  WiFiClient _wifiClient;
  MQTTMan _mqttMan;
  SSEServer _sse;
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
  ssize_t myReadSerial(void *buf, size_t count);
  ssize_t myWriteSerial(const void *buf, size_t count);
  int myDrainSerial();
  int myFlushSerial();
  void myUSleep(unsigned long usecond);

  void mqttConnectedCallback(MQTTMan *mqttMan, bool firstConnection);
  void mqttDisconnectedCallback();
  void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void mqttPublishStoveConnected(bool stoveConnected);
  bool mqttPublishData(const String &baseTopic, const String &palaCategory, const JsonDocument &jsonDoc);
  bool mqttPublishHassDiscovery();
  void mqttPublishHassDiscovery(HassDiscoveryCtx &ctx);
  void mqttPublishStoveHassDiscovery(HassDiscoveryCtx &ctx, Palazzetti::StaticData &staticData, Palazzetti::AllStatusData &allStatusData);

  bool executePalaCmd(const String &cmd, JsonDocument &jsonDoc, bool publish = false);
  Palazzetti::CommandResult executePalaCmdCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed);
  Palazzetti::CommandResult executePalaCmdGet(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, uint8_t cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executePalaCmdSet(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, uint8_t cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executePalaCmdExt(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, uint8_t cmdParamNumber, const uint16_t *cmdParams);

  void publishTick();
  void udpRequestHandler(WiFiUDP &udpServer);

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void validateConfig() override;
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit = false);
  void appInitWebServer(WebServer &server);
  void appRun();

public:
  WPalaControl();
};

#endif
