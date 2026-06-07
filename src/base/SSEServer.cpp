#include "SSEServer.h"

#if SSE_SERVER_ENABLED

void SSEServer::handleSubscription(WebServer &server)
{
    uint8_t subPos = 0;

    // Find the subscription for this client
    while (subPos < SSE_SERVER_MAX_CLIENTS &&
           (!_clients[subPos] ||
            _clients[subPos].remoteIP() != server.client().remoteIP() ||
            _clients[subPos].remotePort() != server.client().remotePort()))
        subPos++;

    // If no subscription found
    if (subPos == SSE_SERVER_MAX_CLIENTS)
    {
        subPos = 0;
        // Find a free slot
        while (subPos < SSE_SERVER_MAX_CLIENTS && _clients[subPos])
            subPos++;

        // If there is no more slot available
        if (subPos == SSE_SERVER_MAX_CLIENTS)
            return server.send(500);
    }

    // create/update subscription
    _clients[subPos] = server.client();
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); // the payload can go on forever
    server.sendContent_P(PSTR("HTTP/1.1 200 OK\nContent-Type: text/event-stream;\nConnection: keep-alive\nCache-Control: no-cache\nAccess-Control-Allow-Origin: *\n\n"));

#if DEVELOPPER_MODE
    LOG_SERIAL_PRINTF_P(PSTR("statusSSEHandler - client #%d (%s:%d) registered\n"), subPos, server.client().remoteIP().toString().c_str(), server.client().remotePort());
#endif
}

void SSEServer::forEach(std::function<void(WiFiClient &, uint8_t)> action)
{
    for (uint8_t i = 0; i < SSE_SERVER_MAX_CLIENTS; i++)
    {
        if (_clients[i].connected())
            action(_clients[i], i);
        else if (_clients[i])
            _clients[i].stop(); // Client disconnected — release slot so it can be reused
    }
}

#if SSE_SERVER_KEEPALIVE
void SSEServer::sendKeepAlive()
{
    forEach([](WiFiClient &client, uint8_t i)
            {
                client.println(F(":keepalive\n\n"));

#if DEVELOPPER_MODE
                LOG_SERIAL_PRINTF_P(PSTR("statusSSEKeepAlive - keep-alive sent to client #%d (%s:%d)\n"), i, client.remoteIP().toString().c_str(), client.remotePort());
#endif
            });
}
#endif

void SSEServer::init(char appIdChar, WebServer &server)
{
    String url(F("/statusEvt"));
    url += appIdChar;
    // register SSE Uri
    server.on(url, HTTP_GET, [this, &server]()
              { handleSubscription(server); });
#if SSE_SERVER_KEEPALIVE
    // send keep alive event every 60 seconds
#ifdef ESP8266
    _keepAliveTicker.attach(60, [this]()
                            { _needKeepAlive = true; });
#else
    _keepAliveTicker.attach<SSEServer *>(60, [](SSEServer *sse)
                                         { sse->_needKeepAlive = true; }, this);
#endif
#endif
}

void SSEServer::broadcast(const char *message, const char *eventType /* = "message" */)
{
    forEach([message, eventType](WiFiClient &client, uint8_t i)
            {
                client.printf_P(PSTR("event: %s\ndata: %s\n\n"), eventType, message);

#if DEVELOPPER_MODE
                LOG_SERIAL_PRINTF_P(PSTR("statusSSEBroadcast - event sent to client #%d\n"), i);
#endif
            });
}

void SSEServer::broadcast(JsonVariantConst message, const char *eventType /* = "message" */)
{
    forEach([message, eventType](WiFiClient &client, uint8_t i)
            {
                client.printf_P(PSTR("event: %s\ndata: "), eventType);
                serializeJson(message, client);
                client.print(F("\n\n"));

#if DEVELOPPER_MODE
                LOG_SERIAL_PRINTF_P(PSTR("statusSSEBroadcast - event sent to client #%d\n"), i);
#endif
            });
}

#if SSE_SERVER_KEEPALIVE
void SSEServer::loop()
{
    if (_needKeepAlive)
    {
        _needKeepAlive = false;
        sendKeepAlive();
    }
}
#endif // SSE_SERVER_KEEPALIVE

#endif // SSE_SERVER_ENABLED