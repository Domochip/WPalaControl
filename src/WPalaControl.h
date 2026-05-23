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

  struct HassDiscoveryStoveContext
  {
    const String &device;
    const String &uniqueIdPrefixStove;
    const __FlashStringHelper *availabilityJSON;
    uint16_t SPLMIN;
    uint16_t SPLMAX;
    byte UICONFIG;
    byte MAINTPROBE;
    const uint16_t *FANLMINMAX;
    bool hasSetPoint;
    bool hasPower;
    bool hasOnOff;
    bool hasRoomFan;
    bool hasFan3;
    bool ifFan3SwitchEntity;
    bool hasFan4;
    bool ifFan4SwitchEntity;
    bool isAirType;
    bool isHydroType;
    bool hasFanAuto;
  };

  String prepareHassDiscoveryTopic(const String &type, const String &uniqueId);
  bool mqttPublishHassGateway();
  String buildStoveDeviceString(const String &uniqueIdPrefixStove, uint16_t MOD, uint16_t VER, const char *FWDATE);
  void mqttPublishHassStoveConnectivity(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveStatus(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveStatusText(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveThermostat(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveSupplyWaterTemp(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveMainTemp(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveFlueGasTemp(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStovePqt(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveServiceTime(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveFeeder(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveDpTarget(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveDpPressure(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveOnOff(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveSetPoint(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStovePower(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveRoomFan(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveRoomFanAuto(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveFan3(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveFan4(const HassDiscoveryStoveContext &ctx);
  void mqttPublishHassStoveSetTime(const HassDiscoveryStoveContext &ctx);
  bool mqttHassDiscoveryStove();
  bool mqttHassDiscovery();

  bool mqttPublishUpdate();

  Palazzetti::CommandResult executePalaCmdCmdOff(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdCmdOn(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetAlls(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetChrd(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetCntr(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetDprs(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetFand(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetHpar(JsonObject &data, uint16_t hparAddress);
  Palazzetti::CommandResult executePalaCmdGetIopt(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetLabl(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetMdve(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetParm(JsonObject &data, uint16_t parmAddress);
  Palazzetti::CommandResult executePalaCmdGetSetp(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetStat(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetStdt(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetTime(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetTmps(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetPowr(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdGetSern(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetCday(JsonObject &data, uint16_t p0, uint16_t p1, uint16_t p2);
  Palazzetti::CommandResult executePalaCmdSetCprd(JsonObject &data, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4, uint16_t p5);
  Palazzetti::CommandResult executePalaCmdSetCset(uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetCsph(uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetCspm(uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetCsst(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetCsth(uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetCstm(uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetFn2d(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetFn2u(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetFn3l(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetFn4l(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetHpar(JsonObject &data, uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetParm(JsonObject &data, uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetPowr(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetPwrd(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetPwru(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetRfan(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetSetp(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetSlnt(JsonObject &data, uint16_t p0);
  Palazzetti::CommandResult executePalaCmdSetStpd(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetStpf(JsonObject &data, JsonObject &info, uint16_t p0, uint16_t p1);
  Palazzetti::CommandResult executePalaCmdSetStpu(JsonObject &data);
  Palazzetti::CommandResult executePalaCmdSetTime(JsonObject &data, JsonObject &info, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4, uint16_t p5);
  Palazzetti::CommandResult executeCmdPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed);
  Palazzetti::CommandResult executeGetPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executeSetPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);
  Palazzetti::CommandResult executeExtPalaCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams);
  bool executePalaCmd(const String &cmd, JsonDocument &jsonDoc, bool publish = false);

  void publishTick();
  void udpRequestHandler(WiFiUDP &udpServer);

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit = false);
  const PROGMEM char *getHTMLContent(WebPageForPlaceHolder wp);
  size_t getHTMLContentSize(WebPageForPlaceHolder wp);
  void appInitWebServer(WebServer &server);
  void appRun();

public:
  WPalaControl();
};

#endif
