#ifndef TimeManager_h
#define TimeManager_h

#include <stdint.h>
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#ifdef ESP8266
#include <sntp.h>
#else
#include <esp_sntp.h>
#endif

class TimeManager
{
public:
    TimeManager() = delete;

    // Initialize time synchronization with NTP server
    // Timezone should be set separately via setTimezone() before calling init()
    static void init(const char *ntpServer = "pool.ntp.org");

    // Set timezone (POSIX TZ string, e.g., "UTC0", "EST5EDT,M3.2.0,M11.1.0")
    // Can be called at any time, typically during system initialization
    static void setTimezone(const char *tzString);

    // Check if NTP time has been synchronized
    static bool isNtpSynchronized() { return _ntpEpoch != 0; }

    // Get current time formatted as local datetime string (buf must be at least 24 chars)
    // Format: "YYYY-MM-DD HH:MM:SS TZ"
    // Returns number of characters written (not including null terminator), or 0 if NTP not synchronized
    static int getLocalTime(char *buf, size_t bufSize);

    // Get current time formatted as UTC datetime string (buf must be at least 24 chars)
    // Format: "YYYY-MM-DD HH:MM:SS UTC"
    // Returns number of characters written (not including null terminator), or 0 if NTP not synchronized
    static int getUTCTime(char *buf, size_t bufSize);

private:
    static int formatTime(bool utc, char *buf, size_t bufSize);

    static uint32_t _ntpEpoch;
    static uint32_t _ntpMillis;

    friend class CrashSaver;
};

#endif // TimeManager_h
