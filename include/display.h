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
  char temp[12] = "";   // "19.5°C", empty without sensor
  char time[8] = "";    // "07:42" or "--:--"

  bool hasBg = false;   // false -> "---" in grey
  char bg[8] = "";      // "5.6" or "101"
  char delta[8] = "";   // "+0.2", "-0.3", "0.0" (ASCII minus)
  Trend trend = Trend::None;
  BgRange range = BgRange::InRange;
  bool stale = false;
  char age[20] = "";    // "3 min ago" / "just now" / "Refreshing…"
  bool ageAlert = false;  // age line in red (stale, or a failed refresh)
};

void displayBegin();  // after M5.begin()

// ---- Startup splash ----------------------------------------------------------
enum class StepState : uint8_t { Todo, Wait, Ok, Fail };

struct SplashStep {
  StepState state = StepState::Todo;
  char label[12] = "";
  char detail[28] = "";  // right-aligned, e.g. "MyWiFi · -52"
};

struct SplashModel {
  SplashStep steps[4];  // Settings, Wi-Fi, Time, Nightscout
  char footer[56] = "";
  bool footerAlert = false;  // hint in red
  uint8_t frame = 0;         // animation frame for waiting dots (0-2)
};

// Draws the startup splash (logo, title, version, checklist, footer),
// repainting only what changed.
void displaySplash(const SplashModel& m, const char* version);

// Draws the main screen, touching only the regions that changed.
void displayUpdate(const ScreenModel& m);

// Full-screen progress bar (firmware updates). percent < 0 shows only text.
void displayProgress(const char* title, const char* detail, int percent);

// Forces a full repaint on the next displayUpdate()/displayDebug().
void displayInvalidate();

// Text page (status / log): one line per entry, redrawn only when changed.
void displayDebug(const char* const* lines, int count);
