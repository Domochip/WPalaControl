#include "WPalaControl.h"

#ifdef ESP8266
#define PALA_SERIAL Serial
#else
#define PALA_SERIAL Serial2
#endif

// Serial management functions -------------
int WPalaControl::myOpenSerial(uint32_t baudrate)
{
#ifdef ESP8266
  LOG_SERIAL.flush();
  PALA_SERIAL.begin(baudrate);
  PALA_SERIAL.pins(15, 13); // swap ESP8266 pins to alternative positions (D7(GPIO13)(RX)/D8(GPIO15)(TX))
  pinMode(1, INPUT);
#else
  PALA_SERIAL.begin(baudrate, SERIAL_8N1, 23, 5); // set ESP32 pins to match hat position (IO23(RX)/IO5(TX))
#endif
  return 0;
}
void WPalaControl::myCloseSerial()
{
  PALA_SERIAL.end();
  // set TX PIN to OUTPUT HIGH to avoid stove bus blocking
#ifdef ESP8266
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
#else
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
#endif
}
int WPalaControl::mySelectSerial(unsigned long timeout)
{
  size_t avail;
  unsigned long startmillis = millis();
  while ((avail = PALA_SERIAL.available()) == 0 && (millis() - startmillis) < timeout)
    ;

  return avail;
}
ssize_t WPalaControl::myReadSerial(void *buf, size_t count) { return PALA_SERIAL.read((char *)buf, count); }
ssize_t WPalaControl::myWriteSerial(const void *buf, size_t count) { return PALA_SERIAL.write((const uint8_t *)buf, count); }
int WPalaControl::myDrainSerial()
{
  PALA_SERIAL.flush(); // On ESP, Serial.flush() is drain
  return 0;
}
int WPalaControl::myFlushSerial()
{
  PALA_SERIAL.flush();
  while (PALA_SERIAL.read() != -1)
    ; // flush RX buffer
  return 0;
}
void WPalaControl::myUSleep(unsigned long usecond) { delayMicroseconds(usecond); }

void WPalaControl::mqttConnectedCallback(MQTTMan *mqttMan, bool firstConnection)
{
  // Subscribe command topic --------------------------------
  String cmdTopic = _preparedMqttBaseTopic;

  switch (_ha.mqtt.type) // switch on MQTT type
  {
  case HaMqttType::Generic:
  case HaMqttType::GenericJson:
  case HaMqttType::GenericCategorized:
    cmdTopic += F("/cmd");
    break;
  }

  if (firstConnection)
    mqttMan->publish(cmdTopic.c_str(), ""); // make empty publish only for firstConnection
  mqttMan->subscribe(cmdTopic.c_str());

  // Subscribe to update/install topic -----------------------
  String updateInstalltopic(_preparedMqttBaseTopic);
  updateInstalltopic += F("/update/install");
  mqttMan->subscribe(updateInstalltopic.c_str());

  // raise flag to publish Home Assistant discovery data
  _needPublishHassDiscovery = true;
}

void WPalaControl::mqttDisconnectedCallback()
{
  // if MQTT is disconnected, MQTT Reconnection will publish "1" to connectedTopic
  _publishedStoveConnected = false;
}

void WPalaControl::mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
  // calculate command topic
  String cmdTopic = _preparedMqttBaseTopic;

  switch (_ha.mqtt.type) // switch on MQTT type
  {
  case HaMqttType::Generic:
  case HaMqttType::GenericJson:
  case HaMqttType::GenericCategorized:
    cmdTopic += F("/cmd");
    break;
  }
  // if topic is command one
  if (cmdTopic == topic)
  {
    String cmd;
    JsonDocument json;

    // convert payload to String cmd
    cmd.concat((char *)payload, length);

    // replace '+' by ' '
    cmd.replace('+', ' ');

    // execute Palazzetti command
    executePalaCmd(cmd, json, true);

    // publish json result to MQTT
    String resTopic = _preparedMqttBaseTopic;
    resTopic += F("/result");
    _mqttMan.publish(resTopic.c_str(), json);
  }

  // if topic ends with "/update/install"
  const size_t topicLen = strlen(topic);
  if (topicLen >= 15 && memcmp_P(topic + topicLen - 15, PSTR("/update/install"), 15) == 0)
  {
    String version;
    String retMsg;
    unsigned long lastProgressPublish = 0;

    // result topic is topic without the last 8 characters ("/install")
    String resTopic(topic);
    resTopic.remove(resTopic.length() - 8);

    if (length > 10)
      retMsg = F("Version is too long");
    else
    {
      // convert payload to String
      version.concat((char *)payload, length);

      // Define the progress callback function
      std::function<void(size_t, size_t)> progressCallback = [this, &resTopic, &lastProgressPublish](size_t progress, size_t total)
      {
        // if last progress publish is less than 500ms ago then return
        if (millis() - lastProgressPublish < 500)
          return;
        lastProgressPublish = millis();

        uint8_t percent = (progress * 100) / total;
        LOG_SERIAL_PRINTF_P(PSTR("Progress: %d%%\n"), percent);
        String payload = String(F("{\"update_percentage\":")) + percent + '}';
        _mqttMan.publish(resTopic.c_str(), payload.c_str(), true);
      };

      SystemState::shouldReboot = updateFirmware(version.c_str(), retMsg, progressCallback);
    }

    if (SystemState::shouldReboot)
      retMsg = F("{\"update_percentage\":null,\"in_progress\":true}");
    else
    {
      _mqttMan.publish(topic, (String(F("Update failed: ")) + retMsg).c_str(), true);
      retMsg = F("{\"in_progress\":false}");
    }

    // publish result
    _mqttMan.publish(resTopic.c_str(), retMsg.c_str(), true);
  }
}

void WPalaControl::mqttPublishStoveConnected(bool stoveConnected)
{
  if (_mqttMan.connected() && _publishedStoveConnected != stoveConnected)
  {
    // if Stove is connected, publish 2 to connected topic otherwise fallback to 1
    _mqttMan.publishToConnectedTopic((stoveConnected ? "2" : "1"));
    _needPublishHassDiscovery = true; // raise flag to publish Home Assistant discovery data
    _publishedStoveConnected = stoveConnected;
  }
}

bool WPalaControl::mqttPublishData(const String &baseTopic, const String &palaCategory, const JsonDocument &jsonDoc)
{
  bool res = false;
  if (!_mqttMan.connected())
    return res;

  char topicBuf[sizeof(_preparedMqttBaseTopic) + sizeof("/TIME/STOVE_DATETIME")]; // longest possible topic

  if (_ha.mqtt.type == HaMqttType::Generic)
  {
    for (JsonPairConst kv : jsonDoc["DATA"].as<JsonObjectConst>())
    {
      // prepare topic
      snprintf(topicBuf, sizeof(topicBuf), "%s/%s", baseTopic.c_str(), kv.key().c_str());
      // prepare value
      const char *val = kv.value().as<const char *>(); // try to get value as string
      // publish to MQTT
      res = val ? _mqttMan.publish(topicBuf, val) : _mqttMan.publish(topicBuf, kv.value());
    }
  }

  if (_ha.mqtt.type == HaMqttType::GenericJson)
  {
    // prepare topic
    snprintf(topicBuf, sizeof(topicBuf), "%s/%s", baseTopic.c_str(), palaCategory.c_str());
    // publish to MQTT
    res = _mqttMan.publish(topicBuf, jsonDoc["DATA"]);
  }

  if (_ha.mqtt.type == HaMqttType::GenericCategorized)
  {
    int prefixLen = snprintf(topicBuf, sizeof(topicBuf), "%s/%s/", baseTopic.c_str(), palaCategory.c_str());
    for (JsonPairConst kv : jsonDoc["DATA"].as<JsonObjectConst>())
    {
      // prepare topic
      snprintf(topicBuf + prefixLen, sizeof(topicBuf) - prefixLen, "%s", kv.key().c_str());
      // prepare value
      const char *val = kv.value().as<const char *>(); // try to get value as string
      // publish to MQTT
      res = val ? _mqttMan.publish(topicBuf, val) : _mqttMan.publish(topicBuf, kv.value());
    }
  }

  return res;
}

bool WPalaControl::mqttPublishHassDiscovery()
{
  if (!_mqttMan.connected())
    return false;

  LOG_SERIAL_PRINTLN(F("Publish Home Assistant Discovery data"));

  // Helper lambda to prepare entity topic
  auto prepareHassDiscoveryTopic = [&](const String &type, const String &uniqueId)
  {
    String topic;
    topic.reserve(strlen(_ha.mqtt.hassDiscoveryPrefix) + type.length() + uniqueId.length() + 9); // 9 = "/" + "/" + "/config"
    topic += _ha.mqtt.hassDiscoveryPrefix;
    topic += F("/");
    topic += type;
    topic += F("/");
    topic += uniqueId;
    topic += F("/config");
    return topic;
  };

  // variables
  JsonDocument json;
  String device;
  String uniqueIdPrefix, uniqueIdPrefixStove;
  String uniqueId;
  String topic;

  // ---------- Device ----------

  // prepare unique id prefix
  uniqueIdPrefix = F(CUSTOM_APP_MODEL "_");
  uniqueIdPrefix += WiFi.macAddress();
  uniqueIdPrefix.replace(":", "");

  // prepare device JSON
  deserializeJson(json, F("{"
                          "\"configuration_url\":\"http://" CUSTOM_APP_MODEL ".local\","
                          "\"manufacturer\":\"" CUSTOM_APP_MANUFACTURER "\","
                          "\"model\":\"" CUSTOM_APP_MODEL "\","
                          "\"sw_version\":\"" VERSION "\""
                          "}"));
  json[F("identifiers")][0] = uniqueIdPrefix;
  json[F("name")] = WiFi.getHostname();
  serializeJson(json, device); // serialize to device String

  // ----- Entities -----

  //
  // Connectivity entity
  //

  // prepare uniqueId, topic and payload for connectivity sensor
  uniqueId = uniqueIdPrefix + F("_Connectivity");

  topic = prepareHassDiscoveryTopic(F("binary_sensor"), uniqueId);

  // prepare payload for connectivity sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"binary_sensor." CUSTOM_APP_MODEL "_connectivity\","
                          "\"device_class\":\"connectivity\","
                          "\"entity_category\":\"diagnostic\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_connectivity\","
                          "\"state_topic\":\"~/connected\","
                          "\"value_template\": \"{{ iif(int(value) > 0, 'ON', 'OFF') }}\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;

  _mqttMan.publish(topic.c_str(), json, true);

  // clean device JSON before switching to Stove entities
  device = "";

  // ---------- Get Stove Device data ----------

  if (!_Pala.isInitialized())
    return true;

  // read static data from stove
  Palazzetti::StaticData staticData;
  if (Palazzetti::CommandResult::OK != _Pala.getStaticData(staticData))
    return false;

  // read all status from stove
  bool refreshStatus = false;
  unsigned long currentMillis = millis();
  if ((currentMillis - _lastAllStatusRefreshMillis) > 15000UL) // refresh AllStatus data if it's 15sec old
    refreshStatus = true;
  Palazzetti::AllStatusData allStatusData;
  if (Palazzetti::CommandResult::OK != _Pala.getAllStatus(false, allStatusData))
    return false;
  else if (refreshStatus)
    _lastAllStatusRefreshMillis = currentMillis;

  // calculate flags (https://github.com/palazzetti/palazzetti-sdk-asset-parser-python/blob/main/palazzetti_sdk_asset_parser/data/asset_parser.json)
  bool hasSetPoint = (allStatusData.SETP != 0);
  bool hasPower = (staticData.STOVETYPE != 8);
  bool hasOnOff = (staticData.STOVETYPE != 7 && staticData.STOVETYPE != 8);
  bool hasRoomFan = (staticData.FAN2TYPE > 1);
  bool hasFan3 = (staticData.FAN2TYPE > 3); // Fan order is not the expected one
  bool ifFan3SwitchEntity = (allStatusData.FANLMINMAX[2] == 0 && allStatusData.FANLMINMAX[3] == 1);
  bool hasFan4 = (staticData.FAN2TYPE > 2); // Fan order is not the expected one
  bool ifFan4SwitchEntity = (allStatusData.FANLMINMAX[4] == 0 && allStatusData.FANLMINMAX[5] == 1);
  bool isAirType = (staticData.STOVETYPE == 1 || staticData.STOVETYPE == 3 || staticData.STOVETYPE == 5 || staticData.STOVETYPE == 7 || staticData.STOVETYPE == 8);
  bool isHydroType = (staticData.STOVETYPE == 2 || staticData.STOVETYPE == 4 || staticData.STOVETYPE == 6);
  bool hasFanAuto = (staticData.FAN2MODE == 2 || staticData.FAN2MODE == 3);

  // ---------- Usefull variables for entities building ----------

  const __FlashStringHelper *availabilityJSON = F("{\"topic\":\"~/connected\",\"value_template\":\"{{ iif(int(value) > 0, 'online', 'offline') }}\"}");
  const __FlashStringHelper *statusTopicList[] = {F("~/STATUS"), F("~/STAT"), F("~/STAT/STATUS")};
  const __FlashStringHelper *tempProbeTopicListArray[][3] = {
      {F("~/T1"), F("~/TMPS"), F("~/TMPS/T1")},
      {F("~/T2"), F("~/TMPS"), F("~/TMPS/T2")},
      {F("~/T3"), F("~/TMPS"), F("~/TMPS/T3")},
      {F("~/T4"), F("~/TMPS"), F("~/TMPS/T4")},
      {F("~/T5"), F("~/TMPS"), F("~/TMPS/T5")}};
  const __FlashStringHelper *setpTopicList[] = {F("~/SETP"), F("~/SETP"), F("~/SETP/SETP")};
  const __FlashStringHelper *f2lTopicList[] = {F("~/F2L"), F("~/FAND"), F("~/FAND/F2L")};

  // ---------- Stove Device ----------

  // prepare unique id prefix for Stove
  uniqueIdPrefixStove = F(CUSTOM_APP_MODEL "_");
  uniqueIdPrefixStove += staticData.SN;

  // prepare Stove device JSON
  deserializeJson(json, F("{"
                          "\"configuration_url\":\"http://wpalacontrol.local\","
                          "\"name\":\"Stove\""
                          "}"));
  json[F("identifiers")][0] = uniqueIdPrefixStove;
  json[F("model")] = String(staticData.MOD);
  json[F("sw_version")] = String(staticData.VER) + F(" (") + staticData.FWDATE + ')';
  json[F("via_device")] = uniqueIdPrefix;
  serializeJson(json, device); // serialize to device String

  // ----- Stove Entities -----

  //
  // Connectivity entity
  //

  uniqueId = uniqueIdPrefixStove + F("_Connectivity");

  topic = prepareHassDiscoveryTopic(F("binary_sensor"), uniqueId);

  // prepare payload for Stove connectivity sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"binary_sensor.stove_connectivity\","
                          "\"device_class\":\"connectivity\","
                          "\"entity_category\":\"diagnostic\","
                          "\"object_id\":\"stove_connectivity\","
                          "\"state_topic\":\"~/connected\","
                          "\"value_template\": \"{{ iif(int(value) > 1, 'ON', 'OFF') }}\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Status entity
  //

  uniqueId = uniqueIdPrefixStove + F("_STATUS");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove status sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_status\","
                          "\"entity_category\":\"diagnostic\","
                          "\"name\":\"Status\","
                          "\"object_id\":\"stove_status\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("state_topic")] = statusTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ value_json.STATUS }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Status Text entity
  //

  uniqueId = uniqueIdPrefixStove + F("_STATUS_Text");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove status text sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_status_text\","
                          "\"device_class\":\"enum\","
                          "\"name\":\"Status\","
                          "\"object_id\":\"stove_status_text\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("state_topic")] = statusTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;
  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("value_template")] = F("{% set ns = namespace(found=false) %}{% set statusList=[([0],'Off'),([1],'Off Timer'),([2],'Test Fire'),([3,4,5],'Ignition'),([6],'Burning'),([9],'Cool'),([10],'Fire Stop'),([11],'Clean Fire'),([12],'Cool'),([239],'MFDoor Alarm'),([240],'Fire Error'),([241],'Chimney Alarm'),([243],'Grate Error'),([244],'NTC2 Alarm'),([245],'NTC3 Alarm'),([247],'Door Alarm'),([248],'Pressure Alarm'),([249],'NTC1 Alarm'),([250],'TC1 Alarm'),([252],'Gas Alarm'),([253],'No Pellet Alarm')] %}{% for num,text in statusList %}{% if int(value) in num %}{{ text }}{% set ns.found = true %}{% break %}{% endif %}{% endfor %}{% if not ns.found %}Unkown STATUS code {{ value }}{% endif %}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{% set ns = namespace(found=false) %}{% set statusList=[([0],'Off'),([1],'Off Timer'),([2],'Test Fire'),([3,4,5],'Ignition'),([6],'Burning'),([9],'Cool'),([10],'Fire Stop'),([11],'Clean Fire'),([12],'Cool'),([239],'MFDoor Alarm'),([240],'Fire Error'),([241],'Chimney Alarm'),([243],'Grate Error'),([244],'NTC2 Alarm'),([245],'NTC3 Alarm'),([247],'Door Alarm'),([248],'Pressure Alarm'),([249],'NTC1 Alarm'),([250],'TC1 Alarm'),([252],'Gas Alarm'),([253],'No Pellet Alarm')] %}{% for num,text in statusList %}{% if int(value_json.STATUS) in num %}{{ text }}{% set ns.found = true %}{% break %}{% endif %}{% endfor %}{% if not ns.found %}Unkown STATUS code {{ value_json.STATUS }}{% endif %}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Thermostat entity
  //

  // define probe number
  byte probeNumber = staticData.MAINTPROBE;                                                              // default case covering AirType and other HydroType
  if (isHydroType && (staticData.UICONFIG == 1 || staticData.UICONFIG == 3 || staticData.UICONFIG == 4)) // for Hydro which are in a Config controlling Water temperature
    probeNumber = 0;                                                                                     // T1

  uniqueId = uniqueIdPrefixStove + F("_Thermostat");

  topic = prepareHassDiscoveryTopic(F("climate"), uniqueId);

  // prepare payload for Stove thermostat
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"climate.stove_thermostat\","
                          "\"mode_command_template\":\"CMD+{{ iif(value == 'off', 'OFF', 'ON') }}\","
                          "\"mode_command_topic\":\"~/cmd\","
                          "\"modes\":[\"off\",\"heat\"],"
                          "\"name\":\"Thermostat\","
                          "\"object_id\":\"stove_thermostat\","
                          "\"optimistic\":false,"
                          "\"payload_off\":\"CMD+OFF\","
                          "\"payload_on\":\"CMD+ON\","
                          "\"power_command_topic\":\"~/cmd\","
                          "\"temperature_command_template\":\"SET+SETP+{{ value|int }}\","
                          "\"temperature_command_topic\":\"~/cmd\","
                          "\"temperature_unit\":\"C\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;

  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("action_template")] = F("{% set intSTATUS = int(value) %}{{ iif((1 < intSTATUS < 9) or intSTATUS == 11, 'heating', iif(intSTATUS > 0, 'idle', 'off')) }}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("action_template")] = F("{% set intSTATUS = int(value_json.STATUS) %}{{ iif((1 < intSTATUS < 9) or intSTATUS == 11, 'heating', iif(intSTATUS > 0, 'idle', 'off')) }}");

  json[F("action_topic")] = statusTopicList[_ha.mqtt.type];
  json[F("availability")] = serialized(availabilityJSON);
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("current_temperature_template")] = String(F("{{ value_json.T")) + (char)('1' + probeNumber) + F(" }}");
  json[F("current_temperature_topic")] = tempProbeTopicListArray[probeNumber][_ha.mqtt.type];
  json[F("device")] = serialized(device);

  if (hasRoomFan)
  {

    json[F("fan_mode_command_template")] = F("SET+RFAN+{{ {'off':0,'1':1,'2':2,'3':3,'4':4,'5':5,'high':6,'auto':7}[value] }}");
    json[F("fan_mode_command_topic")] = F("~/cmd");
    if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
      json[F("fan_mode_state_template")] = F("{{ ['off',1,2,3,4,5,'high','auto'][int(value)] }}");
    else if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("fan_mode_state_template")] = F("{{ ['off',1,2,3,4,5,'high','auto'][int(value_json.F2L)] }}");
    json[F("fan_mode_state_topic")] = f2lTopicList[_ha.mqtt.type];

    json[F("fan_modes")] = serialized((isAirType && hasFanAuto) ? F("[\"off\",\"1\",\"2\",\"3\",\"4\",\"5\",\"high\",\"auto\"]") : F("[\"off\",\"1\",\"2\",\"3\",\"4\",\"5\",\"high\"]"));
  }

  // Adjust max_temp for stove with air temperature setPoint, goal is to center the range around 19°C (Does someone really wants its room at 51°C ...)
  json[F("max_temp")] = (isHydroType && (staticData.UICONFIG == 1 || staticData.UICONFIG == 3 || staticData.UICONFIG == 4)) ? staticData.SPLMAX : staticData.SPLMIN + 2 * (19 - staticData.SPLMIN);
  json[F("min_temp")] = staticData.SPLMIN;

  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("mode_state_template")] = F("{{ iif(int(value) > 0, 'heat', 'off') }}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("mode_state_template")] = F("{{ iif(int(value_json.STATUS) > 0, 'heat', 'off') }}");

  json[F("mode_state_topic")] = statusTopicList[_ha.mqtt.type];
  // modes already in deserialized JSON

  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("temperature_state_template")] = F("{{ value_json.SETP }}");
  json[F("temperature_state_topic")] = setpTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  // T1 probe config is fixed for hydro type stove
  if (isHydroType)
  {
    //
    // Supply Water temperature entity
    //

    uniqueId = uniqueIdPrefixStove + F("_SupplyWaterTemp");

    topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

    // prepare payload for Stove supply water temperature sensor
    deserializeJson(json, F("{"
                            "\"default_entity_id\":\"sensor.stove_supplywatertemp\","
                            "\"device_class\":\"temperature\","
                            "\"name\":\"Supply Water Temperature\","
                            "\"object_id\":\"stove_supplywatertemp\","
                            "\"suggested_display_precision\":1,"
                            "\"state_class\":\"measurement\","
                            "\"unit_of_measurement\":\"°C\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::Generic)
      json[F("state_topic")] = F("~/T1");
    else if (_ha.mqtt.type == HaMqttType::GenericJson)
    {
      json[F("state_topic")] = F("~/TMPS");
      json[F("value_template")] = F("{{ value_json.T1 }}");
    }
    else if (_ha.mqtt.type == HaMqttType::GenericCategorized)
      json[F("state_topic")] = F("~/TMPS/T1");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // Room/Tank Water/Return Water temperature entity
  //

  // define probe number
  probeNumber = staticData.MAINTPROBE; // default case covering AirType and other HydroType
  if (isHydroType)
  {
    if (staticData.UICONFIG == 1)
      probeNumber = 1; // T2
    else if (staticData.UICONFIG == 10)
      probeNumber = 4; // T5
  }

  // define sensor name
  const __FlashStringHelper *tempSensorNameList[] = {F("Room"), F("Return Water"), F("Tank Water")};
  byte tempSensorNameIndex = 0; // default case covering AirType
  if (isHydroType)
  {
    if (staticData.UICONFIG == 1)
      tempSensorNameIndex = 1; // Return Water
    else if (staticData.UICONFIG == 3 || staticData.UICONFIG == 4)
      tempSensorNameIndex = 2; // Tank Water
  }

  uniqueId = uniqueIdPrefixStove + '_' + tempSensorNameList[tempSensorNameIndex] + F("Temp");
  uniqueId.replace(F(" "), "");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove main temperature sensor
  deserializeJson(json, F("{"
                          "\"device_class\":\"temperature\","
                          "\"suggested_display_precision\":1,"
                          "\"state_class\":\"measurement\","
                          "\"unit_of_measurement\":\"°C\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  String defaultEntityIdSuffix = tempSensorNameList[tempSensorNameIndex];
  defaultEntityIdSuffix.replace(F(" "), "");
  defaultEntityIdSuffix.toLowerCase();
  json[F("default_entity_id")] = String(F("sensor.stove_")) + defaultEntityIdSuffix + F("temp");
  json[F("device")] = serialized(device);
  json[F("name")] = String(tempSensorNameList[tempSensorNameIndex]) + F(" Temperature");
  json[F("object_id")] = String(F("stove_")) + defaultEntityIdSuffix + F("temp");
  json[F("unique_id")] = uniqueId;
  json[F("state_topic")] = tempProbeTopicListArray[probeNumber][_ha.mqtt.type];
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = String(F("{{ value_json.T")) + (char)('1' + probeNumber) + F(" }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Flue Gas temperature entity (T3)
  //

  uniqueId = uniqueIdPrefixStove + F("_FlueGasTemp");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove flue gas temperature sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_fluegastemp\","
                          "\"device_class\":\"temperature\","
                          "\"enabled_by_default\":false,"
                          "\"name\":\"Flue Gas Temperature\","
                          "\"object_id\":\"stove_fluegastemp\","
                          "\"suggested_display_precision\":1,"
                          "\"state_class\":\"measurement\","
                          "\"unit_of_measurement\":\"°C\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;
  json[F("state_topic")] = tempProbeTopicListArray[2][_ha.mqtt.type];
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ value_json.T3 }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Pellet consumption entity
  //

  const __FlashStringHelper *pqtTopicList[] = {F("~/PQT"), F("~/CNTR"), F("~/CNTR/PQT")};

  uniqueId = uniqueIdPrefixStove + F("_PQT");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove pellet consumption sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_pqt\","
                          "\"device_class\":\"weight\","
                          "\"icon\":\"mdi:chart-bell-curve-cumulative\","
                          "\"name\":\"Pellet Consumed\","
                          "\"object_id\":\"stove_pqt\","
                          "\"state_class\":\"total_increasing\","
                          "\"unit_of_measurement\":\"kg\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("state_topic")] = pqtTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ value_json.PQT }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Service time counter entity
  //

  const __FlashStringHelper *serviceTimeTopicList[] = {F("~/SERVICETIME"), F("~/CNTR"), F("~/CNTR/SERVICETIME")};

  uniqueId = uniqueIdPrefixStove + F("_ServiceTimeCounter");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove service time counter sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_servicetimecounter\","
                          "\"icon\":\"mdi:account-wrench-outline\","
                          "\"name\":\"Service Time Counter\","
                          "\"object_id\":\"stove_servicetimecounter\","
                          "\"state_class\":\"total_increasing\","
                          "\"unit_of_measurement\":\"h\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("state_topic")] = serviceTimeTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;
  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("value_template")] = F("{{ value.split(':')[0] }}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ value_json.SERVICETIME.split(':')[0] }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Feeder entity
  //

  const __FlashStringHelper *feederTopicList[] = {F("~/FDR"), F("~/POWR"), F("~/POWR/FDR")};

  uniqueId = uniqueIdPrefixStove + F("_Feeder");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove feeder sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_feeder\","
                          "\"enabled_by_default\":false,"
                          "\"entity_category\":\"diagnostic\","
                          "\"name\":\"Feeder\","
                          "\"object_id\":\"stove_feeder\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("state_topic")] = feederTopicList[_ha.mqtt.type];
  json[F("unique_id")] = uniqueId;
  if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ value_json.FDR }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Target Differential Pressure entity
  //

  const __FlashStringHelper *dpTargetTopicList[] = {F("~/DP_TARGET"), F("~/DPRS"), F("~/DPRS/DP_TARGET")};

  uniqueId = uniqueIdPrefixStove + F("_TargetDifferentialPressure");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove target differential pressure sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_targetdifferentialpressure\","
                          "\"device_class\":\"pressure\","
                          "\"enabled_by_default\":false,"
                          "\"entity_category\":\"diagnostic\","
                          "\"name\":\"Target Differential Pressure\","
                          "\"object_id\":\"stove_targetdifferentialpressure\","
                          "\"state_class\":\"measurement\","
                          "\"unit_of_measurement\":\"mPa\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;
  json[F("state_topic")] = dpTargetTopicList[_ha.mqtt.type];
  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("value_template")] = F("{{ (int(value) * 1000 / 60) | round }}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ (int(value_json.DP_TARGET) * 1000 /60) | round }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // Differential Pressure entity
  //

  const __FlashStringHelper *dpTopicList[] = {F("~/DP_PRESS"), F("~/DPRS"), F("~/DPRS/DP_PRESS")};

  uniqueId = uniqueIdPrefixStove + F("_DifferentialPressure");

  topic = prepareHassDiscoveryTopic(F("sensor"), uniqueId);

  // prepare payload for Stove differential pressure sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor.stove_differentialpressure\","
                          "\"device_class\":\"pressure\","
                          "\"enabled_by_default\":false,"
                          "\"entity_category\":\"diagnostic\","
                          "\"name\":\"Differential Pressure\","
                          "\"object_id\":\"stove_differentialpressure\","
                          "\"state_class\":\"measurement\","
                          "\"unit_of_measurement\":\"mPa\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;
  json[F("state_topic")] = dpTopicList[_ha.mqtt.type];
  if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
    json[F("value_template")] = F("{{ (int(value) * 1000 / 60) | round }}");
  else if (_ha.mqtt.type == HaMqttType::GenericJson)
    json[F("value_template")] = F("{{ (int(value_json.DP_PRESS) * 1000 / 60) | round }}");

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  //
  // OnOff entity
  //

  if (hasOnOff)
  {
    uniqueId = uniqueIdPrefixStove + F("_ON_OFF");

    topic = prepareHassDiscoveryTopic(F("switch"), uniqueId);

    // prepare payload for Stove onoff switch
    deserializeJson(json, F("{"
                            "\"command_topic\":\"~/cmd\","
                            "\"default_entity_id\":\"switch.stove_on_off\","
                            "\"icon\":\"mdi:power\","
                            "\"name\":\"On/Off\","
                            "\"object_id\":\"stove_on_off\","
                            "\"payload_off\":\"CMD+OFF\","
                            "\"payload_on\":\"CMD+ON\","
                            "\"state_off\":\"OFF\","
                            "\"state_on\":\"ON\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("state_topic")] = statusTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
      json[F("value_template")] = F("{{ iif(int(value) > 1 and int(value) != 10, 'ON', 'OFF') }}");
    else if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ iif(int(value_json.STATUS) > 1 and int(value_json.STATUS) != 10, 'ON', 'OFF') }}");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // SetPoint entity
  //

  if (hasSetPoint)
  {
    uniqueId = uniqueIdPrefixStove + F("_SETP");

    topic = prepareHassDiscoveryTopic(F("number"), uniqueId);

    // prepare payload for Stove setpoint number
    deserializeJson(json, F("{"
                            "\"command_template\":\"SET+SETP+{{ value }}\","
                            "\"command_topic\":\"~/cmd\","
                            "\"default_entity_id\":\"number.stove_setp\","
                            "\"device_class\":\"temperature\","
                            "\"mode\":\"slider\","
                            "\"name\":\"SetPoint\","
                            "\"object_id\":\"stove_setp\","
                            "\"unit_of_measurement\":\"°C\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("min")] = staticData.SPLMIN;
    json[F("max")] = staticData.SPLMAX;
    json[F("state_topic")] = setpTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ value_json.SETP }}");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // Power entity
  //

  const __FlashStringHelper *pwrTopicList[] = {F("~/PWR"), F("~/POWR"), F("~/POWR/PWR")};

  if (hasPower)
  {
    uniqueId = uniqueIdPrefixStove + F("_PWR");

    topic = prepareHassDiscoveryTopic(F("number"), uniqueId);

    // prepare payload for Stove power number
    deserializeJson(json, F("{"
                            "\"command_template\":\"SET+POWR+{{ value }}\","
                            "\"command_topic\":\"~/cmd\","
                            "\"default_entity_id\":\"number.stove_pwr\","
                            "\"icon\":\"mdi:signal\","
                            "\"min\":1,"
                            "\"max\":5,"
                            "\"mode\":\"slider\","
                            "\"name\":\"Power\","
                            "\"object_id\":\"stove_pwr\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("state_topic")] = pwrTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ value_json.PWR }}");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // RoomFan entity
  //

  if (hasRoomFan)
  {
    uniqueId = uniqueIdPrefixStove + F("_RFAN");

    topic = prepareHassDiscoveryTopic(F("number"), uniqueId);

    // prepare payload for Stove room fan
    deserializeJson(json, F("{"
                            "\"availability_mode\":\"all\","
                            "\"command_template\":\"SET+RFAN+{{ value }}\","
                            "\"command_topic\":\"~/cmd\","
                            "\"default_entity_id\":\"number.stove_rfan\","
                            "\"icon\":\"mdi:fan\","
                            "\"min\":0,"
                            "\"max\":6,"
                            "\"name\":\"Room Fan\","
                            "\"object_id\":\"stove_rfan\","
                            "\"payload_reset\":\"7\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;

    // specific availibility for room fan
    JsonArray availability = json["availability"].to<JsonArray>();

    JsonObject availability_0 = availability.add<JsonObject>();
    availability_0["topic"] = F("~/connected");
    availability_0["value_template"] = F("{{ iif(int(value) > 0, 'online', 'offline') }}");

    JsonObject availability_1 = availability.add<JsonObject>();
    availability_1["topic"] = f2lTopicList[_ha.mqtt.type];
    if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
      availability_1["value_template"] = F("{{ iif(int(value) < 7, 'online', 'offline') }}");
    else if (_ha.mqtt.type == HaMqttType::GenericJson)
      availability_1["value_template"] = F("{{ iif(int(value_json.F2L) < 7, 'online', 'offline') }}");

    json[F("device")] = serialized(device);
    json[F("state_topic")] = f2lTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ value_json.F2L }}");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // RoomFan Auto entity
  //

  if (isAirType && hasFanAuto)
  {
    uniqueId = uniqueIdPrefixStove + F("_RFAN_Auto");

    topic = prepareHassDiscoveryTopic(F("switch"), uniqueId);

    // prepare payload for Stove room fan auto mode
    deserializeJson(json, F("{"
                            "\"command_topic\":\"~/cmd\","
                            "\"default_entity_id\":\"switch.stove_rfan_auto\","
                            "\"icon\":\"mdi:fan-auto\","
                            "\"name\":\"Room Fan Auto\","
                            "\"object_id\":\"stove_rfan_auto\","
                            "\"payload_off\":\"SET+RFAN+3\","
                            "\"payload_on\":\"SET+RFAN+7\","
                            "\"state_off\":\"OFF\","
                            "\"state_on\":\"ON\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("state_topic")] = f2lTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::Generic || _ha.mqtt.type == HaMqttType::GenericCategorized)
      json[F("value_template")] = F("{{ iif(int(value) == 7, 'ON', 'OFF') }}");
    else if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ iif(int(value_json.F2L) == 7, 'ON', 'OFF') }}");

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // Fan3 entity
  //

  if (hasFan3)
  {
    const __FlashStringHelper *f3lTopicList[] = {F("~/F3L"), F("~/FAND"), F("~/FAND/F3L")};

    uniqueId = uniqueIdPrefixStove + F("_FAN3");

    // entity type depends on Min and Max value of FAN3
    topic = prepareHassDiscoveryTopic(ifFan3SwitchEntity ? F("switch") : F("number"), uniqueId);

    // prepare payload for Stove fan3 number
    deserializeJson(json, F("{"
                            "\"command_topic\":\"~/cmd\","
                            "\"icon\":\"mdi:fan-speed-2\","
                            "\"name\":\"Left Fan\","
                            "\"object_id\":\"stove_fan3\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("state_topic")] = f3lTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ value_json.F3L }}");

    // add entity type specific configuration
    if (ifFan3SwitchEntity)
    {
      json[F("default_entity_id")] = F("switch.stove_fan3");
      json[F("payload_off")] = F("SET+FN3L+0");
      json[F("payload_on")] = F("SET+FN3L+1");
      json[F("state_off")] = F("0");
      json[F("state_on")] = F("1");
    }
    else
    {
      json[F("default_entity_id")] = F("number.stove_fan3");
      json[F("command_template")] = F("SET+FN3L+{{ value }}");
      json[F("min")] = allStatusData.FANLMINMAX[2];
      json[F("max")] = allStatusData.FANLMINMAX[3];
      json[F("mode")] = F("slider");
    }

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // Fan4 entity
  //

  if (hasFan4)
  {
    const __FlashStringHelper *f4lTopicList[] = {F("~/F4L"), F("~/FAND"), F("~/FAND/F4L")};

    uniqueId = uniqueIdPrefixStove + F("_FAN4");

    // entity type depends on Min and Max value of FAN4
    topic = prepareHassDiscoveryTopic(ifFan4SwitchEntity ? F("switch") : F("number"), uniqueId);

    // prepare payload for Stove fan4 number
    deserializeJson(json, F("{"
                            "\"command_topic\":\"~/cmd\","
                            "\"icon\":\"mdi:fan-speed-3\","
                            "\"name\":\"Right Fan\","
                            "\"object_id\":\"stove_fan4\""
                            "}"));
    json[F("~")] = _preparedMqttBaseTopic;
    json[F("availability")] = serialized(availabilityJSON);
    json[F("device")] = serialized(device);
    json[F("state_topic")] = f4lTopicList[_ha.mqtt.type];
    json[F("unique_id")] = uniqueId;
    if (_ha.mqtt.type == HaMqttType::GenericJson)
      json[F("value_template")] = F("{{ value_json.F4L }}");

    // add entity type specific configuration
    if (ifFan4SwitchEntity)
    {
      json[F("default_entity_id")] = F("switch.stove_fan4");
      json[F("payload_off")] = F("SET+FN4L+0");
      json[F("payload_on")] = F("SET+FN4L+1");
      json[F("state_off")] = F("0");
      json[F("state_on")] = F("1");
    }
    else
    {
      json[F("default_entity_id")] = F("number.stove_fan4");
      json[F("command_template")] = F("SET+FN4L+{{ value }}");
      json[F("min")] = allStatusData.FANLMINMAX[4];
      json[F("max")] = allStatusData.FANLMINMAX[5];
      json[F("mode")] = F("slider");
    }

    // publish
    _mqttMan.publish(topic.c_str(), json, true);
  }

  //
  // Set Time entity
  //

  uniqueId = uniqueIdPrefixStove + F("_SET_TIME");

  topic = prepareHassDiscoveryTopic(F("button"), uniqueId);

  // prepare payload for Stove set time button
  deserializeJson(json, F("{"
                          "\"command_template\":\"SET+TIME+{{ now().strftime('%Y-%m-%d+%H:%M:%S') }}\","
                          "\"command_topic\":\"~/cmd\","
                          "\"default_entity_id\":\"button.stove_set_time\","
                          "\"entity_category\":\"diagnostic\","
                          "\"icon\":\"mdi:clock-outline\","
                          "\"name\":\"Set Time\","
                          "\"object_id\":\"stove_set_time\""
                          "}"));
  json[F("~")] = _preparedMqttBaseTopic;
  json[F("availability")] = serialized(availabilityJSON);
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;

  // publish
  _mqttMan.publish(topic.c_str(), json, true);

  return true;
}

bool WPalaControl::mqttPublishUpdate()
{
  if (!_mqttMan.connected())
    return false;

  // get update info
  JsonDocument updateInfo;
  fillLatestUpdateInfoJson(updateInfo);

  String topic;

  // This part of code is necessary because "payload_install" is not a template
  // and we need to get the version to install pushed from Home Assistant
  // so it is mandatory to update the Update entity each time we publish the update
  // parse JSON
  if (_ha.mqtt.hassDiscoveryEnabled)
  {
    JsonVariant jv;
    // if latest_version is available
    if ((jv = updateInfo[F("latest_version")]).is<const char *>())
    {
      // get version
      char version[10] = {0};
      strlcpy(version, jv.as<const char *>(), sizeof(version));

      // then publish updated Update autodiscovery

      // variables
      JsonDocument json;
      String device;

      String uniqueIdPrefix;
      String uniqueId;

      // prepare unique id prefix
      uniqueIdPrefix = F(CUSTOM_APP_MODEL "_");
      uniqueIdPrefix += WiFi.macAddress();
      uniqueIdPrefix.replace(":", "");

      // ---------- Device ----------

      // prepare device JSON
      deserializeJson(json, F("{"
                              "\"configuration_url\":\"http://" CUSTOM_APP_MODEL ".local\","
                              "\"manufacturer\":\"" CUSTOM_APP_MANUFACTURER "\","
                              "\"model\":\"" CUSTOM_APP_MODEL "\","
                              "\"sw_version\":\"" VERSION "\""
                              "}"));
      json[F("identifiers")][0] = uniqueIdPrefix;
      json[F("name")] = WiFi.getHostname();
      serializeJson(json, device); // serialize to device String

      const __FlashStringHelper *availabilityJSON = F("{\"topic\":\"~/connected\",\"value_template\":\"{{ iif(int(value) > 0, 'online', 'offline') }}\"}");

      // ----- Entities -----

      //
      // Update entity
      //

      // prepare uniqueId, topic and payload for update sensor
      uniqueId = uniqueIdPrefix;
      uniqueId += F("_Update");

      topic = _ha.mqtt.hassDiscoveryPrefix;
      topic += F("/update/");
      topic += uniqueId;
      topic += F("/config");

      // prepare payload for update sensor
      deserializeJson(json, F("{"
                              "\"command_topic\":\"~/update/install\","
                              "\"default_entity_id\":\"update." CUSTOM_APP_MODEL "\","
                              "\"device_class\":\"firmware\","
                              "\"entity_category\":\"config\","
                              "\"object_id\":\"" CUSTOM_APP_MODEL "\","
                              "\"state_topic\":\"~/update\""
                              "}"));
      json[F("~")] = _preparedMqttBaseTopic;
      json[F("availability")] = serialized(availabilityJSON);
      json[F("device")] = serialized(device);
      json[F("payload_install")] = version;
      json[F("unique_id")] = uniqueId;

      // publish
      _mqttMan.publish(topic.c_str(), json, true);
    }
  }

  // calculate topic
  topic = _preparedMqttBaseTopic;
  topic += F("/update");

  // publish install in_progress (new in 2024.11)
  // I keep it here because I want to separate the two publish for retrocompatibility
  // if "in_progress" is in the same payload, Home Assistant 2024.10 and lower will ignore the payload
  // (to be moved to WBase around 2025-05)
  _mqttMan.publish(topic.c_str(), (String(F("{\"in_progress\":")) + (Update.isRunning() ? F("true") : F("false")) + '}').c_str(), true);

  // publish update info
  _mqttMan.publish(topic.c_str(), updateInfo, true);

  return true;
}

bool WPalaControl::executePalaCmd(const String &cmd, JsonDocument &jsonDoc, bool publish /* = false*/)
{
  bool cmdProcessed = false;                                                             // cmd has been processed
  Palazzetti::CommandResult cmdSuccess = Palazzetti::CommandResult::COMMUNICATION_ERROR; // Palazzetti function calls successful

  // Prepare answer structure --------------------------------------------------
  jsonDoc.clear();
  JsonObject info = jsonDoc["INFO"].to<JsonObject>();
  JsonObject data = jsonDoc["DATA"].to<JsonObject>();
  const __FlashStringHelper *palaCategory = F(""); // used to return data to the correct MQTT category (if needed)

  // Parse parameters ----------------------------------------------------------
  byte cmdParamNumber = 0;
  uint16_t cmdParams[6] = {};
  char paramBuf[40] = {};

  // if cmd length means there is parameters
  if (cmd.length() > 9 && cmd[8] == ' ')
  {
    // check parameters length before copying to a temporary buffer
    const char *rawParams = cmd.c_str() + 9;
    if (strlen(rawParams) >= sizeof(paramBuf))
    {
      cmdProcessed = true;
      info["MSG"] = F("Parameters too long");
    }

    if (!cmdProcessed)
    {
      // copy parameters to a temporary buffer for parsing
      strlcpy(paramBuf, rawParams, sizeof(paramBuf));

      // trim leading spaces
      char *start = paramBuf;
      while (*start == ' ')
        start++;
      if (start != paramBuf)
        memmove(paramBuf, start, strlen(start) + 1);

      // trim trailing spaces
      int len = strlen(paramBuf);
      while (len > 0 && paramBuf[len - 1] == ' ')
        paramBuf[--len] = '\0';

      // special case SET TIME: replace '-' and ':' with ' '
      if (cmd.startsWith(F("SET TIME ")))
      {
        for (char *q = paramBuf; *q; q++)
          if (*q == '-' || *q == ':')
            *q = ' ';
      }

      // special case SET STPF: replace '.' with ' '
      if (cmd.startsWith(F("SET STPF ")))
      {
        for (char *q = paramBuf; *q; q++)
          if (*q == '.')
            *q = ' ';
      }

      // collapse consecutive spaces into one
      char *r = paramBuf, *w = paramBuf;
      while (*r)
      {
        *w++ = *r++;
        if (r[-1] == ' ')
          while (*r == ' ')
            r++;
      }
      *w = '\0';

      // split parameters by space and convert to integer
      char *tok = strtok(paramBuf, " ");
      while (tok && cmdParamNumber < 6)
      {
        char *endPtr = nullptr;
        long parsedValue = strtol(tok, &endPtr, 10);

        // special case SET STPF (second parameter is the decimal part) (handle 19.8 instead of 19.80)
        if (cmdParamNumber == 1 && cmd.startsWith(F("SET STPF ")) && strlen(tok) == 1)
          parsedValue *= 10;

        // special case EXT ADRD and EXT ADWR (first parameter is an hexadecimal address)
        if (cmdParamNumber == 0 && (cmd.startsWith(F("EXT ADRD ")) || cmd.startsWith(F("EXT ADWR "))))
          parsedValue = strtol(tok, &endPtr, 16);

        // Conversion is valid only if the full token is numeric and the value fits uint16_t.
        if (endPtr != tok && *endPtr == '\0' && parsedValue >= 0 && parsedValue <= 0xFFFF)
          cmdParams[cmdParamNumber] = parsedValue;
        else
        {
          cmdProcessed = true;
          info["MSG"] = (String(F("Incorrect Parameter Value : ")) + tok).c_str();
          break;
        }

        cmdParamNumber++;
        tok = strtok(NULL, " ");
      }

      // too much parameters has been sent
      if (!cmdProcessed && tok)
      {
        cmdProcessed = true;
        info["MSG"] = F("Incorrect Parameter Number");
      }
    }
  }

  // Dispatch command ---------------------------------------------------------

  if (!cmdProcessed)
  {
    if (cmd.startsWith(F("CMD ")))
      cmdSuccess = executePalaCmdCmd(cmd, data, info, palaCategory, cmdProcessed);
    else if (cmd.startsWith(F("GET ")))
      cmdSuccess = executePalaCmdGet(cmd, data, info, palaCategory, cmdProcessed, cmdParamNumber, cmdParams);
    else if (cmd.startsWith(F("SET ")))
      cmdSuccess = executePalaCmdSet(cmd, data, info, palaCategory, cmdProcessed, cmdParamNumber, cmdParams);
    else if (cmd.startsWith(F("EXT ")))
      cmdSuccess = executePalaCmdExt(cmd, data, info, palaCategory, cmdProcessed, cmdParamNumber, cmdParams);
  }

  // Process result -----------------------------------------------------------

  // if command has been processed
  if (cmdProcessed)
  {

    // if MQTT protocol is enabled then update connected topic to reflect stove connectivity
    if (_ha.protocol == HaProtocol::Mqtt)
      mqttPublishStoveConnected(cmdSuccess == Palazzetti::CommandResult::OK);

    // if communication with stove was successful
    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      info["CMD"] = cmd.substring(0, 8);

      info["RSP"] = F("OK");
      jsonDoc["SUCCESS"] = true;

      if (publish && String(palaCategory).length() > 0)
      {
        _eventSourceMan.eventSourceBroadcast(data);

        if (_ha.protocol == HaProtocol::Mqtt && _haSendResult)
          _haSendResult &= mqttPublishData(_preparedMqttBaseTopic, palaCategory, jsonDoc);
      }
    }
    else
    {
      info["CMD"] = cmd;

      // if there is no MSG in info then stove communication failed
      if (info["MSG"].isNull())
      {
        info["RSP"] = F("TIMEOUT");
        info["MSG"] = F("Stove communication failed");
      }
      else
        info["RSP"] = F("ERROR");

      jsonDoc["SUCCESS"] = false;
      data["NODATA"] = true;
    }
  }
  else
  {
    // command is unknown and not processed
    info["RSP"] = F("ERROR");
    info["CMD"] = F("UNKNOWN");
    info["MSG"] = F("No valid request received");
    jsonDoc["SUCCESS"] = false;
    data["NODATA"] = true;
  }

  return jsonDoc["SUCCESS"].as<bool>();
}

Palazzetti::CommandResult WPalaControl::executePalaCmdCmd(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed)
{
  Palazzetti::CommandResult cmdSuccess = Palazzetti::CommandResult::COMMUNICATION_ERROR;

  if (cmd == F("CMD OFF"))
  {
    cmdProcessed = true;
    palaCategory = F("STAT");

    Palazzetti::StatusData statusData;
    cmdSuccess = _Pala.switchOff(&statusData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["STATUS"] = statusData.STATUS;
      data["LSTATUS"] = statusData.LSTATUS;
      data["FSTATUS"] = statusData.FSTATUS;
    }
  }
  else if (cmd == F("CMD ON"))
  {
    cmdProcessed = true;
    palaCategory = F("STAT");

    Palazzetti::StatusData statusData;
    cmdSuccess = _Pala.switchOn(&statusData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["STATUS"] = statusData.STATUS;
      data["LSTATUS"] = statusData.LSTATUS;
      data["FSTATUS"] = statusData.FSTATUS;
    }
  }

  return cmdSuccess;
}

Palazzetti::CommandResult WPalaControl::executePalaCmdGet(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams)
{
  Palazzetti::CommandResult cmdSuccess = Palazzetti::CommandResult::COMMUNICATION_ERROR;
  char floatBuf[8];

  if (cmd == F("GET ALLS"))
  {
    cmdProcessed = true;
    palaCategory = F("ALLS");

    bool refreshStatus = false;
    unsigned long currentMillis = millis();
    if ((currentMillis - _lastAllStatusRefreshMillis) > 15000UL) // refresh AllStatus data if it's 15sec old
      refreshStatus = true;

    Palazzetti::AllStatusData allStatusData;
    cmdSuccess = _Pala.getAllStatus(refreshStatus, allStatusData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      if (refreshStatus)
        _lastAllStatusRefreshMillis = currentMillis;

      data["MBTYPE"] = allStatusData.MBTYPE;
      data["MAC"] = WiFi.macAddress();
      data["MOD"] = allStatusData.MOD;
      data["VER"] = allStatusData.VER;
      data["CORE"] = allStatusData.CORE;
      data["FWDATE"] = allStatusData.FWDATE;
      data["APLTS"] = allStatusData.APLTS;
      data["APLWDAY"] = allStatusData.APLWDAY;
      data["CHRSTATUS"] = allStatusData.CHRSTATUS;
      data["STATUS"] = allStatusData.STATUS;
      data["LSTATUS"] = allStatusData.LSTATUS;
      data["FSTATUS"] = allStatusData.FSTATUS;
      if (allStatusData.isMFSTATUSValid)
        data["MFSTATUS"] = allStatusData.MFSTATUS;
      dtostrf(allStatusData.SETP, 1, 2, floatBuf);
      data["SETP"] = serialized(floatBuf);
      data["PUMP"] = allStatusData.PUMP;
      data["PQT"] = allStatusData.PQT;
      data["F1V"] = allStatusData.F1V;
      data["F1RPM"] = allStatusData.F1RPM;
      data["F2L"] = allStatusData.F2L;
      data["F2LF"] = allStatusData.F2LF;
      JsonArray fanlminmax = data["FANLMINMAX"].to<JsonArray>();
      fanlminmax.add(allStatusData.FANLMINMAX[0]);
      fanlminmax.add(allStatusData.FANLMINMAX[1]);
      fanlminmax.add(allStatusData.FANLMINMAX[2]);
      fanlminmax.add(allStatusData.FANLMINMAX[3]);
      fanlminmax.add(allStatusData.FANLMINMAX[4]);
      fanlminmax.add(allStatusData.FANLMINMAX[5]);
      data["F2V"] = allStatusData.F2V;
      if (allStatusData.isF3LF4LValid)
      {
        data["F3L"] = allStatusData.F3L;
        data["F4L"] = allStatusData.F4L;
      }
      data["PWR"] = allStatusData.PWR;
      dtostrf(allStatusData.FDR, 1, 2, floatBuf);
      data["FDR"] = serialized(floatBuf);
      data["DPT"] = allStatusData.DPT;
      data["DP"] = allStatusData.DP;
      data["IN"] = allStatusData.IN;
      if (allStatusData.isPLEVELValid)
        data["PLEVEL"] = allStatusData.PLEVEL;
      if (allStatusData.isPSENSCSTALEMPValid)
      {
        data["PSENSCSTA"] = allStatusData.PSENSCSTA;
        data["PSENSLEMP"] = allStatusData.PSENSLEMP;
      }
      data["OUT"] = allStatusData.OUT;
      dtostrf(allStatusData.T1, 1, 2, floatBuf);
      data["T1"] = serialized(floatBuf);
      dtostrf(allStatusData.T2, 1, 2, floatBuf);
      data["T2"] = serialized(floatBuf);
      dtostrf(allStatusData.T3, 1, 2, floatBuf);
      data["T3"] = serialized(floatBuf);
      dtostrf(allStatusData.T4, 1, 2, floatBuf);
      data["T4"] = serialized(floatBuf);
      dtostrf(allStatusData.T5, 1, 2, floatBuf);
      data["T5"] = serialized(floatBuf);

      data["EFLAGS"] = allStatusData.EFLAGS;
      if (allStatusData.isSNValid)
        data["SN"] = allStatusData.SN;
    }
  }
  else if (cmd == F("GET CHRD"))
  {
    cmdProcessed = true;
    palaCategory = F("CHRD");

    Palazzetti::ChronoData chronoData;
    cmdSuccess = _Pala.getChronoData(chronoData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["CHRSTATUS"] = chronoData.CHRSTATUS;

      // Add Programs (P1->P6)
      char programName[3] = {'P', 'X', 0};
      char time[6] = {'0', '0', ':', '0', '0', 0};
      for (byte i = 0; i < 6; i++)
      {
        programName[1] = i + '1';
        JsonObject px = data[programName].to<JsonObject>();
        dtostrf(chronoData.PCHRSETP[i], 1, 2, floatBuf);
        px["CHRSETP"] = serialized(floatBuf);
        time[0] = chronoData.PSTART[i][0] / 10 + '0';
        time[1] = chronoData.PSTART[i][0] % 10 + '0';
        time[3] = chronoData.PSTART[i][1] / 10 + '0';
        time[4] = chronoData.PSTART[i][1] % 10 + '0';
        px["START"] = time;
        time[0] = chronoData.PSTOP[i][0] / 10 + '0';
        time[1] = chronoData.PSTOP[i][0] % 10 + '0';
        time[3] = chronoData.PSTOP[i][1] / 10 + '0';
        time[4] = chronoData.PSTOP[i][1] % 10 + '0';
        px["STOP"] = time;
      }

      // Add Days (D1->D7)
      char dayName[3] = {'D', 'X', 0};
      char memoryName[3] = {'M', 'X', 0};
      for (byte dayNumber = 0; dayNumber < 7; dayNumber++)
      {
        dayName[1] = dayNumber + '1';
        JsonObject dx = data[dayName].to<JsonObject>();
        for (byte memoryNumber = 0; memoryNumber < 3; memoryNumber++)
        {
          memoryName[1] = memoryNumber + '1';
          if (chronoData.DM[dayNumber][memoryNumber])
          {
            programName[1] = chronoData.DM[dayNumber][memoryNumber] + '0';
            dx[memoryName] = programName;
          }
          else
            dx[memoryName] = F("OFF");
        }
      }
    }
  }
  else if (cmd == F("GET CNTR") || cmd == F("GET CUNT"))
  {
    cmdProcessed = true;
    palaCategory = F("CNTR");

    Palazzetti::CountersData countersData;
    cmdSuccess = _Pala.getCounters(countersData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["IGN"] = countersData.IGN;
      data["POWERTIME"] = countersData.POWERTIME;
      data["HEATTIME"] = countersData.HEATTIME;
      data["SERVICETIME"] = countersData.SERVICETIME;
      data["ONTIME"] = countersData.ONTIME;
      data["OVERTMPERRORS"] = countersData.OVERTMPERRORS;
      data["IGNERRORS"] = countersData.IGNERRORS;
      data["PQT"] = countersData.PQT;
    }
  }
  else if (cmd == F("GET DPRS"))
  {
    cmdProcessed = true;
    palaCategory = F("DPRS");

    Palazzetti::DPressData dpressData;
    cmdSuccess = _Pala.getDPressData(dpressData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["DP_TARGET"] = dpressData.DP_TARGET;
      data["DP_PRESS"] = dpressData.DP_PRESS;
    }
  }
  else if (cmd == F("GET FAND"))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    Palazzetti::FanData fanData;
    cmdSuccess = _Pala.getFanData(fanData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["F1V"] = fanData.F1V;
      data["F2V"] = fanData.F2V;
      data["F1RPM"] = fanData.F1RPM;
      data["F2L"] = fanData.F2L;
      data["F2LF"] = fanData.F2LF;
      if (fanData.isF3SF4SValid)
      {
        dtostrf(fanData.F3S, 1, 2, floatBuf);
        data["F3S"] = serialized(floatBuf);
        dtostrf(fanData.F4S, 1, 2, floatBuf);
        data["F4S"] = serialized(floatBuf);
      }
      if (fanData.isF3LF4LValid)
      {
        data["F3L"] = fanData.F3L;
        data["F4L"] = fanData.F4L;
      }
    }
  }
  else if (cmd.startsWith(F("GET HPAR ")))
  {
    cmdProcessed = true;
    palaCategory = F("HPAR");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      uint16_t hiddenParamValue;
      cmdSuccess = _Pala.getHiddenParameter(cmdParams[0], &hiddenParamValue);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        String hiddenParamName("HPAR");
        hiddenParamName += cmdParams[0];
        data[hiddenParamName] = hiddenParamValue;
      }
    }
  }
  else if (cmd == F("GET IOPT"))
  {
    cmdProcessed = true;
    palaCategory = F("IOPT");

    Palazzetti::IOData ioData;
    cmdSuccess = _Pala.getIO(ioData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["IN_I01"] = ioData.IN_I01;
      data["IN_I02"] = ioData.IN_I02;
      data["IN_I03"] = ioData.IN_I03;
      data["IN_I04"] = ioData.IN_I04;
      data["OUT_O01"] = ioData.OUT_O01;
      data["OUT_O02"] = ioData.OUT_O02;
      data["OUT_O03"] = ioData.OUT_O03;
      data["OUT_O04"] = ioData.OUT_O04;
      data["OUT_O05"] = ioData.OUT_O05;
      data["OUT_O06"] = ioData.OUT_O06;
      data["OUT_O07"] = ioData.OUT_O07;
    }
  }
  else if (cmd == F("GET LABL"))
  {
    cmdProcessed = true;
    palaCategory = F("LABL");
    cmdSuccess = Palazzetti::CommandResult::OK;

    data["LABEL"] = WiFi.getHostname();
  }
  else if (cmd == F("GET MDVE"))
  {
    cmdProcessed = true;
    palaCategory = F("MDVE");

    Palazzetti::ModelVersionData modelVersionData;
    cmdSuccess = _Pala.getModelVersion(modelVersionData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["MOD"] = modelVersionData.MOD;
      data["VER"] = modelVersionData.VER;
      data["CORE"] = modelVersionData.CORE;
      data["FWDATE"] = modelVersionData.FWDATE;
    }
  }
  else if (cmd.startsWith(F("GET PARM ")))
  {
    cmdProcessed = true;
    palaCategory = F("PARM");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      byte paramValue;
      cmdSuccess = _Pala.getParameter(cmdParams[0], &paramValue);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        String paramName("PAR");
        paramName += cmdParams[0];
        data[paramName] = paramValue;
      }
    }
  }
  else if (cmd == F("GET SETP"))
  {
    cmdProcessed = true;
    palaCategory = F("SETP");

    Palazzetti::SetPointData setPointData;
    cmdSuccess = _Pala.getSetPoint(setPointData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      dtostrf(setPointData.SETP, 1, 2, floatBuf);
      data["SETP"] = serialized(floatBuf);
      dtostrf(setPointData.SECO, 1, 2, floatBuf);
      data["SECO"] = serialized(floatBuf);
      data["BECO"] = setPointData.BECO;
    }
  }
  else if (cmd == F("GET STAT"))
  {
    cmdProcessed = true;
    palaCategory = F("STAT");

    Palazzetti::StatusData statusData;
    cmdSuccess = _Pala.getStatus(statusData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["STATUS"] = statusData.STATUS;
      data["LSTATUS"] = statusData.LSTATUS;
      data["FSTATUS"] = statusData.FSTATUS;
    }
  }
  else if (cmd == F("GET STDT"))
  {
    cmdProcessed = true;
    palaCategory = F("STDT");

    Palazzetti::StaticData staticData;
    cmdSuccess = _Pala.getStaticData(staticData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      // ----- WPalaControl generated values -----
      data["LABEL"] = WiFi.getHostname();

      // Network infos
      data["GWDEVICE"] = F("wlan0"); // always wifi
      data["MAC"] = WiFi.macAddress();
      data["GATEWAY"] = WifiMan::ipToCString(WiFi.gatewayIP());
      data["DNS"][0] = WifiMan::ipToCString(WiFi.dnsIP());

      // Wifi infos
      data["WMAC"] = WiFi.macAddress();
      data["WMODE"] = (WiFi.getMode() & WIFI_STA) ? F("sta") : F("ap");
      data["WADR"] = (WiFi.getMode() & WIFI_STA) ? WifiMan::ipToCString(WiFi.localIP()) : WifiMan::ipToCString(WiFi.softAPIP());
      data["WGW"] = WifiMan::ipToCString(WiFi.gatewayIP());
      data["WENC"] = F("psk2");
      data["WPWR"] = String(WiFi.RSSI()) + F(" dBm"); // need conversion to dBm?
      data["WSSID"] = WiFi.SSID();
      data["WPR"] = (true) ? F("dhcp") : F("static");
      data["WMSK"] = WifiMan::ipToCString(WiFi.subnetMask());
      data["WBCST"] = WifiMan::ipToCString(WiFi.broadcastIP());
      data["WCH"] = String(WiFi.channel());

      // Ethernet infos
      data["EPR"] = F("dhcp");
      data["EGW"] = F("0.0.0.0");
      data["EMSK"] = F("0.0.0.0");
      data["EADR"] = F("0.0.0.0");
      data["EMAC"] = WiFi.macAddress();
      data["ECBL"] = F("down");
      data["EBCST"] = "";

      data["APLCONN"] = 1; // appliance connected
      data["ICONN"] = 0;   // internet connected

      data["CBTYPE"] = F("miniembplug"); // CBox model
      data["sendmsg"] = F("2.1.2 2021-10-12 12:22:53");
      data["plzbridge"] = F("2.2.1 2024-11-06 15:47:08");
      data["SYSTEM"] = F("2.5.4 2024-11-06 15:50:16 (37ef0a2)");

      data["CLOUD_ENABLED"] = true;

      // ----- Values from stove -----
      data["SN"] = staticData.SN;
      data["SNCHK"] = staticData.SNCHK;
      data["MBTYPE"] = staticData.MBTYPE;
      data["MOD"] = staticData.MOD;
      data["VER"] = staticData.VER;
      data["CORE"] = staticData.CORE;
      data["FWDATE"] = staticData.FWDATE;
      data["FLUID"] = staticData.FLUID;
      data["SPLMIN"] = staticData.SPLMIN;
      data["SPLMAX"] = staticData.SPLMAX;
      data["UICONFIG"] = staticData.UICONFIG;
      data["HWTYPE"] = staticData.HWTYPE;
      data["DSPTYPE"] = staticData.DSPTYPE;
      data["DSPFWVER"] = staticData.DSPFWVER;
      data["CONFIG"] = staticData.CONFIG;
      data["PELLETTYPE"] = staticData.PELLETTYPE;
      data["PSENSTYPE"] = staticData.PSENSTYPE;
      data["PSENSLMAX"] = staticData.PSENSLMAX;
      data["PSENSLTSH"] = staticData.PSENSLTSH;
      data["PSENSLMIN"] = staticData.PSENSLMIN;
      data["MAINTPROBE"] = staticData.MAINTPROBE;
      data["STOVETYPE"] = staticData.STOVETYPE;
      data["FAN2TYPE"] = staticData.FAN2TYPE;
      data["FAN2MODE"] = staticData.FAN2MODE;
      data["BLEMBMODE"] = staticData.BLEMBMODE;
      data["BLEDSPMODE"] = staticData.BLEDSPMODE;
      data["CHRONOTYPE"] = 0; // disable chronothermostat (no planning) (enabled if > 1)
      data["AUTONOMYTYPE"] = staticData.AUTONOMYTYPE;
      data["NOMINALPWR"] = staticData.NOMINALPWR;
    }
  }
  else if (cmd == F("GET TIME"))
  {
    cmdProcessed = true;
    palaCategory = F("TIME");

    Palazzetti::DateTimeData dateTimeData;
    cmdSuccess = _Pala.getDateTime(dateTimeData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["STOVE_DATETIME"] = dateTimeData.STOVE_DATETIME;
      data["STOVE_WDAY"] = dateTimeData.STOVE_WDAY;
    }
  }
  else if (cmd == F("GET TMPS"))
  {
    cmdProcessed = true;
    palaCategory = F("TMPS");

    Palazzetti::AllTempsData allTempsData;
    cmdSuccess = _Pala.getAllTemps(allTempsData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      dtostrf(allTempsData.T1, 1, 2, floatBuf);
      data["T1"] = serialized(floatBuf);
      dtostrf(allTempsData.T2, 1, 2, floatBuf);
      data["T2"] = serialized(floatBuf);
      dtostrf(allTempsData.T3, 1, 2, floatBuf);
      data["T3"] = serialized(floatBuf);
      dtostrf(allTempsData.T4, 1, 2, floatBuf);
      data["T4"] = serialized(floatBuf);
      dtostrf(allTempsData.T5, 1, 2, floatBuf);
      data["T5"] = serialized(floatBuf);
    }
  }
  else if (cmd == F("GET POWR"))
  {
    cmdProcessed = true;
    palaCategory = F("POWR");

    Palazzetti::PowerData powerData;
    cmdSuccess = _Pala.getPower(powerData);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["PWR"] = powerData.PWR;
      dtostrf(powerData.FDR, 1, 2, floatBuf);
      data["FDR"] = serialized(floatBuf);
    }
  }
  else if (cmd == F("GET SERN"))
  {
    cmdProcessed = true;
    palaCategory = F("SERN");

    char SN[28];
    cmdSuccess = _Pala.getSN(&SN);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["SN"] = SN;
    }
  }

  return cmdSuccess;
}

Palazzetti::CommandResult WPalaControl::executePalaCmdSet(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams)
{
  Palazzetti::CommandResult cmdSuccess = Palazzetti::CommandResult::COMMUNICATION_ERROR;
  char floatBuf[8];

  if (cmd.startsWith(F("SET CDAY ")))
  {
    cmdProcessed = true;
    palaCategory = F("CHRD");

    if (cmdParamNumber != 3)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      cmdSuccess = _Pala.setChronoDay(cmdParams[0], cmdParams[1], cmdParams[2]);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        char dayName[3] = {'D', 'X', 0};
        char memoryName[3] = {'M', 'X', 0};
        char programName[3] = {'P', 'X', 0};

        dayName[1] = cmdParams[0] + '0';
        memoryName[1] = cmdParams[1] + '0';
        programName[1] = cmdParams[2] + '0';

        JsonObject dx = data[dayName].to<JsonObject>();
        if (cmdParams[2])
          dx[memoryName] = programName;
        else
          dx[memoryName] = F("OFF");
      }
    }
  }
  else if (cmd.startsWith(F("SET CPRD ")))
  {
    cmdProcessed = true;
    palaCategory = F("CHRD");

    if (info["MSG"].isNull())
    {
      cmdSuccess = _Pala.setChronoPrg(cmdParams[0], cmdParams[1], cmdParams[2], cmdParams[3], cmdParams[4], cmdParams[5]);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        char programName[3] = {'P', 'X', 0};
        char time[6] = {'0', '0', ':', '0', '0', 0};

        programName[1] = cmdParams[0] + '0';
        JsonObject px = data[programName].to<JsonObject>();
        px["CHRSETP"] = (float)cmdParams[1];
        time[0] = cmdParams[2] / 10 + '0';
        time[1] = cmdParams[2] % 10 + '0';
        time[3] = cmdParams[3] / 10 + '0';
        time[4] = cmdParams[3] % 10 + '0';
        px["START"] = time;
        time[0] = cmdParams[4] / 10 + '0';
        time[1] = cmdParams[4] % 10 + '0';
        time[3] = cmdParams[5] / 10 + '0';
        time[4] = cmdParams[5] % 10 + '0';
        px["STOP"] = time;
      }
    }
  }
  else if (cmd.startsWith(F("SET CSET ")))
  {
    cmdProcessed = true;

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
      cmdSuccess = _Pala.setChronoSetpoint(cmdParams[0], cmdParams[1]);
  }
  else if (cmd.startsWith(F("SET CSPH ")))
  {
    cmdProcessed = true;

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
      cmdSuccess = _Pala.setChronoStopHH(cmdParams[0], cmdParams[1]);
  }
  else if (cmd.startsWith(F("SET CSPM ")))
  {
    cmdProcessed = true;

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
      cmdSuccess = _Pala.setChronoStopMM(cmdParams[0], cmdParams[1]);
  }
  else if (cmd.startsWith(F("SET CSST ")))
  {
    cmdProcessed = true;
    palaCategory = F("CHRD");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      byte CHRSTATUSResult;
      cmdSuccess = _Pala.setChronoStatus(cmdParams[0], &CHRSTATUSResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["CHRSTATUS"] = CHRSTATUSResult;
      }
    }
  }
  else if (cmd.startsWith(F("SET CSTH ")))
  {
    cmdProcessed = true;

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
      cmdSuccess = _Pala.setChronoStartHH(cmdParams[0], cmdParams[1]);
  }
  else if (cmd.startsWith(F("SET CSTM ")))
  {
    cmdProcessed = true;

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
      cmdSuccess = _Pala.setChronoStartMM(cmdParams[0], cmdParams[1]);
  }
  else if (cmd == F("SET FN2D"))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    Palazzetti::SetRoomFanResult setRoomFanResult;
    cmdSuccess = _Pala.setRoomFanDown(&setRoomFanResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      if (setRoomFanResult.isPWRValid)
        data["PWR"] = setRoomFanResult.PWR;
      data["F2L"] = setRoomFanResult.F2L;
      data["F2LF"] = setRoomFanResult.F2LF;
    }
  }
  else if (cmd == F("SET FN2U"))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    Palazzetti::SetRoomFanResult setRoomFanResult;
    cmdSuccess = _Pala.setRoomFanUp(&setRoomFanResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      if (setRoomFanResult.isPWRValid)
        data["PWR"] = setRoomFanResult.PWR;
      data["F2L"] = setRoomFanResult.F2L;
      data["F2LF"] = setRoomFanResult.F2LF;
    }
  }
  else if (cmd.startsWith(F("SET FN3L ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      uint16_t F3LResult;
      cmdSuccess = _Pala.setRoomFan3(cmdParams[0], &F3LResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["F3L"] = F3LResult;
      }
    }
  }
  else if (cmd.startsWith(F("SET FN4L ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      uint16_t F4LResult;
      cmdSuccess = _Pala.setRoomFan4(cmdParams[0], &F4LResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["F4L"] = F4LResult;
      }
    }
  }
  else if (cmd.startsWith(F("SET FN3S ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      float F3SResult;
      cmdSuccess = _Pala.setSetPointFan3(cmdParams[0], &F3SResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        dtostrf(F3SResult, 1, 2, floatBuf);
        data["F3S"] = serialized(floatBuf);
      }
    }
  }
  else if (cmd.startsWith(F("SET FN4S ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      float F4SResult;
      cmdSuccess = _Pala.setSetPointFan4(cmdParams[0], &F4SResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        dtostrf(F4SResult, 1, 2, floatBuf);
        data["F4S"] = serialized(floatBuf);
      }
    }
  }
  else if (cmd.startsWith(F("SET HPAR ")))
  {
    cmdProcessed = true;
    palaCategory = F("HPAR");

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      cmdSuccess = _Pala.setHiddenParameter(cmdParams[0], cmdParams[1]);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data[String(F("HPAR")) + cmdParams[0]] = cmdParams[1];
      }
    }
  }
  else if (cmd.startsWith(F("SET PARM ")))
  {
    cmdProcessed = true;
    palaCategory = F("PARM");

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      cmdSuccess = _Pala.setParameter(cmdParams[0], cmdParams[1]);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data[String(F("PAR")) + cmdParams[0]] = cmdParams[1];
      }
    }
  }
  else if (cmd.startsWith(F("SET POWR ")))
  {
    cmdProcessed = true;
    palaCategory = F("POWR");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      Palazzetti::SetPowerResult setPowerResult;
      cmdSuccess = _Pala.setPower(cmdParams[0], &setPowerResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["PWR"] = setPowerResult.PWR;
        if (setPowerResult.isF2LValid)
          data["F2L"] = setPowerResult.F2L;
        JsonArray fanlminmax = data["FANLMINMAX"].to<JsonArray>();
        fanlminmax.add(setPowerResult.FANLMINMAX[0]);
        fanlminmax.add(setPowerResult.FANLMINMAX[1]);
        fanlminmax.add(setPowerResult.FANLMINMAX[2]);
        fanlminmax.add(setPowerResult.FANLMINMAX[3]);
        fanlminmax.add(setPowerResult.FANLMINMAX[4]);
        fanlminmax.add(setPowerResult.FANLMINMAX[5]);
      }
    }
  }
  else if (cmd == F("SET PWRD"))
  {
    cmdProcessed = true;
    palaCategory = F("POWR");

    Palazzetti::SetPowerResult setPowerResult;
    cmdSuccess = _Pala.setPowerDown(&setPowerResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["PWR"] = setPowerResult.PWR;
      if (setPowerResult.isF2LValid)
        data["F2L"] = setPowerResult.F2L;
      JsonArray fanlminmax = data["FANLMINMAX"].to<JsonArray>();
      fanlminmax.add(setPowerResult.FANLMINMAX[0]);
      fanlminmax.add(setPowerResult.FANLMINMAX[1]);
      fanlminmax.add(setPowerResult.FANLMINMAX[2]);
      fanlminmax.add(setPowerResult.FANLMINMAX[3]);
      fanlminmax.add(setPowerResult.FANLMINMAX[4]);
      fanlminmax.add(setPowerResult.FANLMINMAX[5]);
    }
  }
  else if (cmd == F("SET PWRU"))
  {
    cmdProcessed = true;
    palaCategory = F("POWR");

    Palazzetti::SetPowerResult setPowerResult;
    cmdSuccess = _Pala.setPowerUp(&setPowerResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      data["PWR"] = setPowerResult.PWR;
      if (setPowerResult.isF2LValid)
        data["F2L"] = setPowerResult.F2L;
      JsonArray fanlminmax = data["FANLMINMAX"].to<JsonArray>();
      fanlminmax.add(setPowerResult.FANLMINMAX[0]);
      fanlminmax.add(setPowerResult.FANLMINMAX[1]);
      fanlminmax.add(setPowerResult.FANLMINMAX[2]);
      fanlminmax.add(setPowerResult.FANLMINMAX[3]);
      fanlminmax.add(setPowerResult.FANLMINMAX[4]);
      fanlminmax.add(setPowerResult.FANLMINMAX[5]);
    }
  }
  else if (cmd.startsWith(F("SET RFAN ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      Palazzetti::SetRoomFanResult setRoomFanResult;
      cmdSuccess = _Pala.setRoomFan(cmdParams[0], &setRoomFanResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        if (setRoomFanResult.isPWRValid)
          data["PWR"] = setRoomFanResult.PWR;
        data["F2L"] = setRoomFanResult.F2L;
        data["F2LF"] = setRoomFanResult.F2LF;
      }
    }
  }
  else if (cmd.startsWith(F("SET SETP ")))
  {
    cmdProcessed = true;
    palaCategory = F("SETP");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      float SETPResult;
      cmdSuccess = _Pala.setSetpoint((byte)cmdParams[0], &SETPResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        dtostrf(SETPResult, 1, 2, floatBuf);
        data["SETP"] = serialized(floatBuf);
      }
    }
  }
  else if (cmd.startsWith(F("SET SLNT ")))
  {
    cmdProcessed = true;
    palaCategory = F("FAND");

    if (cmdParamNumber != 1)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    if (info["MSG"].isNull())
    {
      Palazzetti::SetSilentModeResult setSilentModeResult;
      cmdSuccess = _Pala.setSilentMode(cmdParams[0], &setSilentModeResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["SLNT"] = setSilentModeResult.SLNT;
        data["PWR"] = setSilentModeResult.PWR;
        data["F2L"] = setSilentModeResult.F2L;
        data["F2LF"] = setSilentModeResult.F2LF;
        if (setSilentModeResult.isF3LF4LValid)
        {
          data["F3L"] = setSilentModeResult.F3L;
          data["F4L"] = setSilentModeResult.F4L;
        }
      }
    }
  }
  else if (cmd == F("SET STPD"))
  {
    cmdProcessed = true;
    palaCategory = F("SETP");

    float SETPResult;
    cmdSuccess = _Pala.setSetPointDown(&SETPResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      dtostrf(SETPResult, 1, 2, floatBuf);
      data["SETP"] = serialized(floatBuf);
    }
  }
  else if (cmd.startsWith(F("SET STPF ")))
  {
    cmdProcessed = true;
    palaCategory = F("SETP");

    if (cmdParamNumber != 2)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;
    else if (cmdParams[1] > 80 || cmdParams[1] % 20 != 0)
    {
      char msgBuf[64];
      snprintf(msgBuf, sizeof(msgBuf), "Incorrect Parameter Value : %u.%02u", cmdParams[0], cmdParams[1]);
      info["MSG"] = msgBuf;
    }

    // convert splitted float string back to float
    float setPointFloat = cmdParams[1]; // load decimal part
    setPointFloat /= 100.0f;
    setPointFloat += cmdParams[0]; // load integer part

    if (info["MSG"].isNull())
    {
      float SETPResult;
      cmdSuccess = _Pala.setSetpoint(setPointFloat, &SETPResult);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        dtostrf(SETPResult, 1, 2, floatBuf);
        data["SETP"] = serialized(floatBuf);
      }
    }
  }
  else if (cmd == F("SET STPU"))
  {
    cmdProcessed = true;
    palaCategory = F("SETP");

    float SETPResult;
    cmdSuccess = _Pala.setSetPointUp(&SETPResult);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      dtostrf(SETPResult, 1, 2, floatBuf);
      data["SETP"] = serialized(floatBuf);
    }
  }
  else if (cmd.startsWith(F("SET TIME ")))
  {
    cmdProcessed = true;
    palaCategory = F("TIME");

    if (cmdParamNumber != 6)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    // Check if date is valid
    // basic control
    if (cmdParams[0] < 2000 || cmdParams[0] > 2099)
      info["MSG"] = F("Incorrect Year");
    else if (cmdParams[1] < 1 || cmdParams[1] > 12)
      info["MSG"] = F("Incorrect Month");
    else if ((cmdParams[2] < 1 || cmdParams[2] > 31) ||
             ((cmdParams[1] == 4 || cmdParams[1] == 6 || cmdParams[1] == 9 || cmdParams[1] == 11) && cmdParams[2] > 30) ||                        // 30 days month control
             (cmdParams[1] == 2 && cmdParams[2] > 29) ||                                                                                          // February leap year control
             (cmdParams[1] == 2 && cmdParams[2] == 29 && !(((cmdParams[0] % 4 == 0) && (cmdParams[0] % 100 != 0)) || (cmdParams[0] % 400 == 0)))) // February not leap year control
      info["MSG"] = F("Incorrect Day");
    else if (cmdParams[3] > 23)
      info["MSG"] = F("Incorrect Hour");
    else if (cmdParams[4] > 59)
      info["MSG"] = F("Incorrect Minute");
    else if (cmdParams[5] > 59)
      info["MSG"] = F("Incorrect Second");

    if (info["MSG"].isNull())
    {
      Palazzetti::DateTimeData dateTimeData;
      cmdSuccess = _Pala.setDateTime(cmdParams[0], cmdParams[1], cmdParams[2], cmdParams[3], cmdParams[4], cmdParams[5], &dateTimeData);

      if (cmdSuccess == Palazzetti::CommandResult::OK)
      {
        data["STOVE_DATETIME"] = dateTimeData.STOVE_DATETIME;
        data["STOVE_WDAY"] = dateTimeData.STOVE_WDAY;
      }
    }
  }

  return cmdSuccess;
}

Palazzetti::CommandResult WPalaControl::executePalaCmdExt(const String &cmd, JsonObject &data, JsonObject &info, const __FlashStringHelper *&palaCategory, bool &cmdProcessed, byte cmdParamNumber, const uint16_t *cmdParams)
{
  Palazzetti::CommandResult cmdSuccess = Palazzetti::CommandResult::COMMUNICATION_ERROR;

  if (cmd.startsWith(F("EXT ADRD")))
  {
    cmdProcessed = true;
    palaCategory = F("ADRD");

    if (cmdParamNumber != 2 && cmdParamNumber != 3)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    // the third parameter was designed for Micronova MB and is not used in Fumis board

    uint16_t ADDR_DATA;
    cmdSuccess = _Pala.readData(cmdParams[0], cmdParams[1], &ADDR_DATA);

    if (cmdSuccess == Palazzetti::CommandResult::OK)
    {
      String addrName(F("ADDR_"));
      // append the first parameter as hex string
      addrName += String(cmdParams[0], HEX);
      data[addrName] = ADDR_DATA;
    }
  }
#if DEVELOPPER_MODE
  // To be used only if you have good knowledge of Alpha motherboard
  else if (cmd.startsWith(F("EXT ADWR")))
  {
    cmdProcessed = true;
    palaCategory = F("ADWR");

    if (cmdParamNumber != 3 && cmdParamNumber != 4)
      info["MSG"] = String(F("Incorrect Parameter Number : ")) + cmdParamNumber;

    // the fourth parameter was designed for Micronova MB and is not used in Fumis board

    cmdSuccess = _Pala.writeData(cmdParams[0], cmdParams[1], cmdParams[2]);
  }
#endif

  return cmdSuccess;
}

void WPalaControl::publishTick()
{
  LOG_SERIAL_PRINTLN(F("PublishTick"));

  // if MQTT protocol is enabled and connected then publish Core, Wifi and WPalaControl status
  if (_ha.protocol == HaProtocol::Mqtt && _mqttMan.connected())
  {
    JsonDocument json;
    _applicationList[CoreApp]->fillStatusJSON(json[getAppIdName(CoreApp)].to<JsonObject>());
    _applicationList[WifiManApp]->fillStatusJSON(json[getAppIdName(WifiManApp)].to<JsonObject>());
    fillStatusJSON(json[getAppIdName(CustomApp)].to<JsonObject>());

    _mqttMan.publish(_preparedMqttBaseTopic, json, true);
  }

  // array of commands to execute
  const __FlashStringHelper *cmdList[] = {
      F("GET STAT"),
      F("GET TMPS"),
      F("GET FAND"),
      F("GET CNTR"),
      F("GET TIME"),
      F("GET SETP"),
      F("GET POWR"),
      F("GET DPRS")};

  // initialize _haSendResult for publish session
  _haSendResult = true;

  // execute commands
  for (const __FlashStringHelper *cmd : cmdList)
  {
    JsonDocument json;
    // execute command with publish flag to true
    if (!executePalaCmd(cmd, json, true))
      break;
  }
}

void WPalaControl::udpRequestHandler(WiFiUDP &udpServer)
{

  int packetSize = udpServer.parsePacket();
  if (packetSize <= 0)
    return;

  String strData;
  JsonDocument json;

  strData.reserve(packetSize + 1);

  // while udpServer.read() do not return -1, get returned value and add it to strData
  int bufferByte;
  while ((bufferByte = udpServer.read()) >= 0)
    strData += (char)bufferByte;

  // process request
  if (strData.endsWith(F("bridge?")))
    executePalaCmd(F("GET STDT"), json);
  else if (strData.endsWith(F("bridge?GET ALLS")))
    executePalaCmd(F("GET ALLS"), json);
  else
    executePalaCmd("", json);

  // answer to the requester
  udpServer.beginPacket(udpServer.remoteIP(), udpServer.remotePort());
  serializeJson(json, udpServer);
  udpServer.endPacket();
}

//------------------------------------------
// Used to initialize configuration properties to default values
void WPalaControl::setConfigDefaultValues()
{
  _hwDetection = HwDetection::AutoDetect;

  _ha.protocol = HaProtocol::Disabled;
  _ha.hostname[0] = 0;
  _ha.uploadPeriod = 60;

  _ha.mqtt.type = HaMqttType::GenericJson;
  _ha.mqtt.port = 1883;
  _ha.mqtt.username[0] = 0;
  _ha.mqtt.password[0] = 0;
  strcpy_P(_ha.mqtt.generic.baseTopic, PSTR("$model$"));
  _ha.mqtt.hassDiscoveryEnabled = true;
  strcpy_P(_ha.mqtt.hassDiscoveryPrefix, PSTR("homeassistant"));
}

//------------------------------------------
// Parse JSON object into configuration properties
bool WPalaControl::parseConfigJSON(JsonVariant json, bool fromWebPage /* = false */)
{
  JsonVariant jv;

  // parse hardware detection mode
  if ((jv = json[F("hwdetection")]).is<JsonVariant>())
    _hwDetection = static_cast<HwDetection>(jv.as<byte>());

  // Parse HA protocol
  if ((jv = json[F("haproto")]).is<JsonVariant>())
    _ha.protocol = static_cast<HaProtocol>(jv.as<byte>());

  // if an home Automation protocol has been selected then get common param
  if (_ha.protocol != HaProtocol::Disabled)
  {
    if ((jv = json[F("hahost")]).is<const char *>())
      strlcpy(_ha.hostname, jv, sizeof(_ha.hostname));
    if ((jv = json[F("haupperiod")]).is<JsonVariant>())
      _ha.uploadPeriod = jv;
  }

  // Now get specific param
  switch (_ha.protocol)
  {
  case HaProtocol::Disabled:
    break;

  case HaProtocol::Mqtt:

    if ((jv = json[F("hamtype")]).is<JsonVariant>())
      _ha.mqtt.type = static_cast<HaMqttType>(jv.as<byte>());
    if ((jv = json[F("hamport")]).is<JsonVariant>())
      _ha.mqtt.port = jv;
    if ((jv = json[F("hamu")]).is<const char *>())
      strlcpy(_ha.mqtt.username, jv, sizeof(_ha.mqtt.username));

    if ((jv = json[F("hamp")]).is<const char *>())
    {
      // if not from web page or password is not the predefined one
      if (!fromWebPage || strcmp_P(jv, appDataPredefPassword))
        strlcpy(_ha.mqtt.password, jv, sizeof(_ha.mqtt.password));
    }

    switch (_ha.mqtt.type)
    {
    case HaMqttType::Generic:
    case HaMqttType::GenericJson:
    case HaMqttType::GenericCategorized:
      if ((jv = json[F("hamgbt")]).is<const char *>())
        strlcpy(_ha.mqtt.generic.baseTopic, jv, sizeof(_ha.mqtt.generic.baseTopic));

      if (!_ha.hostname[0] || !_ha.mqtt.generic.baseTopic[0])
        _ha.protocol = HaProtocol::Disabled;
      break;
    }

    _ha.mqtt.hassDiscoveryEnabled = json[F("hamhassde")];

    if ((jv = json[F("hamhassdp")]).is<const char *>())
      strlcpy(_ha.mqtt.hassDiscoveryPrefix, jv, sizeof(_ha.mqtt.hassDiscoveryPrefix));

    break;
  }

  return true;
}

//------------------------------------------
// Generate JSON from configuration properties
void WPalaControl::fillConfigJSON(JsonVariant json, bool forSaveFile /* = false */)
{
  json[F("hwdetection")] = _hwDetection;

  json[F("haproto")] = _ha.protocol;
  json[F("hahost")] = _ha.hostname;
  json[F("haupperiod")] = _ha.uploadPeriod;

  // if for WebPage or protocol selected is MQTT
  if (!forSaveFile || _ha.protocol == HaProtocol::Mqtt)
  {
    json[F("hamtype")] = _ha.mqtt.type;
    json[F("hamport")] = _ha.mqtt.port;
    json[F("hamu")] = _ha.mqtt.username;
    if (forSaveFile)
      json[F("hamp")] = _ha.mqtt.password;
    else
      json[F("hamp")] = (const __FlashStringHelper *)appDataPredefPassword; // predefined special password (mean to keep already saved one)

    json[F("hamgbt")] = _ha.mqtt.generic.baseTopic;

    json[F("hamhassde")] = _ha.mqtt.hassDiscoveryEnabled;
    json[F("hamhassdp")] = _ha.mqtt.hassDiscoveryPrefix;
  }
}

//------------------------------------------
// Generate JSON of application status
void WPalaControl::fillStatusJSON(JsonVariant json)
{
  json[F("hwversion")] = _detectedHwVersion == HwVersion::V1 ? F("V1.x") : (_detectedHwVersion == HwVersion::V2 ? F("V2.x") : F("Unknown"));
  json[F("hwdetection")] = _hwDetection == HwDetection::AutoDetect ? F(" (Auto-Detected)") : F(" (Forced)");

  // Home Automation protocol
  if (_ha.protocol == HaProtocol::Mqtt)
    json[F("haprotocol")] = F("MQTT");
  else
    json[F("haprotocol")] = F("Disabled");

  // Home automation connection status
  if (_ha.protocol == HaProtocol::Mqtt)
  {
    json[F("hamqttstatus")] = _mqttMan.getStateString();

    if (_mqttMan.state() == MQTT_CONNECTED)
      json[F("hamqttlastpublish")] = (_haSendResult ? F("OK") : F("Failed"));
  }
}

//------------------------------------------
// code to execute during initialization and reinitialization of the app
bool WPalaControl::appInit(bool reInit /* = false */)
{
  // Stop UdpServer
  _udpServer.stop();

  // Stop Publish
  _publishTicker.detach();

  // Stop MQTT
  _mqttMan.disconnect();

  // if MQTT used so configure it
  if (_ha.protocol == HaProtocol::Mqtt)
  {
    // prepare base topic
    MQTTMan::prepareTopic(_ha.mqtt.generic.baseTopic, _preparedMqttBaseTopic, sizeof(_preparedMqttBaseTopic));

    // prepare will topic
    String willTopic = _preparedMqttBaseTopic;
    willTopic += F("/connected");

    // setup MQTT
    _mqttMan.setBufferSize(2048); // max JSON size (STDT ~1100 but Thermostat HAss discovery ~1800)
    _mqttMan.setClient(_wifiClient).setServer(_ha.hostname, _ha.mqtt.port);
    _mqttMan.setConnectedAndWillTopic(willTopic.c_str());
    _mqttMan.setConnectedCallback(std::bind(&WPalaControl::mqttConnectedCallback, this, std::placeholders::_1, std::placeholders::_2));
    _mqttMan.setDisconnectedCallback(std::bind(&WPalaControl::mqttDisconnectedCallback, this));
    _mqttMan.setCallback(std::bind(&WPalaControl::mqttCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Connect
    _mqttMan.connect(_ha.mqtt.username, _ha.mqtt.password);
  }

  LOG_SERIAL_PRINT(F("Connecting to Stove..."));

// Initialize hw version detection
#ifdef ESP8266
  uint8_t hwDetectPin = 5;
#else
  uint8_t hwDetectPin = 22;
#endif

  if (_hwDetection == HwDetection::AutoDetect)
  {
    pinMode(hwDetectPin, INPUT_PULLUP);
    delay(2);

    if (digitalRead(hwDetectPin) == HIGH)
      _detectedHwVersion = HwVersion::V1;
    else
      _detectedHwVersion = HwVersion::V2;
  }
  else if (_hwDetection == HwDetection::ForcedV1)
    _detectedHwVersion = HwVersion::V1;
  else
    _detectedHwVersion = HwVersion::V2;

  if (_detectedHwVersion == HwVersion::V1)
    LOG_SERIAL_PRINT(F("HW1..."));
  else
    LOG_SERIAL_PRINT(F("HW2..."));

  Palazzetti::CommandResult cmdRes;
  Palazzetti::SerialAdapter palaSerialAdapter = {
      .open = std::bind(&WPalaControl::myOpenSerial, this, std::placeholders::_1),
      .close = std::bind(&WPalaControl::myCloseSerial, this),
      .select = std::bind(&WPalaControl::mySelectSerial, this, std::placeholders::_1),
      .read = std::bind(&WPalaControl::myReadSerial, this, std::placeholders::_1, std::placeholders::_2),
      .write = std::bind(&WPalaControl::myWriteSerial, this, std::placeholders::_1, std::placeholders::_2),
      .drain = std::bind(&WPalaControl::myDrainSerial, this),
      .flush = std::bind(&WPalaControl::myFlushSerial, this),
      .uSleep = std::bind(&WPalaControl::myUSleep, this, std::placeholders::_1)};

  cmdRes = _Pala.initialize(palaSerialAdapter, _detectedHwVersion == HwVersion::V1);

  if (cmdRes == Palazzetti::CommandResult::OK)
  {
    LOG_SERIAL_PRINTLN(F("Stove connected"));
    char SN[28];
    _Pala.getSN(&SN);
    LOG_SERIAL_PRINTF_P(PSTR("Stove Serial Number: %s\n"), SN);
  }
  else
    LOG_SERIAL_PRINTLN(F("Stove connection failed"));

  if (cmdRes == Palazzetti::CommandResult::OK)
    publishTick(); // if configuration changed, publish immediately

#ifdef ESP8266
  _publishTicker.attach(_ha.uploadPeriod, [this]()
                        { this->_needPublish = true; });
#else
  _publishTicker.attach<WPalaControl *>(_ha.uploadPeriod, [](WPalaControl *palaControl)
                                        { palaControl->_needPublish = true; }, this);
#endif

  // flag to force publish update (init and reinit)
  _needPublishUpdate = true;

  // start publish update Ticker
#ifdef ESP8266
  _publishUpdateTicker.attach(86400, [this]()
                              { this->_needPublishUpdate = true; });
#else
  _publishUpdateTicker.attach<WPalaControl *>(86400, [](WPalaControl *palaSensor)
                                              { palaSensor->_needPublishUpdate = true; }, this);
#endif

  // Start UDP Server
  _udpServer.begin(54549);

  return cmdRes == Palazzetti::CommandResult::OK;
}

//------------------------------------------
// Return HTML Code to insert into Status Web page
const PROGMEM char *WPalaControl::getHTMLContent(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return status2htmlgz;
  case config:
    return config2htmlgz;
  default:
    return nullptr;
  }
}

// and his Size
size_t WPalaControl::getHTMLContentSize(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return sizeof(status2htmlgz);
  case config:
    return sizeof(config2htmlgz);
  default:
    return 0;
  }
}

//------------------------------------------
// code to register web request answer to the web server
void WPalaControl::appInitWebServer(WebServer &server)
{
  // Handle HTTP GET requests
  server.on(F("/cgi-bin/sendmsg.lua"), HTTP_GET, [this, &server]()
            {
    String cmd;

    if (server.hasArg(F("cmd")))
      cmd = server.arg(F("cmd"));

    // WPalaControl specific command
    if (cmd.startsWith(F("BKP PARM ")))
    {
      String strFileType(cmd.substring(9));

      if (strFileType != F("CSV") && strFileType != F("JSON"))
      {
        String ret(F("{\"INFO\":{\"CMD\":\"BKP PARM\",\"MSG\":\"Incorrect File Type : "));
        ret += strFileType;
        ret += F("\"},\"SUCCESS\":false,\"DATA\":{\"NODATA\":true}}");
        SERVER_KEEPALIVE_FALSE()
        server.send(200, F("text/json"), ret);
        return;
      }

      byte params[0x6A];
      Palazzetti::CommandResult cmdRes = _Pala.getAllParameters(&params);

      if (cmdRes != Palazzetti::CommandResult::OK)
      {
        SERVER_KEEPALIVE_FALSE()
        server.send(200, F("text/json"), F("{\"INFO\":{\"CMD\":\"BKP PARM\",\"MSG\":\"Stove communication failed\",\"RSP\":\"TIMEOUT\"},\"SUCCESS\":false,\"DATA\":{\"NODATA\":true}}"));
        return;
      }

      if (strFileType == F("CSV"))
      {
        String toReturn;
        char line[10];
        toReturn.reserve(965); // Header + 106 lines with worst-case index/value width (3+1+3+2)
        toReturn += F("PARM;VALUE\r\n");

        for (byte i = 0; i < 0x6A; i++)
        {
          snprintf(line, sizeof(line), "%d;%d\r\n", i, params[i]);
          toReturn += line;
        }

        SERVER_KEEPALIVE_FALSE()
        server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"PARM.csv\""));
        server.send(200, F("text/csv"), toReturn);
      }
      else
      {
        JsonDocument json;
        JsonArray PARM = json[F("PARM")].to<JsonArray>();
        for (byte i = 0; i < 0x6A; i++)
          PARM.add(params[i]);

        SERVER_KEEPALIVE_FALSE()
        server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"PARM.json\""));
        server.setContentLength(measureJson(json));
        server.send(200, F("text/json"), "");
        { auto client = server.client(); serializeJson(json, client); }
      }

      return;
    }

    // WPalaControl specific command
    if (cmd.startsWith(F("BKP HPAR ")))
    {
      String strFileType(cmd.substring(9));

      if (strFileType != F("CSV") && strFileType != F("JSON"))
      {
        String ret(F("{\"INFO\":{\"CMD\":\"BKP HPAR\",\"MSG\":\"Incorrect File Type : "));
        ret += strFileType;
        ret += F("\"},\"SUCCESS\":false,\"DATA\":{\"NODATA\":true}}");
        SERVER_KEEPALIVE_FALSE()
        server.send(200, F("text/json"), ret);
        return;
      }

      uint16_t hiddenParams[0x6F];
      Palazzetti::CommandResult cmdRes = _Pala.getAllHiddenParameters(&hiddenParams);

      if (cmdRes != Palazzetti::CommandResult::OK)
      {
        SERVER_KEEPALIVE_FALSE()
        server.send(200, F("text/json"), F("{\"INFO\":{\"CMD\":\"BKP HPAR\",\"MSG\":\"Stove communication failed\",\"RSP\":\"TIMEOUT\"},\"SUCCESS\":false,\"DATA\":{\"NODATA\":true}}"));
        return;
      }

      if (strFileType == F("CSV"))
      {
        String toReturn;
        char line[12];
        toReturn.reserve(1232); // Header + 111 lines with worst-case index/value width (3+1+5+2)
        toReturn += F("HPAR;VALUE\r\n");

        for (byte i = 0; i < 0x6F; i++)
        {
          snprintf(line, sizeof(line), "%d;%d\r\n", i, hiddenParams[i]);
          toReturn += line;
        }

        SERVER_KEEPALIVE_FALSE()
        server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"HPAR.csv\""));
        server.send(200, F("text/csv"), toReturn);
      }
      else if (strFileType == F("JSON"))
      {
        JsonDocument json;
        JsonArray HPAR = json[F("HPAR")].to<JsonArray>();
        for (byte i = 0; i < 0x6F; i++)
          HPAR.add(hiddenParams[i]);

        SERVER_KEEPALIVE_FALSE()
        server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"HPAR.json\""));
        server.setContentLength(measureJson(json));
        server.send(200, F("text/json"), "");
        { auto client = server.client(); serializeJson(json, client); }
      }

      return;
    }

    JsonDocument json;

    // Other commands processed using normal Palazzetti logic
    executePalaCmd(cmd, json);

    // send response
    SERVER_KEEPALIVE_FALSE()
    server.setContentLength(measureJson(json));
    server.send(200, F("text/json"), "");
    { auto client = server.client(); serializeJson(json, client); } });

  // Handle HTTP POST requests (Body contains a JSON)
  server.on(
      F("/cgi-bin/sendmsg.lua"), HTTP_POST, [this, &server]()
      {
        String cmd;
        JsonDocument json;

        DeserializationError error = deserializeJson(json, server.arg(F("plain")));

        if (!error && !json[F("command")].isNull())
          cmd = json[F("command")].as<String>();

        // process cmd
        executePalaCmd(cmd, json);

        // send response
        SERVER_KEEPALIVE_FALSE()
        server.setContentLength(measureJson(json));
        server.send(200, F("text/json"), "");
        { auto client = server.client(); serializeJson(json, client); } });

  // register EventSource
  _eventSourceMan.initEventSourceServer(getAppIdChar(_appId), server);
}

//------------------------------------------
// Run for timer
void WPalaControl::appRun()
{
  if (_ha.protocol == HaProtocol::Mqtt)
  {
    _mqttMan.loop();

    // if Home Assistant discovery enabled and publish is needed (and publish is successful)
    if (_ha.mqtt.hassDiscoveryEnabled && _needPublishHassDiscovery && mqttPublishHassDiscovery())
    {
      _needPublishHassDiscovery = false;
      _needPublish = true; // force publishTick after discovery
    }

    if (_needPublishUpdate && mqttPublishUpdate())
      _needPublishUpdate = false;
  }

  if (_needPublish)
  {
    _needPublish = false;
    publishTick();
  }

  // Handle UDP requests
  udpRequestHandler(_udpServer);
}

//------------------------------------------
// Constructor
WPalaControl::WPalaControl() : Application(CustomApp)
{
  // TX/GPIO15 is pulled down and so block the stove bus by default...
  pinMode(15, OUTPUT); // set TX PIN to OUTPUT HIGH to unlock bus during WiFi connection
  digitalWrite(15, HIGH);
}
