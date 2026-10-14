// Screen rendering: layout "A" bedside clock plus a debug page.
//
// Every region is rendered off-screen through one small, reused band sprite
// and pushed in one go, so nothing flickers and no full-screen buffer is
// needed (the Basic Core has no PSRAM).
#pragma once

#include <Arduino.h>

#include "glucose.h"

// Everything the main screen shows. Strings are UTF-8.
struct ScreenModel {
  char date[16] = "";   // "Wed 14 Oct", empty until time is known
  char temp[12] = "";   // "19°C", empty without sensor
  char time[8] = "";    // "07:42" or "--:--"

  bool hasBg = false;   // false -> "---" in grey
  char bg[8] = "";      // "5.6" or "101"
  char delta[8] = "";   // "+0.2", "-0.3", "0.0" (ASCII minus)
  Trend trend = Trend::None;
  BgRange range = BgRange::InRange;
  bool stale = false;
  char age[16] = "";    // "3 min ago"
};

void displayBegin();       // M5.begin() and SD must already be up
int displayFontsLoaded();  // number of .vlw fonts loaded from /fonts (of 5)

// Full-screen centred message (boot, missing SD card, ...).
void displayMessage(const char* title, const char* detail);

// Draws the main screen, touching only the regions that changed.
void displayUpdate(const ScreenModel& m);

// Forces a full repaint on the next displayUpdate()/displayDebug().
void displayInvalidate();

// Debug page: one line of text per entry, redrawn only when changed.
void displayDebug(const char* const* lines, int count);
