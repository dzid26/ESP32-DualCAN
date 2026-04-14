#include <unity_config.h>

// Lives at test/ root so every test env inherits it. Must compile cleanly
// for Arduino (hw-arduino), ESP-IDF (esp32-c6 / esp32-c6-debug), and
// native envs — pick the output path based on what's defined by the
// framework.

#if defined(ARDUINO)
#  include <Arduino.h>
#else
#  include <stdio.h>
#endif

extern "C" {

__attribute__((weak)) void setUp(void) {}
__attribute__((weak)) void tearDown(void) {}
__attribute__((weak)) void suiteSetUp(void) {}
__attribute__((weak)) int  suiteTearDown(int num_failures) { return num_failures; }

#if defined(ARDUINO)

void unityOutputStart(unsigned long baudrate) { Serial.begin(baudrate); }
void unityOutputChar(unsigned int c)          { Serial.write(c); }
void unityOutputFlush(void)                   { Serial.flush(); }

// Deliberately DO NOT call Serial.end() here. PlatformIO's default
// implementation calls Serial.end() which on ESP32-C6 HWCDC invokes
// HWCDC::deinit(), pulling USB D+/D- low and killing the device's
// USB enumeration until a hardware reset.
// https://github.com/platformio/platformio-core/issues/5359
void unityOutputComplete(void)                { Serial.flush(); }

#else  // ESP-IDF and native: go through stdio (putchar / fflush).

void unityOutputStart(unsigned long baudrate) { (void) baudrate; }
void unityOutputChar(unsigned int c)          { putchar(c); }
void unityOutputFlush(void)                   { fflush(stdout); }
void unityOutputComplete(void)                { fflush(stdout); }

#endif

}
