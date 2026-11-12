#include "WifiMan.h"

// WPA2 requires 8 to 63 characters (empty = open AP); softAP() fails otherwise
static_assert(sizeof(DEFAULT_AP_PSK) == 1 || (sizeof(DEFAULT_AP_PSK) - 1 >= 8 && sizeof(DEFAULT_AP_PSK) - 1 <= 63),
              "DEFAULT_AP_PSK must be empty or 8 to 63 characters long");
static_assert(sizeof(DEFAULT_AP_SSID) - 1 <= 32, "DEFAULT_AP_SSID must be at most 32 characters long");

const char *WifiMan::ipToCString(IPAddress ip)
{
  static char buf[16];
  if (!ip)
    buf[0] = '\0';
  else
    snprintf_P(buf, sizeof(buf), PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
  return buf;
}

const char *WifiMan::getMacAddress()
{
  static char mac[18] = {0};

  // Calculate MAC address only on first call
  if (mac[0] == '\0')
  {
    uint8_t macBuf[6];
    WiFi.macAddress(macBuf);
    snprintf_P(mac, sizeof(mac), PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), macBuf[0], macBuf[1], macBuf[2], macBuf[3], macBuf[4], macBuf[5]);
  }

  return mac;
}

// Scan networks and return the least crowded non-overlapping channel (1, 6 or 11)
// each network overlapping a candidate (+/-4 channels) adds a weight based on its signal strength
// returns 1 if no network is found or if the scan fails
static uint8_t scanForApChannel()
{
  static const uint8_t candidates[] = {1, 6, 11};
  uint8_t best = candidates[0];
  uint32_t bestScore = UINT32_MAX;

  int n = WiFi.scanNetworks();

  if (n > 0)
  {
    for (uint8_t candidate : candidates)
    {
      uint32_t score = 0;
      for (int i = 0; i < n; i++)
      {
        if (abs((int)WiFi.channel(i) - candidate) <= 4)
          score += constrain((int)WiFi.RSSI(i) + 100, 1, 100); // -40dBm weighs more than -90dBm
      }
      if (score < bestScore)
      {
        bestScore = score;
        best = candidate;
      }
    }
  }
  WiFi.scanDelete();

  LOG_SERIAL_PRINTF_P(PSTR("%dN-CH%u "), n, best);
  return best;
}

// Start the AP (with captive portal DNS) if not already running
// returns true if the AP is running (already or just started)
bool WifiMan::enableAP()
{
  // AP already running: nothing to do (and keep its channel)
  if (WiFi.getMode() & WIFI_AP)
    return true;

  // AP is off: no client can be disturbed by a scan, so choose the channel now
  uint8_t apChannel = scanForApChannel();

  if (!WiFi.softAP(F(DEFAULT_AP_SSID), F(DEFAULT_AP_PSK), apChannel))
  {
    LOG_SERIAL_PRINTLN(F("/!\\ softAP failed (check SSID / PSK) /!\\"));
    WiFi.enableAP(false); // disable AP mode for a later retry if softAP failed
    return false;
  }

  // wait (max 2s) for the AP to get its IP
  // compare to 0.0.0.0 explicitly: INADDR_NONE is 255.255.255.255 on ESP8266 but 0.0.0.0 on ESP32 ...
  const IPAddress noIP(0, 0, 0, 0);
  uint32_t waitStart = millis();
  while (WiFi.softAPIP() == noIP && millis() - waitStart < 2000)
    delay(10);

  if (WiFi.softAPIP() == noIP)
  {
    LOG_SERIAL_PRINTLN(F("/!\\ softAP has no IP /!\\"));
    WiFi.enableAP(false); // disable AP mode for a later retry if softAP failed
    return false;
  }

  // start captive portal DNS server
  _dnsServer = new DNSServer();
  _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer->start(53, F("*"), WiFi.softAPIP());

  LOG_SERIAL_PRINTF_P(PSTR("AP started (" DEFAULT_AP_SSID " - ch %u - %s) "), apChannel, ipToCString(WiFi.softAPIP()));
  return true;
}

void WifiMan::setStaState(StaState state)
{
  _staState = state;
  _staStateMillis = millis();
}

// Start a connection attempt
void WifiMan::startConnect()
{
  WiFi.config(ip, gw, mask, dns1, dns2);
  WiFi.begin(ssid, password);
  setStaState(StaState::Connecting);
}

void WifiMan::onConnected()
{
  // stop DNS server
  if (_dnsServer)
  {
    _dnsServer->stop();
    delete _dnsServer;
    _dnsServer = nullptr;
  }
  // disable AP
  WiFi.enableAP(false);
  _connectionCount++;

  // Init TimeManager for NTP synchronization when WiFi is connected
  TimeManager::init();

  setStaState(StaState::Connected);

  LOG_SERIAL_PRINTF_P(PSTR("WiFi connected (%s)\n"), ipToCString(WiFi.localIP()));
}

void WifiMan::onConnectFailed()
{
  LOG_SERIAL_PRINTF_P(PSTR("WiFi connection failed (reason: %u), enabling AP, next attempt in %us\n"), _lastDiscoReason, _refreshPeriod);

  // stop STA activity first, so the AP channel scan runs with an idle radio
  WiFi.disconnect();
  // enable AP as fallback to let the user reach the device
  enableAP();
  setStaState(StaState::WaitRetry);
}

// Initial connection, blocking (used by appInit only: state is Off and STA disconnected)
void WifiMan::initialConnect()
{
  if (staEnabled())
  {
    LOG_SERIAL_PRINT(F("Connect"));

    startConnect();

    // Wait _reconnectDuration for connection
    for (int i = 0; i < (((uint16_t)_reconnectDuration) * 10) && !WiFi.isConnected(); i++)
    {
      if ((i % 10) == 0)
        LOG_SERIAL_PRINT(".");
      delay(100);
    }

    // if connection is successfull
    if (WiFi.isConnected())
      onConnected();
    else // connection failed
      onConnectFailed();
  }
  else // else if AP is configured
  {
    // AP is the normal mode here (if it fails to start, appRun retries periodically)
    enableAP();
  }
}

void WifiMan::setConfigDefaultValues()
{
  ssid[0] = 0;
  password[0] = 0;
  strcpy_P(hostname, PSTR(CUSTOM_APP_MODEL));
  ip = 0;
  gw = 0;
  mask = 0;
  dns1 = 0;
  dns2 = 0;
}

bool WifiMan::parseConfigJSON(JsonVariant json, bool fromWebPage /* = false */)
{
  parseField(json["s"], ssid);
  parseSecret(json["p"], password, fromWebPage);
  parseField(json["h"], hostname);
  parseIPField(json["ip"], ip);
  parseIPField(json["gw"], gw);
  parseIPField(json[F("mask")], mask);
  parseIPField(json[F("dns1")], dns1);
  parseIPField(json[F("dns2")], dns2);

  // staticip : when false, DHCP is used
  JsonVariant staticip = json[F("staticip")];
  if (!staticip.isNull() && !staticip.as<bool>())
    ip = 0;

  return true;
}

void WifiMan::validateConfig()
{
  // no IP means DHCP: gateway, mask and DNS are meaningless, don't keep stale values
  if (!ip)
    gw = mask = dns1 = dns2 = 0;
}

void WifiMan::fillConfigJSON(JsonVariant json, bool forSaveFile /* = false */)
{
  json["s"] = ssid;

  fillSecret(json, F("p"), password, forSaveFile);

  json["h"] = hostname;

  json["ip"] = ipToCString(ip);
  json["gw"] = ipToCString(gw);
  json[F("mask")] = ipToCString(mask);
  json[F("dns1")] = ipToCString(dns1);
  json[F("dns2")] = ipToCString(dns2);

  // for the web page checkbox only
  if (!forSaveFile)
    json[F("staticip")] = (ip != 0);
}

void WifiMan::fillStatusJSON(JsonVariant json)
{

  if ((WiFi.getMode() & WIFI_AP))
  {
    json[F("apmode")] = F("on");
    json[F("apip")] = ipToCString(WiFi.softAPIP());
  }
  else
    json[F("apmode")] = F("off");

  if (staEnabled())
  {
    json[F("stationmode")] = F("on");
    if (WiFi.isConnected())
    {
      json[F("stationip")] = ipToCString(WiFi.localIP());
      json[F("stationipsource")] = ip ? F("Static IP") : F("DHCP");
      json[F("bssid")] = WiFi.BSSIDstr();
      json[F("rssi")] = WiFi.RSSI();
    }
  }
  else
    json[F("stationmode")] = F("off");

  // station connection state
  switch (_staState)
  {
  case StaState::Connecting:
    json[F("stastate")] = F("connecting");
    break;
  case StaState::Connected:
    json[F("stastate")] = F("connected");
    break;
  case StaState::Lost:
    json[F("stastate")] = F("lost");
    break;
  case StaState::WaitRetry:
  {
    json[F("stastate")] = F("waitretry");
    // seconds until next attempt (forced attempt after _maxRetryDelay if a client is connected to the softAP)
    uint32_t elapsed = millis() - _staStateMillis;
    uint32_t target = (WiFi.softAPgetStationNum() == 0 ? _refreshPeriod : _maxRetryDelay) * 1000UL;
    json[F("nextretry")] = (elapsed < target) ? (target - elapsed + 999) / 1000 : 0;
    break;
  }
  default:
    json[F("stastate")] = F("off");
    break;
  }

  json[F("mac")] = getMacAddress();
  json[F("connectcount")] = _connectionCount;
  json[F("discoreason")] = _lastDiscoReason;
}

bool WifiMan::appInit(bool reInit /* = false */)
{
  // do not store WiFi config in SDK flash/NVS
  WiFi.persistent(false);

  // Set hostname before any WiFi start (ESP32 applies it to the STA interface only when the WiFi mode changes)
  WiFi.hostname(hostname);

  // Initialize connection state and disconnect before WiFi operations -----
  setStaState(StaState::Off);
  WiFi.disconnect();

  // let the SDK handle short drops by itself (see appRun, StaState::Lost)
  WiFi.setAutoReconnect(true);

  // saving WiFi config (reInit) leaves Recovery Mode
  if (reInit)
    SystemState::recoveryMode = false;

  // Configure handlers
  if (!reInit)
  {
    // only keep the reason (also for failed attempts: wrong password, AP not found, ...)
    // detection, log and reconnection are handled by appRun
#ifdef ESP8266
    _discoEventHandler = WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected &evt)
                                                        { _lastDiscoReason = evt.reason; });
#else
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)
                 { _lastDiscoReason = info.wifi_sta_disconnected.reason; },
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif
  }

  // Initial connection (blocking, enables AP when needed)
  initialConnect();

  // start MDNS
  if (reInit)
    MDNS.end();
  MDNS.begin(CUSTOM_APP_MODEL);

  return (staEnabled() ? WiFi.isConnected() : true);
}

void WifiMan::appInitWebServer(WebServer &server)
{
  server.on(F("/wnl"), HTTP_GET,
            [this, &server]()
            {
              // prepare response
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("no-cache"));

              int8_t n = WiFi.scanComplete();
              if (n == -2)
              {
                server.send(200, F("application/json"), F("{\"r\":-2,\"wnl\":[]}"));
                WiFi.scanNetworks(true);
              }
              else if (n == -1)
              {
                server.send(200, F("application/json"), F("{\"r\":-1,\"wnl\":[]}"));
              }
              else
              {
                JsonDocument json;
                json["r"] = n;
                JsonArray wnl = json[F("wnl")].to<JsonArray>();
                for (uint8_t i = 0; i < n; i++)
                {
                  JsonObject wnl0 = wnl.add<JsonObject>();
                  wnl0["SSID"] = WiFi.SSID(i);
                  wnl0["RSSI"] = WiFi.RSSI(i);
                }
                server.setContentLength(measureJson(json));
                server.send(200, F("application/json"), "");
                WiFiClient client = server.client();
                serializeJson(json, client);
                WiFi.scanDelete();
              }
            });
}

void WifiMan::appRun()
{
  // Non-blocking station connection management
  uint32_t elapsed = millis() - _staStateMillis;

  switch (_staState)
  {
  case StaState::Off:
    // AP-only mode: make sure the AP is running (retry if it failed to start)
    if (!(WiFi.getMode() & WIFI_AP) && elapsed >= _refreshPeriod * 1000UL)
    {
      setStaState(StaState::Off); // restart the retry delay
      enableAP();
    }
    break;

  case StaState::Connected:
    // connection lost: let the SDK auto-reconnect for a while (AP stays off)
    if (!WiFi.isConnected())
    {
      LOG_SERIAL_PRINTF_P(PSTR("WiFi disconnected (reason: %u)\n"), _lastDiscoReason);
      setStaState(StaState::Lost);
    }
    break;

  case StaState::Lost:
    if (WiFi.isConnected())
      onConnected();
    else if (elapsed >= _autoReconnectGrace * 1000UL)
      onConnectFailed(); // drop is not short: handle it as a failed attempt (AP enabled, next attempt after _refreshPeriod)
    break;

  case StaState::Connecting:
    if (WiFi.isConnected())
      onConnected();
    else if (elapsed >= _reconnectDuration * 1000UL)
      onConnectFailed();
    break;

  case StaState::WaitRetry:
    // retry only if no client is connected to the softAP
    // (the attempt would make the AP follow the router channel and kick the client)
    // but don't wait forever: retry anyway after _maxRetryDelay
    if (elapsed >= _refreshPeriod * 1000UL)
    {
      if (WiFi.softAPgetStationNum() == 0)
        startConnect();
      else if (elapsed >= _maxRetryDelay * 1000UL)
      {
        LOG_SERIAL_PRINTLN(F("WiFi retry forced (client still connected to AP)"));
        startConnect();
      }
    }
    break;

  default:
    break;
  }

  if (_dnsServer)
    _dnsServer->processNextRequest();

#ifdef ESP8266
  MDNS.update();
#endif
}

void WifiMan::mqttPublishHassDiscovery(HassDiscoveryCtx &ctx)
{
  JsonDocument json;

  //
  // Wifi connection counter entity
  //

  // prepare payload for wifi connection counter sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor." CUSTOM_APP_MODEL "_wifi_connect_count\","
                          "\"entity_category\":\"diagnostic\","
                          "\"icon\":\"mdi:counter\","
                          "\"name\":\"WiFi Connect Count\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_wifi_connect_count\","
                          "\"state_topic\":\"~/WiFi\","
                          "\"value_template\":\"{{ value_json.connectcount }}\""
                          "}"));
  ctx.publishEntity(json, F("sensor"), F("WifiConnectCount"));

  //
  // Wifi last disconnection reason entity
  //

  // prepare payload for wifi last disconnection reason sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor." CUSTOM_APP_MODEL "_wifi_disco_reason\","
                          "\"entity_category\":\"diagnostic\","
                          "\"icon\":\"mdi:wifi-remove\","
                          "\"name\":\"WiFi Last Disconnection Reason\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_wifi_disco_reason\","
                          "\"state_topic\":\"~/WiFi\","
                          "\"value_template\":\""
                          "{% set r={0:'No disconnection',1:'Unspecified',2:'Auth expired',3:'Auth leave',4:'Assoc expired',8:'Station left',15:'4-way handshake timeout',16:'Group key update timeout',200:'Beacon timeout',201:'No AP found',202:'Auth failed',203:'Assoc failed',204:'Handshake timeout',205:'Connection failed'} %}"
                          "{{ r.get(value_json.discoreason|int,'Unknown') }}\""
                          "}"));
  ctx.publishEntity(json, F("sensor"), F("WifiDiscoReason"));

  //
  // WiFi RSSI entity
  //

  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor." CUSTOM_APP_MODEL "_wifi_rssi\","
                          "\"device_class\":\"signal_strength\","
                          "\"enabled_by_default\":false,"
                          "\"entity_category\":\"diagnostic\","
                          "\"name\":\"WiFi RSSI\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_wifi_rssi\","
                          "\"state_class\":\"measurement\","
                          "\"state_topic\":\"~/WiFi\","
                          "\"unit_of_measurement\":\"dBm\","
                          "\"value_template\":\"{{ value_json.rssi }}\""
                          "}"));
  ctx.publishEntity(json, F("sensor"), F("WifiRssi"));

  //
  // WiFi BSSID entity
  //

  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"sensor." CUSTOM_APP_MODEL "_wifi_bssid\","
                          "\"enabled_by_default\":false,"
                          "\"entity_category\":\"diagnostic\","
                          "\"icon\":\"mdi:wifi-marker\","
                          "\"name\":\"WiFi BSSID\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_wifi_bssid\","
                          "\"state_topic\":\"~/WiFi\","
                          "\"value_template\":\"{{ value_json.bssid | default('') }}\""
                          "}"));
  ctx.publishEntity(json, F("sensor"), F("WifiBssid"));
}