#ifndef MQTTMan_h
#define MQTTMan_h

#include "../Main.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <Ticker.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define CONNECTED_CALLBACK_SIGNATURE std::function<void(MQTTMan * mqttMan, bool firstConnection)>
#define DISCONNECTED_CALLBACK_SIGNATURE std::function<void()>

class MQTTMan : private PubSubClient
{
private:
    char _username[64] = {0};
    char _password[64] = {0};
    char _connectedAndWillTopic[96] = {0};
    bool _needMqttReconnect = false;
    Ticker _mqttReconnectTicker;

    CONNECTED_CALLBACK_SIGNATURE _connectedCallBack = nullptr;
    DISCONNECTED_CALLBACK_SIGNATURE _disconnectedCallBack = nullptr;

    bool connect(bool firstConnection);

public:
    static void prepareTopic(const char *topic, char *result, size_t resultSize);

    using PubSubClient::setClient;
    using PubSubClient::setServer;
    MQTTMan &setConnectedAndWillTopic(const char *topic);
    MQTTMan &setConnectedCallback(CONNECTED_CALLBACK_SIGNATURE connectedCallback);
    MQTTMan &setDisconnectedCallback(DISCONNECTED_CALLBACK_SIGNATURE disconnectedCallback);
    using PubSubClient::setCallback;
    bool connect(const char *username = nullptr, const char *password = nullptr);
    using PubSubClient::connected;
    void disconnect();
    using PubSubClient::beginPublish;
    using PubSubClient::endPublish;
    using PubSubClient::publish;
    bool publish(const char *topic, JsonVariantConst payload, bool retained = false);
    bool publishToConnectedTopic(const char *payload);
    using PubSubClient::publish_P;
    using PubSubClient::state;
    const __FlashStringHelper *getStateString();
    using PubSubClient::getBufferSize;
    using PubSubClient::setBufferSize;
    using PubSubClient::subscribe;
    bool loop();
};

#endif