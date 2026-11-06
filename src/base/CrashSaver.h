/*

Inspired by https://github.com/brainelectronics/EspSaveCrashSpiffs

*/

#ifndef CrashSaver_h
#define CrashSaver_h

#ifdef ESP8266

#include <FS.h>
#include <functional>
#include <user_interface.h>

#include "SystemState.h"
#include "TimeManager.h"

class CrashSaver
{
public:
    static constexpr const char *DEFAULT_DIR = "/crash/";
    static constexpr unsigned LOG_FILE_PATH_LEN = 13; // "/crash/" + "65535" + '\0'
    static FS *_fs;

    CrashSaver() = delete;

    static const char *getNextLogFilePath() { return _nextLogFilePath; }
    static void init(FS &fs);

    static uint16_t count();
    static void iterateCrashLogFiles(std::function<void(uint16_t, const char *)> callback);
    static bool clearAllLogs();

private:
    static void calculateNextLogFilePath();
    static char _nextLogFilePath[LOG_FILE_PATH_LEN];
};

#endif // ESP8266
#endif // CrashSaver_h
