#include "WifiMan.h"

void WifiMan::enableAP(bool force = false)
{
  if (!(WiFi.getMode() & WIFI_AP) || force)
  {
    WiFi.enableAP(true);
    WiFi.softAP(F(DEFAULT_AP_SSID), F(DEFAULT_AP_PSK), _apChannel);
    // Start DNS server
    _dnsServer = new DNSServer();
    _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    while (!WiFi.softAPIP().isSet())
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

        LOG_SERIAL_PRINTF_P(PSTR("Connected (%s) "), WiFi.localIP().toString().c_str());
      }
      else // connection failed
      {
        WiFi.disconnect();
        LOG_SERIAL_PRINT(F("AP not found "));
#ifdef ESP8266
        _refreshTicker.once(_refreshPeriod, [this]()
                            { _needRefreshWifi = true; });
#else
        _refreshTicker.once<typeof this>(_refreshPeriod * 1000, [](typeof this wifiMan)
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

    LOG_SERIAL_PRINTF_P(PSTR(" AP mode(%s - %s) "), F(DEFAULT_AP_SSID), WiFi.softAPIP().toString().c_str());
  }
}

void WifiMan::setConfigDefaultValues()
{
  ssid[0] = 0;
  password[0] = 0;
  hostname[0] = 0;
  ip = 0;
  gw = 0;
  mask = 0;
  dns1 = 0;
  dns2 = 0;
}

bool WifiMan::parseConfigJSON(JsonDocument &doc, bool fromWebPage = false)
{
  JsonVariant jv;
  char tempPassword[64 + 1] = {0};

  if ((jv = doc["s"]).is<const char *>())
    strlcpy(ssid, jv, sizeof(ssid));

  if ((jv = doc["p"]).is<const char *>())
  {
    strlcpy(tempPassword, jv, sizeof(tempPassword));

    // if not from web page or password is not the predefined one
    if (!fromWebPage || strcmp_P(tempPassword, predefPassword))
      strcpy(password, tempPassword);
  }

  if ((jv = doc["h"]).is<const char *>())
    strlcpy(hostname, jv, sizeof(hostname));

  IPAddress ipParser;
  if ((jv = doc["ip"]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      ip = static_cast<uint32_t>(ipParser);
    else
      ip = 0;
  }

  if ((jv = doc["gw"]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      gw = static_cast<uint32_t>(ipParser);
    else
      gw = 0;
  }

  if ((jv = doc[F("mask")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      mask = static_cast<uint32_t>(ipParser);
    else
      mask = 0;
  }

  if ((jv = doc[F("dns1")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      dns1 = static_cast<uint32_t>(ipParser);
    else
      dns1 = 0;
  }

  if ((jv = doc[F("dns2")]).is<const char *>())
  {
    if (ipParser.fromString(jv.as<const char *>()))
      dns2 = static_cast<uint32_t>(ipParser);
    else
      dns2 = 0;
  }

  return true;
}

String WifiMan::generateConfigJSON(bool forSaveFile = false)
{
  JsonDocument doc;

  doc["s"] = ssid;

  if (forSaveFile)
    doc["p"] = password;
  else
    // there is a predefined special password (mean to keep already saved one)
    doc["p"] = (const __FlashStringHelper *)predefPassword;

  doc["h"] = hostname;

  doc[F("staticip")] = (ip ? 1 : 0);
  if (ip)
    doc["ip"] = IPAddress(ip).toString();
  if (gw)
    doc["gw"] = IPAddress(gw).toString();
  else
    doc["gw"] = F("0.0.0.0");
  if (mask)
    doc[F("mask")] = IPAddress(mask).toString();
  else
    doc[F("mask")] = F("0.0.0.0");
  if (dns1)
    doc[F("dns1")] = IPAddress(dns1).toString();
  if (dns2)
    doc[F("dns2")] = IPAddress(dns2).toString();

  String gc;
  serializeJson(doc, gc);

  return gc;
}

String WifiMan::generateStatusJSON()
{
  JsonDocument doc;

  if ((WiFi.getMode() & WIFI_AP))
  {
    doc[F("apmode")] = F("on");
    doc[F("apip")] = WiFi.softAPIP().toString();
  }
  else
    doc[F("apmode")] = F("off");

  if (ssid[0])
  {
    doc[F("stationmode")] = F("on");
    if (WiFi.isConnected())
    {
      doc[F("stationip")] = WiFi.localIP().toString();
      doc[F("stationipsource")] = ip ? F("Static IP") : F("DHCP");
    }
  }
  else
    doc[F("stationmode")] = F("off");

  doc[F("mac")] = WiFi.macAddress();

  String gs;
  serializeJson(doc, gs);

  return gs;
}

bool WifiMan::appInit(bool reInit = false)
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

const PROGMEM char *WifiMan::getHTMLContent(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return status1htmlgz;
    break;
  case config:
    return config1htmlgz;
    break;
  default:
    return nullptr;
    break;
  };
  return nullptr;
};
size_t WifiMan::getHTMLContentSize(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return sizeof(status1htmlgz);
    break;
  case config:
    return sizeof(config1htmlgz);
    break;
  default:
    return 0;
    break;
  };
  return 0;
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
                JsonDocument doc;
                doc["r"] = n;
                JsonArray wnl = doc[F("wnl")].to<JsonArray>();
                for (byte i = 0; i < n; i++)
                {
                  JsonObject wnl0 = wnl.add<JsonObject>();
                  wnl0["SSID"] = WiFi.SSID(i);
                  wnl0["RSSI"] = WiFi.RSSI(i);
                }
                String networksJSON;
                serializeJson(doc, networksJSON);

                server.send(200, F("text/json"), networksJSON);
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