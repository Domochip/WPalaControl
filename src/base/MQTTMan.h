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

class MQTTMan : private PubSubClient
{
public:
    using ConnectedCallback = std::function<void(MQTTMan * mqttMan, bool firstConnection)>;
    using DisconnectedCallback = std::function<void()>;

private:
    char _username[64] = {0};
    char _password[64] = {0};
    char _connectedAndWillTopic[96] = {0};
    char _baseTopic[64 + 4 + 12 - 7 + sizeof(CUSTOM_APP_MODEL) + 1] = {0};
    bool _needMqttReconnect = false;
    Ticker _mqttReconnectTicker;
    uint16_t _connectionCount = 0;
    int8_t _lastDiscoState = 0;   // last MQTT disconnection state code (0 = no disconnection yet)

    ConnectedCallback _connectedCallBack = nullptr;
    DisconnectedCallback _disconnectedCallBack = nullptr;

    bool connect(bool firstConnection);

public:
    static constexpr size_t baseTopicSize = 64 + 4 + 12 - 7 + sizeof(CUSTOM_APP_MODEL) + 1; // usual baseTopic length + one of each placeholder (+4 sn) (+12 mac) (+X model)
    static void prepareTopic(const char *topicTemplate, char *result, size_t resultSize);
    MQTTMan &setBaseTopic(const char *baseTopicTemplate);
    const char *getBaseTopic() const;

    using PubSubClient::setClient;
    using PubSubClient::setServer;
    MQTTMan &setConnectedAndWillTopic(const char *topic);
    MQTTMan &setConnectedCallback(ConnectedCallback connectedCallback);
    MQTTMan &setDisconnectedCallback(DisconnectedCallback disconnectedCallback);
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
    uint16_t getConnectionCount() const;
    int8_t getLastDiscoState() const;
    using PubSubClient::getBufferSize;
    using PubSubClient::setBufferSize;
    using PubSubClient::subscribe;
    bool loop();
};

#endif