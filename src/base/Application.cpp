#include "Application.h"

Application *Application::_applicationList[3] = {nullptr, nullptr, nullptr};

Application::Application(AppId appId) : _appId(appId)
{
  _applicationList[_appId] = this;
}

char Application::getAppIdChar(AppId appId)
{
  return '0' + appId;
}

const __FlashStringHelper *Application::getAppIdName(AppId appId)
{
  if (appId == CoreApp)
    return F("Core");

  if (appId == WifiManApp)
    return F("WiFi");

  return F(CUSTOM_APP_MODEL);
}

bool Application::saveConfig()
{
  char configPath[32];
  snprintf_P(configPath, sizeof(configPath), PSTR("/%s.json"), (const char *)getAppIdName(_appId));
  File configFile = LittleFS.open(configPath, "w");
  if (!configFile)
  {
    LOG_SERIAL_PRINTLN(F("Failed to open config file for writing"));
    return false;
  }

  JsonDocument doc;
  fillConfigJSON(doc, true);
  serializeJson(doc, configFile);
  configFile.close();
  return true;
}

bool Application::loadConfig()
{
  // special exception for Core, there is no Core.json file to Load
  if (_appId == CoreApp)
    return true;

  bool result = false;
  char configPath[32];
  snprintf_P(configPath, sizeof(configPath), PSTR("/%s.json"), (const char *)getAppIdName(_appId));
  File configFile = LittleFS.open(configPath, "r");
  if (configFile)
  {

    JsonDocument jsonDoc;

    DeserializationError deserializeJsonError = deserializeJson(jsonDoc, configFile);

    // if deserialization failed, then log error and save current config (default values)
    if (deserializeJsonError)
    {

      LOG_SERIAL_PRINTF_P(PSTR("deserializeJson() failed : %s\n"), deserializeJsonError.c_str());

      saveConfig();
    }
    else
    { // otherwise pass it to application
      result = parseConfigJSON(jsonDoc);
    }
    configFile.close();
  }

  return result;
}

bool Application::getLastestUpdateInfo(char *version, char *title, char *releaseDate, char *summary)
{
  version[0] = title[0] = releaseDate[0] = summary[0] = '\0';

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

  // sometime the stream is not yet ready (no data available yet)
  for (byte i = 0; i < 200 && stream->available() == 0; i++) // available include an optimistic_yield of 100us
    ;

  // while there is data to read
  while (http.connected() && stream->available())
  {
    // read the next character
    char c = stream->read();

    // if c is a brace or bracket, increment or decrement the treeLevel
    if (c == '{' || c == '[')
      treeLevel++;
    else if (c == '}' || c == ']')
      treeLevel--;

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
    if (keyLen >= 11 && memcmp(keyBuffer + keyLen - 11, "\"tag_name\":", 11) == 0)
    {
      targetPtr = version;
      targetMaxLen = 9;
    }
    // if we found the key "name"
    else if (keyLen >= 7 && memcmp(keyBuffer + keyLen - 7, "\"name\":", 7) == 0)
    {
      targetPtr = title;
      targetMaxLen = 63;
    }
    // if we found the key "published_at"
    else if (keyLen >= 15 && memcmp(keyBuffer + keyLen - 15, "\"published_at\":", 15) == 0)
    {
      targetPtr = releaseDate;
      targetMaxLen = 10;
    }
    // if we found the key "body"
    else if (keyLen >= 7 && memcmp(keyBuffer + keyLen - 7, "\"body\":", 7) == 0)
    {
      targetPtr = summary;
      targetMaxLen = 255;
    }

    // if this is not a key we are looking for, continue
    if (!targetPtr)
      continue;

    //otherwise prepare target buffer
    targetPtr[0] = '\0';
    size_t curLen = 0;

    // skip until opening doublequote
    while (stream->available() && stream->read() != '"')
      ;

    // for title, skip version prefix up to first space
    if (targetPtr == title)
      while (stream->available() && stream->read() != ' ')
        ;

    // read the value
    while (stream->available())
    {
      c = stream->read();

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

  return version[0] != '\0';
}

void Application::fillLatestUpdateInfoJson(JsonDocument &doc, bool forWebPage /* = false */)
{
  doc[F("installed_version")] = VERSION;

  char version[10], title[64], releaseDate[11], summary[256];

  if (getLastestUpdateInfo(version, title, releaseDate, summary))
  {
    doc[F("latest_version")] = version;
    doc[F("title")] = title;
    doc[F("release_summary")] = summary;

    char releaseUrl[128];
    snprintf_P(releaseUrl, sizeof(releaseUrl), PSTR("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/tag/%s"), version);
    doc[F("release_url")] = releaseUrl;

    if (forWebPage)
      doc[F("release_date")] = releaseDate;
  }
}

bool Application::updateFirmware(const char *version, String &retMsg, std::function<void(size_t, size_t)> progressCallback /* = nullptr */)
{
  if (!version || !version[0])
  {
    retMsg = F("No version provided");
    return false;
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
  for (byte i = 0; i < 200 && stream->available() == 0; i++) // available include an optimistic_yield of 100us
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

String Application::getStatusJSON()
{
  JsonDocument doc;
  fillStatusJSON(doc);
  String s;
  serializeJson(doc, s);
  return s;
}

void Application::init(bool skipExistingConfig)
{
  bool result = true;

  LOG_SERIAL_PRINTF_P(PSTR("Start %s : "), (const char *)getAppIdName(_appId));

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

  // HTML Status handler
  sprintf_P(url, PSTR("/status%c.html"), getAppIdChar(_appId));
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.send_P(200, PSTR("text/html"), getHTMLContent(status), getHTMLContentSize(status));
            });

  // HTML Config handler
  sprintf_P(url, PSTR("/config%c.html"), getAppIdChar(_appId));
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.send_P(200, PSTR("text/html"), getHTMLContent(config), getHTMLContentSize(config));
            });

  // JSON Status handler
  sprintf_P(url, PSTR("/gs%c"), getAppIdChar(_appId));
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));
              JsonDocument doc;
              fillStatusJSON(doc);
              server.setContentLength(measureJson(doc));
              server.send(200, F("text/json"), "");
              WiFiClient client = server.client();
              serializeJson(doc, client);
            });

  // JSON Config handler
  sprintf_P(url, PSTR("/gc%c"), getAppIdChar(_appId));
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));
              JsonDocument doc;
              fillConfigJSON(doc);
              server.setContentLength(measureJson(doc));
              server.send(200, F("text/json"), "");
              WiFiClient client = server.client();
              serializeJson(doc, client);
            });

  sprintf_P(url, PSTR("/sc%c"), getAppIdChar(_appId));
  server.on(url, HTTP_POST,
            [this, &server]()
            {
              // All responses have keep-alive set to false
              SERVER_KEEPALIVE_FALSE()

              // config json are received in POST body (arg("plain"))

              // Deserialize it
              JsonDocument doc;
              DeserializationError error = deserializeJson(doc, server.arg(F("plain")));
              if (error)
              {
                server.send(400, F("text/html"), F("Malformed JSON"));
                return;
              }

              // Parse it using the application method
              if (!parseConfigJSON(doc, true))
              {
                server.send(400, F("text/html"), F("Invalid Configuration"));
                return;
              }

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
    LOG_SERIAL_PRINTF_P(PSTR("ReStart %s : "), (const char *)getAppIdName(_appId));

    if (appInit(true))
      LOG_SERIAL_PRINTLN(F("OK"));
    else
      LOG_SERIAL_PRINTLN(F("FAILED"));

    _reInit = false;
  }

  appRun();
}