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
  uint8_t _apChannel = 2;
  bool _needRefreshWifi = false;
  bool _stationConnectedToSoftAP = false;
  Ticker _refreshTicker;
  uint8_t _refreshPeriod = 60;     // try to reconnect as station mode every 60 seconds
  uint8_t _reconnectDuration = 20; // duration to try to connect to Wifi in seconds
  uint16_t _connectionCount = 0;   // count of successful WiFi connections

  void enableAP(bool force = false);
  void refreshWiFi();

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void fillStatusJSON(JsonVariant json);
  bool appInit(bool reInit = false);
  void appInitWebServer(WebServer &server);
  void appRun();

public:
  WifiMan() : Application(WifiManApp) {};
  // helper to convert IPAddress to const char*
  static const char *ipToCString(IPAddress ip);
  // helper to get MAC address as const char* (cached)
  static const char *getMacAddress();
  void mqttPublishHassDiscovery(HassDiscoveryCtx &ctx);
};

#endif
