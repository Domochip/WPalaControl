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

String Application::getAppIdName(AppId appId)
{
  if (appId == CoreApp)
    return F("Core");

  if (appId == WifiManApp)
    return F("WiFi");

  return F(CUSTOM_APP_MODEL);
}

bool Application::saveConfig()
{
  File configFile = LittleFS.open(String('/') + getAppIdName(_appId) + F(".json"), "w");
  if (!configFile)
  {
    LOG_SERIAL_PRINTLN(F("Failed to open config file for writing"));
    return false;
  }

  configFile.print(generateConfigJSON(true));
  configFile.close();
  return true;
}

bool Application::loadConfig()
{
  // special exception for Core, there is no Core.json file to Load
  if (_appId == CoreApp)
    return true;

  bool result = false;
  File configFile = LittleFS.open(String('/') + getAppIdName(_appId) + F(".json"), "r");
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

bool Application::getLastestUpdateInfo(char (*version)[10], char (*title)[64] /* = nullptr */, char (*releaseDate)[11] /* = nullptr */, char (*summary)[256] /* = nullptr */)
{
  // version is mandatory
  if (!version)
    return false;

  // initialize the output strings
  *version[0] = 0;
  if (title)
    *title[0] = 0;
  if (releaseDate)
    *releaseDate[0] = 0;
  if (summary)
    *summary[0] = 0;

  String githubURL = F("https://api.github.com/repos/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/latest");

  WiFiClientSecure clientSecure;
  HTTPClient http;

  clientSecure.setInsecure();
  http.begin(clientSecure, githubURL);
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

  char keyBuffer[16] = {0}; // Shifting buffer used to find keys (longest one is "\"published_at\":"" =>15 chars)
  String valueBuffer;       // used to store the value before copying it to the target array
  uint8_t treeLevel = 0;    // used to skip unwanted data
  bool keyFound = false;    // used to know if we found a key we are looking for
  char *targetArray = nullptr;
  size_t targetArraySize = 0;

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
    if (strlen(keyBuffer) == sizeof(keyBuffer) - 1)
      memmove(keyBuffer, keyBuffer + 1, sizeof(keyBuffer) - 1);

    // add the new character at the end
    keyBuffer[strlen(keyBuffer) + 1] = 0;
    keyBuffer[strlen(keyBuffer)] = c;

    keyFound = false;

    // if we found the key "tag_name"
    if (c == ':' && strlen(keyBuffer) >= 11 && !strcmp_P(keyBuffer + strlen(keyBuffer) - 11, PSTR("\"tag_name\":")))
    {
      keyFound = true;
      targetArray = (char *)version;
      targetArraySize = sizeof(*version);
    }

    // if we found the key "name"
    if (c == ':' && title && strlen(keyBuffer) >= 7 && !strcmp_P(keyBuffer + strlen(keyBuffer) - 7, PSTR("\"name\":")))
    {
      keyFound = true;
      targetArray = (char *)title;
      targetArraySize = sizeof(*title);
    }

    // if we found the key "published_at"
    if (c == ':' && releaseDate && strlen(keyBuffer) >= 15 && !strcmp_P(keyBuffer + strlen(keyBuffer) - 15, PSTR("\"published_at\":")))
    {
      keyFound = true;
      targetArray = (char *)releaseDate;
      targetArraySize = sizeof(*releaseDate);
    }

    // if we found the key "body"
    if (c == ':' && summary && strlen(keyBuffer) >= 7 && !strcmp_P(keyBuffer + strlen(keyBuffer) - 7, PSTR("\"body\":")))
    {
      keyFound = true;
      targetArray = (char *)summary;
      targetArraySize = sizeof(*summary);
    }

    if (keyFound)
    {
      valueBuffer.clear();

      // read until the next quote (should be next to semicolon)
      stream->readStringUntil('"');

      // for name/title key, skip text until the first space
      if (targetArray == (char *)title)
        stream->readStringUntil(' ');

      // read the value
      while (stream->available())
      {
        c = stream->read();
        if (c == '"' && valueBuffer[valueBuffer.length() - 1] != '\\')
          break;

        if (valueBuffer[valueBuffer.length() - 1] == '\\' && c == 'n')
          valueBuffer[valueBuffer.length() - 1] = '\n';
        else if (valueBuffer[valueBuffer.length() - 1] == '\\' && c == 'r')
          valueBuffer[valueBuffer.length() - 1] = '\r';
        else
          valueBuffer.concat(c);

        // for summary, stop at "\r\n\r\n##"
        if (targetArray == (char *)summary && valueBuffer.endsWith(F("\r\n\r\n##")))
        {
          // remove the last 6 characters
          valueBuffer.remove(valueBuffer.length() - 6);
          break;
        }
      }
    }

    // if we found the key and the value is not empty, copy it to the target array
    if (keyFound && valueBuffer.length() > 0)
    {
      strlcpy(targetArray, valueBuffer.c_str(), targetArraySize);
    }
  }

  http.end();

  return strlen(*version) > 0;
}

String Application::getLatestUpdateInfoJson(bool forWebPage /* = false */)
{
  JsonDocument doc;

  doc[F("installed_version")] = VERSION;

  char version[10] = {0};
  char title[64] = {0};
  char releaseDate[11] = {0};
  char summary[256] = {0};

  if (getLastestUpdateInfo(&version, &title, &releaseDate, &summary))
  {
    doc[F("latest_version")] = version;
    doc[F("title")] = title;
    doc[F("release_summary")] = summary;
    doc[F("release_url")] = String(F("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/tag/")) + version;

    if (forWebPage)
      doc[F("release_date")] = releaseDate;
  }

  String info;
  serializeJson(doc, info);

  return info;
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

  String fwUrl(F("https://github.com/" CUSTOM_APP_MANUFACTURER "/" CUSTOM_APP_MODEL "/releases/download/"));
#ifdef ESP8266
  fwUrl = fwUrl + version + '/' + F(CUSTOM_APP_MODEL) + '.' + version + F(".bin");
#else
  fwUrl = fwUrl + version + '/' + F(CUSTOM_APP_MODEL) + F(".esp32") + '.' + version + F(".bin");
#endif

  LOG_SERIAL_PRINTF_P(PSTR("Trying to Update from URL: %s\n"), fwUrl.c_str());

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.begin(clientSecure, fwUrl);
  int httpCode = https.GET();

  if (httpCode != 200)
  {
    https.end();

    retMsg = F("Failed to download file, httpCode: ");
    retMsg += httpCode;

    LOG_SERIAL_PRINTLN(retMsg);

    return false;
  }

  // starting here we have a valid httpCode (200)

  // get the stream
  WiFiClient *stream = https.getStreamPtr();
  int contentLength = https.getSize();

  LOG_SERIAL_PRINTF_P(PSTR("Update Start: %s (Online Update)\n"), (String(F(CUSTOM_APP_MODEL)) + '.' + version + F(".bin")).c_str());

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
  return generateStatusJSON();
}

void Application::init(bool skipExistingConfig)
{
  bool result = true;

  LOG_SERIAL_PRINTF_P(PSTR("Start %s : "), getAppIdName(_appId).c_str());

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
              server.send(200, F("text/json"), generateStatusJSON());
            });

  // JSON Config handler
  sprintf_P(url, PSTR("/gc%c"), getAppIdChar(_appId));
  server.on(url, HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));
              server.send(200, F("text/json"), generateConfigJSON());
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
    LOG_SERIAL_PRINTF_P(PSTR("ReStart %s : "), getAppIdName(_appId).c_str());

    if (appInit(true))
      LOG_SERIAL_PRINTLN(F("OK"));
    else
      LOG_SERIAL_PRINTLN(F("FAILED"));

    _reInit = false;
  }

  appRun();
}