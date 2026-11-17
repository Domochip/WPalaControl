#ifndef Core_h
#define Core_h

#include "../Main.h"
#include "Application.h"

#ifdef ESP8266
#include "CrashSaver.h"
#endif

class Core : public Application
{
private:
  // Configuration Properties
  char ntpTimezone[64 + 1] = {0};
  char webPassword[64 + 1] = {0};

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit = false);
  void appInitWebServer(WebServer &server);
  void appRun() {};

public:
  Core() : Application(CoreApp) {};
  static const char *getSerialNumber();
  // to be called before any other web server initialization
  void initWebAuth(WebServer &server);
  void mqttPublishHassDiscovery(HassDiscoveryCtx &ctx);
};

#endif