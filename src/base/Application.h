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

  typedef enum
  {
    status,
    config
  } WebPageForPlaceHolder;

  AppId _appId;
  bool _reInit = false;

  // already built methods
  bool saveConfig();
  bool loadConfig();

  static bool getLastestUpdateInfo(char *version, char *title, char *releaseDate, char *summary);
  static void fillLatestUpdateInfoJson(JsonDocument &doc, bool forWebPage = false);
  static bool updateFirmware(const char *version, String &retMsg, std::function<void(size_t, size_t)> progressCallback = nullptr);

  // specialization required from the application
  virtual void setConfigDefaultValues() = 0;
  virtual bool parseConfigJSON(JsonDocument &doc, bool fromWebPage = false) = 0;
  virtual void fillConfigJSON(JsonDocument &doc, bool forSaveFile = false) = 0;
  virtual void fillStatusJSON(JsonDocument &doc) = 0;
  virtual bool appInit(bool reInit = false) = 0;
  virtual const PROGMEM char *getHTMLContent(WebPageForPlaceHolder wp) = 0;
  virtual size_t getHTMLContentSize(WebPageForPlaceHolder wp) = 0;
  virtual void appInitWebServer(WebServer &server) = 0;
  virtual void appRun() = 0;

public:
  Application(AppId appId);

  static char getAppIdChar(AppId appId);
  static const __FlashStringHelper *getAppIdName(AppId appId);
  String getStatusJSON();
  void init(bool skipExistingConfig);
  void initWebServer(WebServer &server);
  void run();
};

#endif