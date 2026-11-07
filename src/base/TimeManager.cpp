#include "TimeManager.h"

uint32_t TimeManager::_ntpEpoch = 0;
uint32_t TimeManager::_ntpMillis = 0;

void TimeManager::init(const char *ntpServer)
{
#ifdef ESP8266
    sntp_stop();
    sntp_setservername(0, ntpServer);
    sntp_init();
#else
    esp_sntp_stop();
    esp_sntp_setservername(0, ntpServer);
    esp_sntp_init();
#endif

#ifdef ESP8266
    settimeofday_cb([]()
#else
    sntp_set_time_sync_notification_cb([](struct timeval *)
#endif
                    {
        time_t t = time(nullptr);
        if (t > 1000000000UL)
        {
            _ntpEpoch = (uint32_t)t;
            _ntpMillis = millis();
        } });
}

void TimeManager::setTimezone(const char *tzString)
{
    if (!tzString)
        return;

    setenv("TZ", tzString, 1);
    tzset();
}

int TimeManager::formatTime(bool utc, char *buf, size_t bufSize)
{
    if (_ntpEpoch == 0)
        return 0;

    time_t t = (time_t)(_ntpEpoch + (millis() - _ntpMillis) / 1000);
    struct tm tmInfo;
    if (utc)
        gmtime_r(&t, &tmInfo);
    else
        localtime_r(&t, &tmInfo);
    return strftime(buf, bufSize, utc ? "%Y-%m-%d %H:%M:%S UTC" : "%Y-%m-%d %H:%M:%S %Z", &tmInfo);
}

int TimeManager::getUTCTime(char *buf, size_t bufSize)
{
    return formatTime(true, buf, bufSize);
}

int TimeManager::getLocalTime(char *buf, size_t bufSize)
{
    return formatTime(false, buf, bufSize);
}
