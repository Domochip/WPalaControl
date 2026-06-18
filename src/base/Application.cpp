#include "Application.h"

Application *Application::_applicationList[3] = {nullptr, nullptr, nullptr};

void Application::HassDiscoveryCtx::publishEntity(JsonDocument &json, const String &type, const __FlashStringHelper *uniqueIdSuffix, bool withStandardAvail)
{
  static const __FlashStringHelper *standardAvailabilityJSON = F("{\"topic\":\"~/connected\",\"value_template\":\"{{ iif(int(value) > 0, 'online', 'offline') }}\"}");

  String uniqueId = uniqueIdPrefix + '_' + uniqueIdSuffix;

  // prepare topic: <hassDiscoveryPrefix>/<type>/<uniqueId>/config
  String topic;
  topic.reserve(strlen(hassDiscoveryPrefix) + type.length() + uniqueId.length() + 9); // 9 = "/" + "/" + "/config"
  topic += hassDiscoveryPrefix;
  topic += '/';
  topic += type;
  topic += '/';
  topic += uniqueId;
  topic += F("/config");

  // complete payload with common fields
  json["~"] = mqttMan.getBaseTopic();
  if (withStandardAvail)
    json[F("availability")] = serialized(availabilityJSON ? availabilityJSON : standardAvailabilityJSON);
  json[F("device")] = serialized(device);
  json[F("unique_id")] = uniqueId;

  // publish to MQTT
  mqttMan.publish(topic.c_str(), json, true);
}

Application::Application(AppId appId) : _appId(appId)
{
  _applicationList[_appId] = this;
}

char Application::getAppIdChar()
{
  return '0' + _appId;
}

const __FlashStringHelper *Application::getAppIdName()
{
  if (_appId == CoreApp)
    return F("Core");

  if (_appId == WifiManApp)
    return F("WiFi");

  return F(CUSTOM_APP_MODEL);
}

const __FlashStringHelper *Application::getMqttTopicSuffix()
{
  if (_appId == CoreApp)
    return F("/Core");

  if (_appId == WifiManApp)
    return F("/WiFi");

  return F("/App");
}

bool Application::saveConfig()
{
  char configPath[32];
  snprintf_P(configPath, sizeof(configPath), PSTR("/%s.json"), (const char *)getAppIdName());
  File configFile = LittleFS.open(configPath, "w");
  if (!configFile)
  {
    LOG_SERIAL_PRINTLN(F("Failed to open config file for writing"));
    return false;
  }

  JsonDocument json;
  fillConfigJSON(json, true);
  serializeJson(json, configFile);
  configFile.close();
  return true;
}

bool Application::loadConfig()
{
  // special exception for Core, there is no Core.json file to Load
  if (_appId == CoreApp)
    return true;

  bool result = true; // missing file is not an error: default values are already set
  char configPath[32];
  snprintf_P(configPath, sizeof(configPath), PSTR("/%s.json"), (const char *)getAppIdName());
  File configFile = LittleFS.open(configPath, "r");
  if (configFile)
  {
    JsonDocument json;

    DeserializationError deserializeJsonError = deserializeJson(json, configFile);

    // if deserialization failed, then log error and save current config (default values)
    if (deserializeJsonError)
    {
      LOG_SERIAL_PRINTF_P(PSTR("deserializeJson() failed : %s\n"), deserializeJsonError.c_str());
      saveConfig();
      result = false;
    }
    else
    { // otherwise pass it to application
      result = parseConfigJSON(json);
      validateConfig();
    }
    configFile.close();
  }

  return result;
}

// Writes a secret field: real value when saving to file, predefined placeholder when sending to web page
void Application::fillSecret(JsonVariant json, const __FlashStringHelper *key, const char *value, bool forSaveFile)
{
  if (forSaveFile)
    json[key] = value;
  else
    json[key] = (const __FlashStringHelper *)predefPassword;
}

// Parses a secret field: skips update if the value is the predefined placeholder sent by web page
void Application::parseSecret(JsonVariant jv, char *dest, size_t size, bool fromWebPage)
{
  if (jv.is<const char *>() && (!fromWebPage || strcmp_P(jv, predefPassword)))
    strlcpy(dest, jv, size);
}

void Application::parseIPField(JsonVariant jv, uint32_t &dest)
{
  if (jv.is<const char *>())
  {
    IPAddress ip;
    if (ip.fromString(jv.as<const char *>()))
      dest = ip;
    else
      dest = 0;
  }
}

bool Application::getLatestUpdateInfo(char *version, char *title /* = nullptr */, char *releaseDate /* = nullptr */, char *summary /* = nullptr */)
{
  if (version)
    version[0] = '\0';
  if (title)
    title[0] = '\0';
  if (releaseDate)
    releaseDate[0] = '\0';
  if (summary)
    summary[0] = '\0';

  WiFiClientSecure clientSecure;
  HTTPClient http;

  clientSecure.setInsecure();
  http.begin(clientSecure, String(F("https://api.github.com/repos/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/latest")));
  int httpCode = http.GET();

  // check for http error
  if (httpCode != 200)
  {
    http.end();
    return false;
  }

  // httpCode is 200, we can continue
  WiFiClient *stream = http.getStreamPtr();

  // We need to parse the JSON response without loading the whole response in memory

  const uint8_t maxKeyLength = 16; // longest key is "\"published_at\":"" => 15 chars
  char keyBuffer[17] = {0};        // Shifting buffer used to find keys
  uint8_t keyLen = 0;              // current length of the key in the buffer (up to maxKeyLength)
  uint8_t treeLevel = 0;           // used to skip unwanted data
  bool inOuterString = false;      // true while the outer loop is inside a JSON string value
  bool prevWasBackslash = false;   // used to detect escaped quotes inside strings

  // readNextChar waits briefly for the next byte, allowing the WiFi stack to
  // process incoming TCP segments that haven't arrived yet (fixes premature exit
  // when the receive buffer empties between two TCP segments)
  auto readNextChar = [&](char &c) -> bool
  {
    for (uint8_t i = 0; i < 200 && !stream->available(); i++) // available includes an optimistic_yield of 100us
      ;
    if (!stream->available())
      return false;
    c = stream->read();
    return true;
  };

  // while there is data to read
  char c;
  while (readNextChar(c))
  {

    // toggle string mode on unescaped quotes; only count braces/brackets outside strings
    if (c == '"' && !prevWasBackslash)
      inOuterString = !inOuterString;
    prevWasBackslash = !prevWasBackslash && (c == '\\');

    if (!inOuterString)
    {
      if (c == '{' || c == '[')
        treeLevel++;
      else if (c == '}' || c == ']')
        treeLevel--;
    }

    // if we are not at the first treeLevel, skip the character
    // (there is some "name" key in assets that we don't want to parse)
    if (treeLevel > 1)
      continue;

    // if keyBuffer is full, shift it to the left by one character
    if (keyLen == maxKeyLength)
    {
      memmove(keyBuffer, keyBuffer + 1, maxKeyLength - 1);
      keyLen = maxKeyLength - 1;
    }
    // and add the new character at the end
    keyBuffer[keyLen++] = c;
    keyBuffer[keyLen] = '\0';

    if (c != ':')
      continue;

    char *targetPtr = nullptr;
    size_t targetMaxLen = 0;

    // if we found the key "tag_name"
    if (version && keyLen >= 11 && memcmp(keyBuffer + keyLen - 11, "\"tag_name\":", 11) == 0)
    {
      targetPtr = version;
      targetMaxLen = 9;
    }
    // if we found the key "name"
    else if (title && keyLen >= 7 && memcmp(keyBuffer + keyLen - 7, "\"name\":", 7) == 0)
    {
      targetPtr = title;
      targetMaxLen = 63;
    }
    // if we found the key "published_at"
    else if (releaseDate && keyLen >= 15 && memcmp(keyBuffer + keyLen - 15, "\"published_at\":", 15) == 0)
    {
      targetPtr = releaseDate;
      targetMaxLen = 10;
    }
    // if we found the key "body"
    else if (summary && keyLen >= 7 && memcmp(keyBuffer + keyLen - 7, "\"body\":", 7) == 0)
    {
      targetPtr = summary;
      targetMaxLen = 255;
    }

    // if this is not a key we are looking for, continue
    if (!targetPtr)
      continue;

    // otherwise prepare target buffer
    targetPtr[0] = '\0';
    size_t curLen = 0;

    // skip until opening doublequote
    while (readNextChar(c) && c != '"')
      ;

    // for title, skip version prefix up to first space
    if (targetPtr == title)
      while (readNextChar(c) && c != ' ')
        ;

    // read the value
    while (readNextChar(c))
    {

      // endsWithBackslash is used to handle escaped characters in JSON (e.g. \n, \r, \") and avoid stopping at an escaped double quote
      bool endsWithBackslash = (curLen > 0 && targetPtr[curLen - 1] == '\\');

      if (c == '"' && !endsWithBackslash)
        break;

      if (endsWithBackslash)
      {
        if (c == 'n')
          targetPtr[curLen - 1] = '\n';
        else if (c == 'r')
          targetPtr[curLen - 1] = '\r';
        else if (c == '"')
          targetPtr[curLen - 1] = '"';
        else if (c == '\\')
          targetPtr[curLen - 1] = '\\';
      }
      else if (curLen < targetMaxLen)
      {
        targetPtr[curLen++] = c;
        targetPtr[curLen] = '\0';
      }

      // for summary, stop at first section break "\r\n\r\n##"
      if (targetPtr == summary && curLen >= 6 && memcmp(targetPtr + curLen - 6, "\r\n\r\n##", 6) == 0)
      {
        // remove the last 6 characters
        curLen -= 6;
        targetPtr[curLen] = '\0';
        // avoid adding more text in summary
        targetMaxLen = curLen;
      }
    }
  }

  http.end();

  return version && version[0] != '\0';
}

void Application::fillLatestUpdateInfoJson(JsonVariant json, bool forWebPage /* = false */)
{
  json[F("installed_version")] = VERSION;

  char version[10], title[64], releaseDate[11], summary[256];

  if (getLatestUpdateInfo(version, title, releaseDate, summary))
  {
    json[F("latest_version")] = version;
    json[F("title")] = title;
    json[F("release_summary")] = summary;

    char releaseUrl[128];
    snprintf_P(releaseUrl, sizeof(releaseUrl), PSTR("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/tag/%s"), version);
    json[F("release_url")] = releaseUrl;

    if (forWebPage)
      json[F("release_date")] = releaseDate;
    else
      json[F("in_progress")] = (Update.isRunning() ? F("true") : F("false"));
  }
}

bool Application::updateFirmware(const char *version, String &retMsg, std::function<void(size_t, size_t)> progressCallback /* = nullptr */)
{
  if (!version || !version[0])
  {
    retMsg = F("No version provided");
    return false;
  }

  char latestVersion[10];
  if (strcmp(version, "latest") == 0)
  {
    if (!getLatestUpdateInfo(latestVersion, nullptr, nullptr, nullptr))
    {
      retMsg = F("Failed to get latest version");
      return false;
    }

    version = latestVersion;
  }

  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();

  char fwUrl[200];
#ifdef ESP8266
  snprintf_P(fwUrl, sizeof(fwUrl), PSTR("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/download/%s/" CUSTOM_APP_MODEL ".%s.bin"), version, version);
#else
  snprintf_P(fwUrl, sizeof(fwUrl), PSTR("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/download/%s/" CUSTOM_APP_MODEL ".esp32.%s.bin"), version, version);
#endif

  LOG_SERIAL_PRINTF_P(PSTR("Trying to Update from URL: %s\n"), fwUrl);

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.begin(clientSecure, fwUrl);
  int httpCode = https.GET();

  if (httpCode != 200)
  {
    https.end();

    char retMsgBuf[48];
    snprintf_P(retMsgBuf, sizeof(retMsgBuf), PSTR("Failed to download file, httpCode: %d"), httpCode);
    retMsg = retMsgBuf;

    LOG_SERIAL_PRINTLN(retMsg);

    return false;
  }

  // starting here we have a valid httpCode (200)

  // get the stream
  WiFiClient *stream = https.getStreamPtr();
  int contentLength = https.getSize();

  const char *fwName = strrchr(fwUrl, '/');
  fwName = fwName ? fwName + 1 : fwUrl;
  LOG_SERIAL_PRINTF_P(PSTR("Update Start: %s (Online Update)\n"), fwName);

  if (progressCallback)
    Update.onProgress(progressCallback);

  Update.begin(contentLength);

  // sometime the stream is not yet ready (no data available yet)
  // and writeStream start by a peek which then fail
  for (uint8_t i = 0; i < 200 && stream->available() == 0; i++) // available include an optimistic_yield of 100us
    ;
  Update.writeStream(*stream);

  Update.end();

  https.end();

  bool success = !Update.hasError();
  if (success)
    LOG_SERIAL_PRINTLN(F("Update successful"));
  else
  {
#ifdef ESP8266
    retMsg = Update.getErrorString();
#else
    retMsg = Update.errorString();
#endif
    LOG_SERIAL_PRINTF_P(PSTR("Update failed: %s\n"), retMsg.c_str());
    Update.clearError();
  }

  return success;
}

void Application::mqttPublishStatus(MQTTMan &mqttMan)
{
  JsonDocument json;
  String topic;
  topic.reserve(MQTTMan::baseTopicSize + 5); // base topic + suffix

  topic = mqttMan.getBaseTopic();
  topic += getMqttTopicSuffix();
  fillStatusJSON(json);
  mqttMan.publish(topic.c_str(), json, true);
}

bool Application::mqttPublishUpdate(MQTTMan &mqttMan)
{
  if (!mqttMan.connected())
    return false;

  JsonDocument updateInfo;
  fillLatestUpdateInfoJson(updateInfo);

  String topic = mqttMan.getBaseTopic();
  topic += F("/update");

  return mqttMan.publish(topic.c_str(), updateInfo, true);
}

void Application::init(bool skipExistingConfig)
{
  bool result = true;

  LOG_SERIAL_PRINTF_P(PSTR("Start %s : "), (const char *)getAppIdName());

  setConfigDefaultValues();

  if (!skipExistingConfig)
    result = loadConfig();

  // Execute specific Application Init Code
  result = appInit() && result;

  LOG_SERIAL_PRINTLN(result ? F("OK") : F("FAILED"));
}

void Application::initWebServer(WebServer &server)
{
  char url[16];

  // JSON Status handler
  sprintf_P(url, PSTR("/gs%c"), getAppIdChar());
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));
              JsonDocument json;
              fillStatusJSON(json);
              server.setContentLength(measureJson(json));
              server.send(200, F("text/json"), "");
              WiFiClient client = server.client();
              serializeJson(json, client);
            });

  // JSON Config handler
  sprintf_P(url, PSTR("/gc%c"), getAppIdChar());
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));
              JsonDocument json;
              fillConfigJSON(json);
              server.setContentLength(measureJson(json));
              server.send(200, F("text/json"), "");
              WiFiClient client = server.client();
              serializeJson(json, client);
            });

  sprintf_P(url, PSTR("/sc%c"), getAppIdChar());
  server.on(url, HTTP_POST,
            [this, &server]()
            {
              // All responses have keep-alive set to false
              SERVER_KEEPALIVE_FALSE()

              // config json are received in POST body (arg("plain"))

              // Deserialize it
              JsonDocument json;
              DeserializationError error = deserializeJson(json, server.arg(F("plain")));
              if (error)
              {
                server.send(400, F("text/html"), F("Malformed JSON"));
                return;
              }

              // Parse it using the application method
              if (!parseConfigJSON(json, true))
              {
                server.send(400, F("text/html"), F("Invalid Configuration"));
                return;
              }
              validateConfig();

              // Save it
              if (!saveConfig())
              {
                server.send(500, F("text/html"), F("Configuration hasn't been saved"));
                return;
              }

              // Everything went fine, Send client answer
              server.send(200);
              _reInit = true;
            });

  // Execute Specific Application Web Server initialization
  appInitWebServer(server);
}

void Application::run()
{
  if (_reInit)
  {
    LOG_SERIAL_PRINTF_P(PSTR("ReStart %s : "), (const char *)getAppIdName());

    if (appInit(true))
      LOG_SERIAL_PRINTLN(F("OK"));
    else
      LOG_SERIAL_PRINTLN(F("FAILED"));

    _reInit = false;
  }

  appRun();
}