#include "WifiMan.h"

const char *WifiMan::ipToCString(IPAddress ip)
{
  static char buf[16];
  snprintf_P(buf, sizeof(buf), PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
  return buf;
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

      WiFi.begin(ssid, password);
      WiFi.config(ip, gw, mask, dns1, dns2);

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
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
        // disable AP
        WiFi.enableAP(false);
#ifdef STATUS_LED_GOOD
        STATUS_LED_GOOD
#endif

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
        _refreshTicker.once<typeof this>(_refreshPeriod, [](typeof this wifiMan)
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

  if ((jv = json["p"]).is<const char *>())
  {
    // if not from web page or password is not the predefined one
    if (!fromWebPage || strcmp_P(jv, predefPassword))
      strlcpy(password, jv, sizeof(password));
  }

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

  if (forSaveFile)
    json["p"] = password;
  else
    // there is a predefined special password (used to keep already saved one)
    json["p"] = (const __FlashStringHelper *)predefPassword;

  json["h"] = hostname;

  json[F("staticip")] = (ip ? 1 : 0);

  if (ip)
    json["ip"] = ipToCString(ip);
  if (gw)
    json["gw"] = ipToCString(gw);
  else
    json["gw"] = F("0.0.0.0");
  if (mask)
    json[F("mask")] = ipToCString(mask);
  else
    json[F("mask")] = F("0.0.0.0");
  if (dns1)
    json[F("dns1")] = ipToCString(dns1);
  if (dns2)
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
    }
  }
  else
    json[F("stationmode")] = F("off");

  uint8_t macBuf[6];
  char mac[18];
  WiFi.macAddress(macBuf);
  snprintf_P(mac, sizeof(mac), PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), macBuf[0], macBuf[1], macBuf[2], macBuf[3], macBuf[4], macBuf[5]);
  json[F("mac")] = mac;
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
                                                            LOG_SERIAL_PRINTLN(F("Wifi disconnected"));
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
                for (byte i = 0; i < n; i++)
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