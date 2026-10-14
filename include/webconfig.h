// Settings web page at http://m5clock.local (or the IP on the status page).
//
// Reads and writes M5NS.INI on the SD card. Display settings apply at once;
// changes to Nightscout or Wi-Fi restart the clock. No password, by choice:
// anyone on the local network can open it.
#pragma once

#include "config.h"
#include "display.h"

struct WebHooks {
  // Called with the freshly saved and re-read config.
  void (*applied)(const AppConfig& saved);
  // What the clock currently shows, for the status line at the top.
  ScreenModel (*screen)();
};

constexpr const char* kMdnsName = "m5clock";

void webBegin(const AppConfig& current, const char* iniPath, WebHooks hooks);
void webLoop();  // call from loop(); handles at most one pending request
