#ifndef EventSourceMan_h
#define EventSourceMan_h

#include "../Main.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#include <WiFi.h>
#include <WebServer.h>
#endif

#include <Ticker.h>
#include <ArduinoJson.h>

class EventSourceMan
{
private:
#if EVTSRC_ENABLED
    WiFiClient _EventSourceClientList[EVTSRC_MAX_CLIENTS];

    void eventSourceHandler(WebServer &server);
    
#if EVTSRC_KEEPALIVE_ENABLED
    bool _needEventSourceKeepAlive = false;
    Ticker _eventSourceKeepAliveTicker;

    void eventSourceKeepAlive();
#endif
#endif

public:
#if EVTSRC_ENABLED
    void initEventSourceServer(char appIdChar, WebServer &server);
    void eventSourceBroadcast(const char *message, const char *eventType = "message");
    void eventSourceBroadcast(JsonVariantConst message, const char *eventType = "message");

#if EVTSRC_KEEPALIVE_ENABLED

    void run();
#endif
#endif
};

#endif