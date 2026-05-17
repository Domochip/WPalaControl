#ifndef Core_h
#define Core_h

#include "../Main.h"
#include "Application.h"

#include "data/status0.html.gz.h"
#include "data/config0.html.gz.h"
#include "data/fw.html.gz.h"

#ifdef ESP8266
#include "CrashSaver.h"
#endif

class Core : public Application
{
private:
  void setConfigDefaultValues();
  bool parseConfigJSON(JsonDocument &doc, bool fromWebPage);
  void fillConfigJSON(JsonDocument &doc, bool forSaveFile);
  void fillStatusJSON(JsonDocument &doc);
  bool appInit(bool reInit);
  const PROGMEM char *getHTMLContent(WebPageForPlaceHolder wp);
  size_t getHTMLContentSize(WebPageForPlaceHolder wp);
  void appInitWebServer(WebServer &server);
  void appRun() {};

public:
  Core() : Application(CoreApp) {};
};

#endif