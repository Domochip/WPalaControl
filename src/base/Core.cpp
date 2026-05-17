#include "Core.h"
#include <EEPROM.h>
#include "../Main.h" //for VERSION define
#include "Version.h" //for BASE_VERSION define

#include "data/index.html.gz.h"
#include "data/pure-min.css.gz.h"
#include "data/side-menu.css.gz.h"
#include "data/side-menu.js.gz.h"

void Core::setConfigDefaultValues() {};
bool Core::parseConfigJSON(JsonDocument &doc, bool fromWebPage = false) { return true; };
void Core::fillConfigJSON(JsonDocument &doc, bool forSaveFile) {};
void Core::fillStatusJSON(JsonDocument &doc)
{
  char sn[9];
#ifdef ESP8266
  sprintf_P(sn, PSTR("%08x"), ESP.getChipId());
#else
  sprintf_P(sn, PSTR("%08x"), (uint32_t)(ESP.getEfuseMac() << 40 >> 40));
#endif
  unsigned long minutes = millis() / 60000;

  doc[F("manufacturer")] = CUSTOM_APP_MANUFACTURER;
  doc[F("model")] = CUSTOM_APP_MODEL;
  doc[F("sn")] = sn;
  doc[F("baseversion")] = BASE_VERSION;
  doc[F("version")] = VERSION;
  char uptime[12];
  snprintf_P(uptime, sizeof(uptime), PSTR("%dd%dh%dm"), (byte)(minutes / 1440), (byte)(minutes / 60 % 24), (byte)(minutes % 60));
  doc[F("uptime")] = uptime;
  doc[F("freeheap")] = ESP.getFreeHeap();
#ifdef ESP8266
  doc[F("freestack")] = ESP.getFreeContStack();
  doc[F("flashsize")] = ESP.getFlashChipRealSize();

  uint32_t crashCount = CrashSaver::count();
  doc[F("crashcount")] = crashCount;
#else
  doc[F("freestack")] = uxTaskGetStackHighWaterMark(nullptr);
  doc[F("crashcount")] = 0;
#endif
}
bool Core::appInit(bool reInit = false)
{
  return true;
};
const PROGMEM char *Core::getHTMLContent(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return status0htmlgz;
    break;
  case config:
    return config0htmlgz;
    break;
  };
  return nullptr;
}
size_t Core::getHTMLContentSize(WebPageForPlaceHolder wp)
{
  switch (wp)
  {
  case status:
    return sizeof(status0htmlgz);
    break;
  case config:
    return sizeof(config0htmlgz);
    break;
  };
  return 0;
}
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

  server.on(F("/fw.html"), HTTP_GET,
            [this, &server]()
            {
              SERVER_KEEPALIVE_FALSE()
              server.sendHeader(F("Content-Encoding"), F("gzip"));
              server.send_P(200, PSTR("text/html"), fwhtmlgz, sizeof(fwhtmlgz));
            });

  // Get Latest Update Info ---------------------------------------------------------
  server.on(
      F("/glui"), HTTP_GET,
      [this, &server]()
      {
        SERVER_KEEPALIVE_FALSE()
        JsonDocument doc;
        fillLatestUpdateInfoJson(doc, true);
        server.setContentLength(measureJson(doc));
        server.send(200, F("application/json"), "");
        WiFiClient client = server.client();
        serializeJson(doc, client);
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
        snprintf_P(redirectUrl, sizeof(redirectUrl), PSTR("http://%d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        server.sendHeader(F("Location"), redirectUrl, true);
        server.send(302, F("text/plain"), ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
        server.client().stop();
      });
}