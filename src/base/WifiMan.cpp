#include "WifiMan.h"

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

void WifiMan::enableAP(bool force /* = false */)
{
  if (!(WiFi.getMode() & WIFI_AP) || force)
  {
    WiFi.enableAP(true);
    WiFi.softAP(F(DEFAULT_AP_SSID), F(DEFAULT_AP_PSK), _apChannel);
    // Free existing DNS server if any, and start a new one
    if (_dnsServer)
    {
      _dnsServer->stop();
      delete _dnsServer;
      _dnsServer = nullptr;
    }
    _dnsServer = new DNSServer();
    _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    while (WiFi.softAPIP() == INADDR_NONE)
    {
      delay(10);
    }
    _dnsServer->start(53, F("*"), WiFi.softAPIP());
  }
}

void WifiMan::refreshWiFi()
{
  if (ssid[0]) // if STA configured
  {
    if (!WiFi.isConnected() || WiFi.SSID() != ssid || WiFi.psk() != password)
    {
      enableAP();

      LOG_SERIAL_PRINT(F("Connect"));

      WiFi.config(ip, gw, mask, dns1, dns2);
      WiFi.begin(ssid, password);

      // Wait _reconnectDuration for connection
      for (int i = 0; i < (((uint16_t)_reconnectDuration) * 10) && !WiFi.isConnected(); i++)
      {
        if ((i % 10) == 0)
          LOG_SERIAL_PRINT(".");
        delay(100);
      }

      // if connection is successfull
      if (WiFi.isConnected())
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
#ifdef STATUS_LED_GOOD
        STATUS_LED_GOOD
#endif
        _connectionCount++;

        LOG_SERIAL_PRINTF_P(PSTR("Connected (%s) "), ipToCString(WiFi.localIP()));
      }
      else // connection failed
      {
        WiFi.disconnect();
        LOG_SERIAL_PRINT(F("AP not found "));
#ifdef ESP8266
        _refreshTicker.once(_refreshPeriod, [this]()
                            { _needRefreshWifi = true; });
#else
        _refreshTicker.once<WifiMan *>(_refreshPeriod, [](WifiMan *wifiMan)
                                       { wifiMan->_needRefreshWifi = true; }, this);
#endif
      }
    }
  }
  else // else if AP is configured
  {
    _refreshTicker.detach();
    enableAP();
    WiFi.disconnect();
#ifdef STATUS_LED_GOOD
    STATUS_LED_GOOD
#endif

    LOG_SERIAL_PRINTF_P(PSTR(" AP mode(%s - %s) "), F(DEFAULT_AP_SSID), ipToCString(WiFi.softAPIP()));
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
  JsonVariant jv;

  if ((jv = json["s"]).is<const char *>())
    strlcpy(ssid, jv, sizeof(ssid));

  parseSecret(json["p"], password, sizeof(password), fromWebPage);

  if ((jv = json["h"]).is<const char *>())
    strlcpy(hostname, jv, sizeof(hostname));

  IPAddress ipParser;
  if ((jv = json["ip"]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      ip = ipParser;
    else
      ip = 0;
  }

  if ((jv = json["gw"]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      gw = ipParser;
    else
      gw = 0;
  }

  if ((jv = json[F("mask")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      mask = ipParser;
    else
      mask = 0;
  }

  if ((jv = json[F("dns1")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      dns1 = ipParser;
    else
      dns1 = 0;
  }

  if ((jv = json[F("dns2")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      dns2 = ipParser;
    else
      dns2 = 0;
  }

  return true;
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

  if (ssid[0])
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

  json[F("mac")] = getMacAddress();
  json[F("connectcount")] = _connectionCount;
  json[F("discoreason")] = _lastDiscoReason;
}

bool WifiMan::appInit(bool reInit /* = false */)
{

  // make changes saved to flash
  WiFi.persistent(true);

  // Enable AP at start
  enableAP(true);

  // Stop RefreshWiFi and disconnect before WiFi operations -----
  _refreshTicker.detach();
  WiFi.disconnect();

  // scan networks to search for best free channel
  int n = WiFi.scanNetworks();

  LOG_SERIAL_PRINTF_P(PSTR("%dN-CH"), n);

  if (n)
  {
    while (_apChannel < 12)
    {
      int i = 0;
      while (i < n && WiFi.channel(i) != _apChannel)
        i++;
      if (i == n)
        break;
      _apChannel++;
    }
  }

  LOG_SERIAL_PRINT(_apChannel);
  LOG_SERIAL_PRINT(' ');

  // Configure handlers
  if (!reInit)
  {
#ifdef ESP8266
    _discoEventHandler = WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected &evt)
#else
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)
#endif
                                                        {
                                                          if (!(WiFi.getMode() & WIFI_AP) && ssid[0])
                                                          {
                                                            // stop reconnection
                                                            WiFi.disconnect();
                                                            LOG_SERIAL_PRINT(F("Wifi disconnected (reason: "));
#ifdef ESP8266
                                                            _lastDiscoReason = evt.reason;
#else
                     _lastDiscoReason = info.wifi_sta_disconnected.reason;
#endif
                                                            LOG_SERIAL_PRINT(_lastDiscoReason);
                                                            LOG_SERIAL_PRINTLN(')');
                                                            // call RefreshWifi shortly
                                                            _needRefreshWifi = true;
                                                          }
#ifdef STATUS_LED_WARNING
                                                          STATUS_LED_WARNING
#endif
                                                        }
#ifndef ESP8266
                                                        ,
                                                        WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED
#endif
    );

    // if station connect to softAP
#ifdef ESP8266
    _staConnectedHandler = WiFi.onSoftAPModeStationConnected([this](const WiFiEventSoftAPModeStationConnected &evt)
#else
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)
#endif
                                                             {
      //flag it in _stationConnectedToSoftAP
      _stationConnectedToSoftAP = true; }
#ifndef ESP8266
                                                             ,
                                                             WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED
#endif
    );

    // if station disconnect of the softAP
#ifdef ESP8266
    _staDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected([this](const WiFiEventSoftAPModeStationDisconnected &evt)
#else
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)
#endif
                                                                   {
      //check if a station left
      _stationConnectedToSoftAP = WiFi.softAPgetStationNum(); }
#ifndef ESP8266
                                                                   ,
                                                                   WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED
#endif
    );
  }

  // Set hostname
  WiFi.hostname(hostname);

  // Call RefreshWiFi to initiate configuration
  refreshWiFi();

  // right config so no need to touch again flash
  WiFi.persistent(false);

  // start MDNS
  MDNS.begin(CUSTOM_APP_MODEL);

  return (ssid[0] ? WiFi.isConnected() : true);
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
                server.send(200, F("text/json"), F("{\"r\":-2,\"wnl\":[]}"));
                WiFi.scanNetworks(true);
              }
              else if (n == -1)
              {
                server.send(200, F("text/json"), F("{\"r\":-1,\"wnl\":[]}"));
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
                server.send(200, F("text/json"), "");
                WiFiClient client = server.client();
                serializeJson(json, client);
                WiFi.scanDelete();
              }
            });
}

void WifiMan::appRun()
{
  // if refreshWifi is required and no client is connected to the softAP
  if (_needRefreshWifi && !_stationConnectedToSoftAP)
  {
    _needRefreshWifi = false;
    refreshWiFi();
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