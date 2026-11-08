#include "MQTTMan.h"
#include "WifiMan.h"
#include "Core.h"

void MQTTMan::prepareTopic(const char *topicTemplate, char *result, size_t resultSize)
{
    if (!result || resultSize == 0)
        return;

    result[0] = '\0';
    if (!topicTemplate || resultSize == 1)
        return;

    const char *src = topicTemplate;
    char *dst = result;
    char *end = result + resultSize - 1; // reserve 1 byte: '\0'
    bool overflow = false;

    const char *sn = Core::getSerialNumber();

    const char *mac = WifiMan::getMacAddress();

    const char *model = CUSTOM_APP_MODEL;

    while (*src && dst < end)
    {
        const char *replacement = nullptr;
        size_t replacementLen = 0;

        if (src[0] == '$' && src[1] == 's' && src[2] == 'n' && src[3] == '$')
        {
            replacement = sn;
            replacementLen = strlen(sn);
            src += 4;
        }
        else if (src[0] == '$' && src[1] == 'm' && src[2] == 'a' && src[3] == 'c' && src[4] == '$')
        {
            replacement = mac;
            replacementLen = strlen(mac);
            src += 5;
        }
        else if (src[0] == '$' && src[1] == 'm' && src[2] == 'o' && src[3] == 'd' && src[4] == 'e' && src[5] == 'l' && src[6] == '$')
        {
            replacement = model;
            replacementLen = strlen(model);
            src += 7;
        }
        else
        {
            *dst++ = *src++;
            continue;
        }

        if (dst + replacementLen > end)
        {
            overflow = true;
            replacementLen = end - dst;
        }

        memcpy(dst, replacement, replacementLen);
        dst += replacementLen;
    }

    if (*src)
        overflow = true;

    if (dst > result && *(dst - 1) == '/')
        --dst;
    *dst = '\0';

    if (overflow)
        LOG_SERIAL_PRINTLN(F("/!\\MQTT prepareTopic overflow/!\\"));
}

MQTTMan &MQTTMan::setBaseTopic(const char *baseTopicTemplate)
{
    // prepare base topic with replacements
    prepareTopic(baseTopicTemplate, _baseTopic, sizeof(_baseTopic));

    // build connected/will topic
    char willTopic[sizeof(_baseTopic) + sizeof("/connected")];
    snprintf_P(willTopic, sizeof(willTopic), PSTR("%s/connected"), _baseTopic);
    setConnectedAndWillTopic(willTopic);

    return *this;
}

const char *MQTTMan::getBaseTopic() const
{
    return _baseTopic;
}

void MQTTMan::setConnState(ConnState state)
{
    _connState = state;
    _connStateMillis = millis();
}

// Single (blocking) connection attempt
// on success: Connected state + connected callback; on failure: WaitRetry state
bool MQTTMan::tryConnect()
{
    // generate clientID
    char clientID[sizeof(CUSTOM_APP_MODEL) + 9];
    snprintf_P(clientID, sizeof(clientID), PSTR(CUSTOM_APP_MODEL "%s"), Core::getSerialNumber());

    // Connect
    char *username = (_username[0] ? _username : nullptr);
    char *password = (_username[0] ? _password : nullptr);
    char *willTopic = (_connectedAndWillTopic[0] ? _connectedAndWillTopic : nullptr);
    const char *willMessage = (_connectedAndWillTopic[0] ? "0" : nullptr);
    PubSubClient::connect(clientID, username, password, willTopic, 0, true, willMessage);

    if (!connected())
    {
        _lastDiscoState = state();
        LOG_SERIAL_PRINT(F("MQTT connection failed (reason: "));
        LOG_SERIAL_PRINT(getStateString());
        LOG_SERIAL_PRINT(')');
        scheduleRetry();
        return false;
    }

    LOG_SERIAL_PRINTLN(F("MQTT connected"));

    setConnState(ConnState::Connected);

    if (_connectedAndWillTopic[0])
        publish(_connectedAndWillTopic, "1", true);

    _connectionCount++;

    // clear flag before the callback (it may call disconnect()/connect())
    bool firstConnection = _firstConnection;
    _firstConnection = false;

    // Subscribe to needed topic
    if (_connectedCallBack)
        _connectedCallBack(this, firstConnection);

    return connected();
}

// Schedule next attempt: after 20 sec if WiFi is connected, otherwise as soon as WiFi is back
// (completes the log line started by the caller with the failure/disconnection reason)
void MQTTMan::scheduleRetry()
{
    if (WiFi.isConnected())
    {
        LOG_SERIAL_PRINTLN(F(", next attempt in 20s"));
        setConnState(ConnState::WaitRetry);
    }
    else
    {
        LOG_SERIAL_PRINTLN(F(", waiting for WiFi"));
        setConnState(ConnState::WaitWifi);
    }
}

MQTTMan &MQTTMan::setConnectedAndWillTopic(const char *topic)
{
    if (!topic)
        _connectedAndWillTopic[0] = 0;
    else if (strlen(topic) < sizeof(_connectedAndWillTopic))
        strcpy(_connectedAndWillTopic, topic);

    return *this;
}

MQTTMan &MQTTMan::setConnectedCallback(ConnectedCallback connectedCallback)
{
    _connectedCallBack = connectedCallback;
    return *this;
}

MQTTMan &MQTTMan::setDisconnectedCallback(DisconnectedCallback disconnectedCallback)
{
    _disconnectedCallBack = disconnectedCallback;
    return *this;
}

bool MQTTMan::connect(const char *username, const char *password /* = nullptr */)
{
    // check logins
    if (username && strlen(username) >= sizeof(_username))
        return false;
    if (password && strlen(password) >= sizeof(_password))
        return false;

    if (username)
        strcpy(_username, username);
    else
        _username[0] = 0;

    if (password)
        strcpy(_password, password);
    else
        _password[0] = 0;

    _firstConnection = true;

    // no WiFi: don't try now, loop() will connect as soon as WiFi is back
    if (!WiFi.isConnected())
    {
        setConnState(ConnState::WaitWifi);
        return false;
    }

    return tryConnect();
}

void MQTTMan::disconnect()
{
    // Stop MQTT Reconnect
    setConnState(ConnState::Stopped);

    // Disconnect
    if (connected()) // Issue #598 : disconnect() crash if client not yet set
    {
        // publish disconnected just before disconnect...
        if (_connectedAndWillTopic[0])
            publish(_connectedAndWillTopic, "0", true);

        PubSubClient::disconnect();
        // call disconnected callback if set
        if (_disconnectedCallBack)
            _disconnectedCallBack();
    }
}

bool MQTTMan::publishToConnectedTopic(const char *payload)
{
    if (_connectedAndWillTopic[0])
        return publish(_connectedAndWillTopic, payload, true);
    return false;
}

bool MQTTMan::publish(const char *topic, JsonVariantConst payload, bool retained /* = false */)
{
    const size_t payloadLen = measureJson(payload);
    if (!beginPublish(topic, payloadLen, retained))
        return false;

    struct MQTTWriter
    {
        MQTTMan &mqtt;

        size_t write(uint8_t c) { return mqtt.write(c); }
        size_t write(const uint8_t *buffer, size_t size) { return mqtt.write(buffer, size); }
    } writer{*this};

    const size_t written = serializeJson(payload, writer);
    if (written != payloadLen)
        return false;

    return endPublish();
}

const __FlashStringHelper *MQTTMan::getStateString()
{
    switch (state())
    {
    case MQTT_CONNECTION_TIMEOUT:
        return F("Timed Out");
    case MQTT_CONNECTION_LOST:
        return F("Lost");
    case MQTT_CONNECT_FAILED:
        return F("Failed");
    case MQTT_DISCONNECTED:
        return F("Disconnected");
    case MQTT_CONNECTED:
        return F("Connected");
    case MQTT_CONNECT_BAD_PROTOCOL:
        return F("Bad Protocol Version");
    case MQTT_CONNECT_BAD_CLIENT_ID:
        return F("Incorrect ClientID");
    case MQTT_CONNECT_UNAVAILABLE:
        return F("Server Unavailable");
    case MQTT_CONNECT_BAD_CREDENTIALS:
        return F("Bad Credentials");
    case MQTT_CONNECT_UNAUTHORIZED:
        return F("Unauthorized Connection");
    default:
        return F("Unknown");
    }
}

uint16_t MQTTMan::getConnectionCount() const
{
    return _connectionCount;
}

int8_t MQTTMan::getLastDiscoState() const
{
    return _lastDiscoState;
}

bool MQTTMan::loop()
{
    switch (_connState)
    {
    case ConnState::Connected:
        if (connected())
            return PubSubClient::loop();

        // connection lost
        _lastDiscoState = state();
        LOG_SERIAL_PRINT(F("MQTT disconnected (reason: "));
        LOG_SERIAL_PRINT(getStateString());
        LOG_SERIAL_PRINT(')');

        scheduleRetry();

        if (_disconnectedCallBack)
            _disconnectedCallBack();
        break;

    case ConnState::WaitWifi:
        if (WiFi.isConnected())
            tryConnect();
        break;

    case ConnState::WaitRetry:
        if (!WiFi.isConnected())
            setConnState(ConnState::WaitWifi); // WiFi lost while waiting: no retry until it is back
        else if (millis() - _connStateMillis >= 20000UL)
            tryConnect();
        break;

    default: // Stopped
        return true;
    }

    return connected();
}
