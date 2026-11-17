#ifndef WifiMan_h
#define WifiMan_h

#include "../Main.h"
#include "Application.h"

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
#endif
  // Station connection state
  enum class StaState : uint8_t
  {
    Off,        // no station configured (AP only)
    Connecting, // WiFi.begin() called, waiting for connection (AP kept as is: off on first attempt, on during retries)
    Connected,  // connected (AP disabled)
    Lost,       // connection lost, SDK auto-reconnect is trying (AP still disabled)
    WaitRetry   // connection attempt failed, waiting before next attempt (AP enabled)
  };

  DNSServer *_dnsServer = nullptr;
  StaState _staState = StaState::Off;
  uint32_t _staStateMillis = 0;          // millis() when _staState was last changed
  uint8_t _refreshPeriod = 60;           // try to reconnect as station mode every 60 seconds
  uint16_t _maxRetryDelay = 300;         // retry even if a client is connected to the softAP after this delay, in seconds
  uint8_t _reconnectDuration = 20;       // duration to try to connect to Wifi in seconds
  uint8_t _autoReconnectGrace = 15;      // time left to SDK auto-reconnect after a drop before falling back to AP, in seconds
  uint16_t _connectionCount = 0;         // count of successful WiFi connections
  volatile uint8_t _lastDiscoReason = 0; // last WiFi disconnection reason code (written from WiFi event)

  bool staEnabled() { return ssid[0] && !SystemState::recoveryMode; }
  bool enableAP();
  void setStaState(StaState state);
  void startConnect();
  void onConnected();
  void onConnectFailed();
  void initialConnect();

  void setConfigDefaultValues();
  bool parseConfigJSON(JsonVariant json, bool fromWebPage = false);
  void fillConfigJSON(JsonVariant json, bool forSaveFile = false);
  void validateConfig();
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
