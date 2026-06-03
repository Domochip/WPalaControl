#ifndef Application_h
#define Application_h

#include "../Main.h"
#include "SystemState.h"
#include <LittleFS.h>
#ifdef ESP8266
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#define SERVER_KEEPALIVE_FALSE() server.keepAlive(false);
#include <ESP8266HTTPClient.h>
#else
#include <WebServer.h>
#define SERVER_KEEPALIVE_FALSE()
#include <HTTPClient.h>
#include <Update.h>
#endif
#include <ArduinoJson.h>
#include <Ticker.h>

// Predefined placeholder sent to the web page instead of the real password value
const char predefPassword[] PROGMEM = "ewcXoCt4HHjZUvY0";

class Application
{
protected:
  typedef enum
  {
    CoreApp = 0,
    WifiManApp = 1,
    CustomApp = 2
  } AppId;

  static Application *_applicationList[3]; // static list of all applications

  AppId _appId;
  bool _reInit = false;

  // already built methods
  bool saveConfig();
  bool loadConfig();

  static void fillSecret(JsonVariant json, const __FlashStringHelper *key, const char *value, bool forSaveFile);
  static void parseSecret(JsonVariant jv, char *dest, size_t size, bool fromWebPage);

  static bool getLatestUpdateInfo(char *version, char *title = nullptr, char *releaseDate = nullptr, char *summary = nullptr);
  static void fillLatestUpdateInfoJson(JsonVariant json, bool forWebPage = false);
  static bool updateFirmware(const char *version, String &retMsg, std::function<void(size_t, size_t)> progressCallback = nullptr);

public:
  Application(AppId appId);

  static char getAppIdChar(AppId appId);
  static const __FlashStringHelper *getAppIdName(AppId appId);
  void init(bool skipExistingConfig);
  void initWebServer(WebServer &server);
  void run();

  // ----------
  // specialization required from the application
  // ----------

protected:
  virtual bool appInit(bool reInit = false) = 0;
  virtual void appInitWebServer(WebServer &server) = 0;
  virtual void appRun() = 0;
  virtual void fillConfigJSON(JsonVariant json, bool forSaveFile = false) = 0;
  virtual bool parseConfigJSON(JsonVariant json, bool fromWebPage = false) = 0;
  virtual void validateConfig() {}
  virtual void setConfigDefaultValues() = 0;

public:
  virtual void fillStatusJSON(JsonVariant json) = 0;
};

#endif