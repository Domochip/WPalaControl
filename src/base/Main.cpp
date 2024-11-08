#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <EEPROM.h>
#include <LittleFS.h>
#include <FS.h>

#include "Version.h"
#include "../Main.h"
#include "SystemState.h"
#include "Application.h"
#include "Core.h"
#include "WifiMan.h"

// include Custom Application header file
#include CUSTOM_APP_HEADER

// WebServer
WebServer server(80);

// Core App
Core core;

// WifiMan App
WifiMan wifiMan;

// Custom App
CUSTOM_APP_CLASS custom;

//-----------------------------------------------------------------------
// Setup function
//-----------------------------------------------------------------------
void setup()
{
#ifdef LOG_SERIAL
  LOG_SERIAL.begin(LOG_SERIAL_SPEED);
  LOG_SERIAL_PRINTLN();
  LOG_SERIAL_PRINTLN();
  LOG_SERIAL.flush();
#endif

#ifdef STATUS_LED_SETUP
  STATUS_LED_SETUP
#endif
#ifdef STATUS_LED_ERROR
  STATUS_LED_ERROR
#endif

  LOG_SERIAL_PRINTLN(F(CUSTOM_APP_MANUFACTURER " " CUSTOM_APP_MODEL " " VERSION));
  LOG_SERIAL_PRINTLN(F("---Booting---"));

#ifndef RESCUE_BUTTON_WAIT
#define RESCUE_BUTTON_WAIT 3
#endif

  bool skipExistingConfig = false;

  // look into EEPROM for Rescue mode flag
  EEPROM.begin(4);
  skipExistingConfig = EEPROM.read(0) != 0;
  if (skipExistingConfig)
    EEPROM.write(0, 0);
  EEPROM.end();

#ifdef RESCUE_BTN_PIN
  // if config already skipped, don't wait for rescue button
  if (!skipExistingConfig)
  {
    LOG_SERIAL_PRINTF_P(PSTR("Wait Rescue button for %d seconds\n"), RESCUE_BUTTON_WAIT);

    pinMode(RESCUE_BTN_PIN, (RESCUE_BTN_PIN != 16) ? INPUT_PULLUP : INPUT);
    for (int i = 0; i < 100 && skipExistingConfig == false; i++)
    {
      if (digitalRead(RESCUE_BTN_PIN) == LOW)
        skipExistingConfig = true;
      delay(RESCUE_BUTTON_WAIT * 10);
    }
  }
#endif

  if (skipExistingConfig)
  {
    LOG_SERIAL_PRINTLN(F("-> RESCUE MODE : Stored configuration won't be loaded."));
  }
#ifdef ESP8266
  if (!LittleFS.begin())
#else
  if (!LittleFS.begin(true))
#endif
  {
    LOG_SERIAL_PRINTLN(F("/!\\   File System Mount Failed   /!\\"));
    LOG_SERIAL_PRINTLN(F("/!\\ Configuration can't be saved /!\\"));
  }

  // Init Core
  core.init(skipExistingConfig);

  // Init WiFi
  wifiMan.init(skipExistingConfig);

  // Init Custom Application
  custom.init(skipExistingConfig);

  LOG_SERIAL_PRINT(F("Start WebServer : "));

  core.initWebServer(server);
  wifiMan.initWebServer(server);
  custom.initWebServer(server);

  server.begin();

  LOG_SERIAL_PRINTLN(F("OK"));
  LOG_SERIAL_PRINTLN(F("---End of setup()---"));
}

//-----------------------------------------------------------------------
// Main Loop function
//-----------------------------------------------------------------------
void loop(void)
{

  // Handle WebServer
  server.handleClient();

  if (!SystemState::pauseCustomApp && !SystemState::shouldReboot)
    custom.run();

  wifiMan.run();

  if (SystemState::shouldReboot)
  {
#ifdef LOG_SERIAL
    LOG_SERIAL_PRINTLN(F("Rebooting..."));
    delay(100);
    LOG_SERIAL.end();
#endif
    ESP.restart();
  }
}
