#include "Core.h"
#include "WifiMan.h"
#include <EEPROM.h>
#include "../Main.h" //for VERSION define
#include "Version.h" //for BASE_VERSION define

#include "data/index.html.gz.h"
#include "data/pure-min.css.gz.h"
#include "data/side-menu.css.gz.h"
#include "data/side-menu.js.gz.h"

const char *Core::getSerialNumber()
{
  static char sn[9] = {0};

  // Calculate serial number only on first call
  if (sn[0] == '\0')
  {
#ifdef ESP8266
    sprintf_P(sn, PSTR("%08x"), ESP.getChipId());
#else
    sprintf_P(sn, PSTR("%08x"), (uint32_t)(ESP.getEfuseMac() << 40 >> 40));
#endif
  }

  return sn;
}

void Core::setConfigDefaultValues() {};
bool Core::parseConfigJSON(JsonVariant json, bool fromWebPage /* = false */) { return true; };
void Core::fillConfigJSON(JsonVariant json, bool forSaveFile /* = false */) {};
void Core::fillStatusJSON(JsonVariant json)
{
  unsigned long minutes = millis() / 60000;

  json[F("manufacturer")] = CUSTOM_APP_MANUFACTURER;
  json[F("model")] = CUSTOM_APP_MODEL;
  json[F("sn")] = getSerialNumber();
  json[F("baseversion")] = BASE_VERSION;
  json[F("version")] = VERSION;
  char uptime[12];
  snprintf_P(uptime, sizeof(uptime), PSTR("%dd%dh%dm"), (uint8_t)(minutes / 1440), (uint8_t)(minutes / 60 % 24), (uint8_t)(minutes % 60));
  json[F("uptime")] = uptime;
  json[F("freeheap")] = ESP.getFreeHeap();
#ifdef ESP8266
  json[F("freestack")] = ESP.getFreeContStack();
  json[F("flashsize")] = ESP.getFlashChipRealSize();

  uint32_t crashCount = CrashSaver::count();
  json[F("crashcount")] = crashCount;
#else
  json[F("freestack")] = uxTaskGetStackHighWaterMark(nullptr);
  json[F("crashcount")] = 0;
#endif
}
bool Core::appInit(bool reInit /* = false */)
{
  return true;
};
void Core::appInitWebServer(WebServer &server)
{
  // root is index
  server.on("/", HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.send_P(200, PSTR("text/html"), indexhtmlgz, sizeof(indexhtmlgz));
            });

  // favicon.ico - return 204 so browsers don't retry on every load
  server.on(F("/favicon.ico"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Cache-Control"), F("max-age=604800, public"));
              server.send(204);
            });

  // HEAD / is used to poll device availability
  server.on("/", HTTP_HEAD,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.send(200);
            });

  // Ressources URLs
  server.on(F("/pure-min.css"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.sendHeader(F("Cache-Control"), F("max-age=604800, public"));
              server.send_P(200, PSTR("text/css"), puremincssgz, sizeof(puremincssgz));
            });

  server.on(F("/side-menu.css"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.sendHeader(F("Cache-Control"), F("max-age=604800, public"));
              server.send_P(200, PSTR("text/css"), sidemenucssgz, sizeof(sidemenucssgz));
            });

  server.on(F("/side-menu.js"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.sendHeader(F("Cache-Control"), F("max-age=604800, public"));
              server.send_P(200, PSTR("text/javascript"), sidemenujsgz, sizeof(sidemenujsgz));
            });

  // Get Latest Update Info ---------------------------------------------------------
  server.on(
      F("/glui"), HTTP_GET,
      [this, &server]()
      {
        SERVER_KEEPALIVE_FALSE()
        JsonDocument json;
        fillLatestUpdateInfoJson(json, true);
        server.setContentLength(measureJson(json));
        server.send(200, F("application/json"), "");
        WiFiClient client = server.client();
        serializeJson(json, client);
      });

  // Update Firmware from Github ----------------------------------------------
  server.on(
      F("/update"), HTTP_POST,
      [this, &server]()
      {
        String msg;

        // start chunked response
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, F("text/plain"), "");

        // Define the progress callback function
        std::function<void(size_t, size_t)> progressCallback = [&server](size_t progress, size_t total)
        {
          uint8_t percent = (progress * 100) / total;
          LOG_SERIAL_PRINTF_P(PSTR("Progress: %d%%\n"), percent);
          char pct[10];
          snprintf_P(pct, sizeof(pct), PSTR("p:%d\n"), percent);
          server.sendContent(pct);
        };

        // Call the updateFirmware function with the progress callback
        SystemState::shouldReboot = updateFirmware(server.arg(F("plain")).c_str(), msg, progressCallback);
        if (SystemState::shouldReboot)
          server.sendContent(F("s:true\n"));
        else
        {
          server.sendContent(F("s:false\nm:"));
          server.sendContent(msg);
          server.sendContent("\n");
        }

        // finalize chunked response
        server.sendContent(emptyString);
      });

  // Firmware POST URL allows to push new firmware ----------------------------
  server.on(
      F("/fw"), HTTP_POST,
      [&server]()
      {
        SystemState::shouldReboot = !Update.hasError();

        String msg;

        if (SystemState::shouldReboot)
          msg = F("Update successful");
        else
        {
          msg = F("Update failed: ");
#ifdef ESP8266
          msg += Update.getErrorString();
#else
          msg += Update.errorString();
#endif
          Update.clearError();
          // Update failed so restart to Run custom Application in loop
          SystemState::pauseCustomApp = false;
        }

        LOG_SERIAL_PRINTLN(msg);

        SERVER_KEEPALIVE_FALSE()
        server.send(SystemState::shouldReboot ? 200 : 500, F("text/html"), msg);
      },
      [&server]()
      {
        HTTPUpload &upload = server.upload();

        if (upload.status == UPLOAD_FILE_START)
        {
          // stop to Run custom Application in loop
          SystemState::pauseCustomApp = true;

          LOG_SERIAL_PRINTF_P(PSTR("Update Start: %s\n"), upload.filename.c_str());

#ifdef ESP8266
          Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
#else
          Update.begin();
#endif
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
          Update.write(upload.buf, upload.currentSize);
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
          Update.end(true);
        }

#ifdef ESP8266
        // Feed the dog otherwise big firmware won't pass
        ESP.wdtFeed();
#endif
        yield();
      });

  // reboot POST --------------------------------------------------------------
  server.on(F("/rbt"), HTTP_POST,
            [&server]()
            {
              if (server.hasArg(F("rescue")))
              {
                // Set EEPROM for Rescue mode flag
                EEPROM.begin(4);
                EEPROM.write(0, 1);
                EEPROM.end();
              }
              SERVER_KEEPALIVE_FALSE()
              server.send_P(200, PSTR("text/html"), PSTR("Reboot command received"));
              SystemState::shouldReboot = true;
            });

#ifdef ESP8266
  // Download all crash logs as text attachment -------------------------------
  server.on(F("/crashdl"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.setContentLength(CONTENT_LENGTH_UNKNOWN);
              server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"crashes.txt\""));
              server.send(200, F("text/plain"), "");
              CrashSaver::iterateCrashLogFiles([&server](uint32_t, const char *fileName)
                                               {
                File f = LittleFS.open(fileName, "r");
                if (f) {
                  server.sendContent(F("--- "));
                  server.sendContent(fileName);
                  server.sendContent(F(" ---\n"));
                  server.sendContent(f.readString());
                  f.close();
                } });
              server.sendContent(emptyString);
            });

  // Clear all crash logs -----------------------------------------------------
  server.on(F("/crashclr"), HTTP_POST,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              CrashSaver::clearAllLogs();
              server.send_P(200, PSTR("text/plain"), PSTR("OK"));
            });

#if DEVELOPPER_MODE
  // dbz endpoint try to do a division by 0 to trigger a crash for testing purposes
  server.on(F("/dbz"), HTTP_GET,
            [&server]()
            {
              SERVER_KEEPALIVE_FALSE()
              volatile int a = 1;
              volatile int b = 0;
              volatile int c = a / b;
              (void)c; // avoid unused variable warning
              server.send_P(200, PSTR("text/html"), PSTR("This should never be seen"));
            });
#endif
#endif

  // 302 on not found ---------------------------------------------------------
  server.onNotFound(
      [&server]()
      {
        // redirect to my IP receiving the request
        SERVER_KEEPALIVE_FALSE()
        char redirectUrl[32];
        IPAddress ip = server.client().localIP();
        snprintf_P(redirectUrl, sizeof(redirectUrl), PSTR("http://%s"), WifiMan::ipToCString(ip));
        server.sendHeader(F("Location"), redirectUrl, true);
        server.send(302, F("text/plain"), ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
        server.client().stop();
      });
}
void Core::mqttPublishHassDiscovery(HassDiscoveryCtx &ctx)
{
  JsonDocument json;

  //
  // Connectivity entity
  //

  // prepare payload for connectivity sensor
  deserializeJson(json, F("{"
                          "\"default_entity_id\":\"binary_sensor." CUSTOM_APP_MODEL "_connectivity\","
                          "\"device_class\":\"connectivity\","
                          "\"entity_category\":\"diagnostic\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "_connectivity\","
                          "\"state_topic\":\"~/connected\","
                          "\"value_template\": \"{{ iif(int(value) > 0, 'ON', 'OFF') }}\""
                          "}"));
  ctx.publishEntity(json, F("binary_sensor"), F("_Connectivity"), false);

  //
  // Update entity
  //

  // prepare payload for update sensor
  deserializeJson(json, F("{"
                          "\"command_topic\":\"~/update/install\","
                          "\"default_entity_id\":\"update." CUSTOM_APP_MODEL "\","
                          "\"device_class\":\"firmware\","
                          "\"entity_category\":\"config\","
                          "\"object_id\":\"" CUSTOM_APP_MODEL "\","
                          "\"payload_install\":\"latest\","
                          "\"state_topic\":\"~/update\""
                          "}"));
  ctx.publishEntity(json, F("update"), F("_Update"));
}