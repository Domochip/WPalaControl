#ifndef WifiMan_h
#define WifiMan_h

#include "../Main.h"
#include "Application.h"

#include <Ticker.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif
#include <DNSServer.h>

#include "data/status1.html.gz.h"
#include "data/config1.html.gz.h"

const char predefPassword[] PROGMEM = "ewcXoCt4HHjZUvY0";

class WifiMan : public Application
{

private:
  // Configuration Properties
  char ssid[32 + 1] = {0};
  char password[64 + 1] = {0};
  char hostname[24 + 1] = {0};
  uint32_t ip = 0;
  uint32_t gw = 0;
  uint32_t mask = 0;
  uint32_t dns1 = 0;
  uint32_t dns2 = 0;

// Run properties
#ifdef ESP8266
  WiFiEventHandler _discoEventHandler;
  WiFiEventHandler _staConnectedHandler;
  WiFiEventHandler _staDisconnectedHandler;
#endif
  DNSServer *_dnsServer = nullptr;
  int _apChannel = 2;
  bool _needRefreshWifi = false;
  bool _stationConnectedToSoftAP = false;
  Ticker _refreshTicker;
  uint8_t _refreshPeriod = 60;     // try to reconnect as station mode every 60 seconds
  uint8_t _reconnectDuration = 20; // duration to try to connect to Wifi in seconds

  void enableAP(bool force);
  void refreshWiFi();

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonDocument &doc, bool fromWebPage);
  String generateConfigJSON(bool forSaveFile);
  String generateStatusJSON();
  bool appInit(bool reInit);
  const PROGMEM char *getHTMLContent(WebPageForPlaceHolder wp);
  size_t getHTMLContentSize(WebPageForPlaceHolder wp);
  void appInitWebServer(WebServer &server);
  void appRun();

public:
  WifiMan() : Application(WifiManApp) {};
};

#endif
