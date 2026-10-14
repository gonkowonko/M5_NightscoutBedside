// Over-the-air firmware updates.
//
//  - From PlatformIO:  pio run -e ota -t upload   (ArduinoOTA, port 3232)
//  - From the browser: the Firmware section of the settings page (POST /update)
//
// Both write the inactive app slot, verify it, then restart into it. A bad
// image is rejected and the running firmware carries on. No password.
#pragma once

#include <stddef.h>
#include <stdint.h>

void otaLoop();  // call from loop(): starts ArduinoOTA once Wi-Fi is up

// Browser uploads, driven by the web server's upload callbacks.
bool otaWebStart(size_t expectedSize);
bool otaWebWrite(const uint8_t* data, size_t len);
bool otaWebFinish();  // true when the new firmware is verified and ready
void otaWebAbort();
const char* otaError();  // last error, for the web response
