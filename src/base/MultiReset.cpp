#include "MultiReset.h"
#include "../Main.h"

#ifndef MULTI_RESET_COUNT
#define MULTI_RESET_COUNT 3
#endif

#ifndef MULTI_RESET_WINDOW
#define MULTI_RESET_WINDOW 5
#endif

bool MultiReset::_pending = false;

#if MULTI_RESET_COUNT > 0

#ifdef ESP8266

#include <user_interface.h>

// last 8 bytes of RTC user memory (the first 128 bytes are used by eboot for OTA)
#define MULTI_RESET_RTC_OFFSET 126
#define MULTI_RESET_MAGIC 0x4D525354UL

static uint8_t readCounter()
{
  uint32_t data[2];
  if (!ESP.rtcUserMemoryRead(MULTI_RESET_RTC_OFFSET, data, sizeof(data)) || data[0] != MULTI_RESET_MAGIC || data[1] > MULTI_RESET_COUNT)
    return 0;
  return data[1];
}

static void writeCounter(uint8_t count)
{
  uint32_t data[2] = {MULTI_RESET_MAGIC, count};
  ESP.rtcUserMemoryWrite(MULTI_RESET_RTC_OFFSET, data, sizeof(data));
}

static bool isResetPress()
{
  return ESP.getResetInfoPtr()->reason == REASON_EXT_SYS_RST;
}

#else // ESP32

#include <EEPROM.h>
#include <esp_system.h>

// EN button acts as a power-on reset (RTC memory lost): keep the counter in EEPROM
#define MULTI_RESET_EEPROM_ADDR 0

static uint8_t readCounter()
{
  EEPROM.begin(4);
  uint8_t count = EEPROM.read(MULTI_RESET_EEPROM_ADDR);
  EEPROM.end();
  return count > MULTI_RESET_COUNT ? 0 : count;
}

static void writeCounter(uint8_t count)
{
  EEPROM.begin(4);
  EEPROM.write(MULTI_RESET_EEPROM_ADDR, count);
  EEPROM.end();
}

static bool isResetPress()
{
  esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_POWERON || reason == ESP_RST_EXT;
}

#endif

bool MultiReset::check()
{
  uint8_t count = isResetPress() ? readCounter() + 1 : 0;

  if (count >= MULTI_RESET_COUNT)
  {
    writeCounter(0);
    LOG_SERIAL_PRINTF_P(PSTR("Reset pressed %d times -> Recovery Mode requested\n"), count);
    return true;
  }

  writeCounter(count);
  if (count)
  {
    _pending = true;
    LOG_SERIAL_PRINTF_P(PSTR("Reset pressed %d/%d times (press again within %ds for Recovery Mode)\n"), count, MULTI_RESET_COUNT, MULTI_RESET_WINDOW);
  }
  return false;
}

void MultiReset::run()
{
  if (_pending && millis() >= MULTI_RESET_WINDOW * 1000UL)
  {
    _pending = false;
    writeCounter(0);
  }
}

#else

bool MultiReset::check() { return false; }
void MultiReset::run() {}

#endif
