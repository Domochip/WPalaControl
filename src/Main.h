#ifndef Main_h
#define Main_h

#include <Arduino.h>

// Helper macros to convert a define to a string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// ----- Should be modified for your application -----
#define CUSTOM_APP_MANUFACTURER "Domochip"
#define CUSTOM_APP_MODEL "WPalaControl"
#define CUSTOM_APP_CLASS WPalaControl
#define VERSION_NUMBER "3.3.0"

#define CUSTOM_APP_HEADER TOSTRING(CUSTOM_APP_CLASS.h) // calculated header file "{CUSTOM_APP_CLASS}.h"
#define DEFAULT_AP_SSID CUSTOM_APP_MODEL               // Default Access Point SSID "{CUSTOM_APP_MODEL}"
#define DEFAULT_AP_PSK "password"                      // Default Access Point Password

// Control EventSourceMan code (To be used by Application if EventSource server is needed)
#define EVTSRC_ENABLED 1
#define EVTSRC_MAX_CLIENTS 2
#define EVTSRC_KEEPALIVE_ENABLED 0

// Enable developper mode
#define DEVELOPPER_MODE 0

// Log Serial Object
#ifdef ESP8266
#define LOG_SERIAL Serial1
#else
#define LOG_SERIAL Serial
#endif
// Choose Log Serial Speed
#define LOG_SERIAL_SPEED 38400

// Log Serial Macros
#ifdef LOG_SERIAL
#define LOG_SERIAL_PRINT(...) LOG_SERIAL.print(__VA_ARGS__)
#define LOG_SERIAL_PRINTLN(...) LOG_SERIAL.println(__VA_ARGS__)
#define LOG_SERIAL_PRINTF(...) LOG_SERIAL.printf(__VA_ARGS__)
#define LOG_SERIAL_PRINTF_P(...) LOG_SERIAL.printf_P(__VA_ARGS__)
#else
#define LOG_SERIAL_PRINT(...)
#define LOG_SERIAL_PRINTLN(...)
#define LOG_SERIAL_PRINTF(...)
#define LOG_SERIAL_PRINTF_P(...)
#endif

// Choose Pin used to boot in Rescue Mode
// #define RESCUE_BTN_PIN 2

// Define time to wait for Rescue press (in s)
// #define RESCUE_BUTTON_WAIT 3

// Status LED
// #define STATUS_LED_SETUP pinMode(XX, OUTPUT);pinMode(XX, OUTPUT);
// #define STATUS_LED_OFF digitalWrite(XX, HIGH);digitalWrite(XX, HIGH);
// #define STATUS_LED_ERROR digitalWrite(XX, HIGH);digitalWrite(XX, HIGH);
// #define STATUS_LED_WARNING digitalWrite(XX, HIGH);digitalWrite(XX, HIGH);
// #define STATUS_LED_GOOD digitalWrite(XX, HIGH);digitalWrite(XX, HIGH);

// construct Version text
#if DEVELOPPER_MODE
#define VERSION VERSION_NUMBER "-DEV"
#else
#define VERSION VERSION_NUMBER
#endif

#endif
