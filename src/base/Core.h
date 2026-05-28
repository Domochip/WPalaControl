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
  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit = false);
  HtmlPage getHTMLContent(WebPageForPlaceHolder wp);
  void appInitWebServer(WebServer &server);
  void appRun() {};

public:
  Core() : Application(CoreApp) {};
};

#endif