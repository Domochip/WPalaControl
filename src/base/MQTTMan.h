#ifndef MQTTMan_h
#define MQTTMan_h

#include "../Main.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <PubSubClient.h>
#include <ArduinoJson.h>

class MQTTMan : private PubSubClient
{
public:
    using ConnectedCallback = std::function<void(MQTTMan *mqttMan, bool firstConnection)>;
    using MessageCallback = std::function<void(char *topic, uint8_t *payload, unsigned int length)>;
    using DisconnectedCallback = std::function<void()>;

private:
    // Connection state
    enum class ConnState : uint8_t
    {
        Stopped,   // not started or disconnect() called: nothing to do
        WaitWifi,  // WiFi not connected: no attempt, connect as soon as WiFi is back
        WaitRetry, // last attempt failed (or connection lost) with WiFi connected, waiting before next attempt
        Connected  // connected to the broker
    };

    WiFiClient _wifiClient;
    char _username[32 + 1] = {0};
    char _password[64 + 1] = {0};
    char _connectedAndWillTopic[96] = {0};
    char _baseTopic[64 + 4 + 12 - 7 + sizeof(CUSTOM_APP_MODEL) + 1] = {0};
    ConnState _connState = ConnState::Stopped;
    uint32_t _connStateMillis = 0; // millis() when _connState was last changed
    bool _firstConnection = true;  // true until the first successful connection after connect(username, password)
    uint16_t _connectionCount = 0;
    int8_t _lastDiscoState = 0; // last MQTT disconnection state code (0 = no disconnection yet)

    ConnectedCallback _connectedCallBack = nullptr;
    DisconnectedCallback _disconnectedCallBack = nullptr;

    void setConnState(ConnState state);
    bool tryConnect();
    void scheduleRetry();
    MQTTMan &setBaseTopic(const char *baseTopicTemplate);
    MQTTMan &setConnectedAndWillTopic(const char *topic);
    bool connect(const char *username = nullptr, const char *password = nullptr);

public:
    MQTTMan();

    static constexpr size_t baseTopicSize = 64 + 4 + 12 - 7 + sizeof(CUSTOM_APP_MODEL) + 1; // usual baseTopic length + one of each placeholder (+4 sn) (+12 mac) (+X model)
    static void prepareTopic(const char *topicTemplate, char *result, size_t resultSize);
    const char *getBaseTopic() const;

    // (re)start the connection, to call from appInit: disconnect, then when enabled, configure and connect
    // (connection retried by loop() until disconnect() or the next begin())
    void begin(bool enabled, const char *hostname, uint16_t port, const char *username, const char *password, const char *baseTopic, uint16_t keepAlive,
               ConnectedCallback connectedCallback, MessageCallback messageCallback, DisconnectedCallback disconnectedCallback = nullptr);
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
    using PubSubClient::subscribe;
    bool loop();
};

#endif