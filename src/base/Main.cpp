#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <LittleFS.h>
#include <FS.h>

#include "Version.h"
#include "../Main.h"
#include "SystemState.h"
#include "Application.h"
#include "Core.h"
#include "WifiMan.h"
#include "MultiReset.h"

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

  LOG_SERIAL_PRINTLN(F(CUSTOM_APP_MANUFACTURER " " CUSTOM_APP_MODEL " " VERSION));
  LOG_SERIAL_PRINTLN(F("---Booting---"));

  bool skipExistingConfig = MultiReset::check();

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

#ifdef ESP8266
  // Init CrashSaver
  CrashSaver::init(LittleFS);
#endif

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

  core.run();

  MultiReset::run();

  if (!SystemState::pauseCustomApp && !SystemState::shouldReboot)
    custom.run();

  wifiMan.run();

  // required to maintain WiFi and send tcp packets (particularly for mesh wifi)
#ifdef ESP8266
  delay(10);
#endif

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
