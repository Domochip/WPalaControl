#include "TimeManager.h"

#ifdef ESP8266
#include <user_interface.h>
#else
#include <esp_sntp.h>
#endif

uint32_t TimeManager::_ntpEpoch = 0;
uint32_t TimeManager::_ntpMillis = 0;

void TimeManager::init(const char *ntpServer)
{
    sntp_stop();
    sntp_setservername(0, ntpServer);
    sntp_init();

    settimeofday_cb([]()
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
    if (tzString)
        setTZ(tzString);
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
