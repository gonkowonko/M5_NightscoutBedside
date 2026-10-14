// Configuration read from M5NS.INI on the SD card.
//
// The file format is the one used by mlukasek/M5_NightscoutMon, so the same
// card works with either firmware. Unknown keys are ignored.
#pragma once

#include <Arduino.h>

constexpr int kMaxWifi = 10;  // [wlan0]..[wlan9]

struct AppConfig {
  // [config]
  char nightscout[128] = "";  // host or URL of the Nightscout site
  char token[96] = "";        // access token (optional)
  char tz[64] = "";           // POSIX TZ string actually used
  bool showMgdl = false;      // show_mgdl = 1 -> display mg/dL, else mmol/L
  int timeFormat = 0;         // 0 = 24 h, 1 = 12 h
  int dateFormat = 0;         // 0 = "Wed 14 Oct", 1 = "Wed Oct 14"
  int brightness[3] = {50, 100, 10};  // percent, cycled with button A

  // Range thresholds, in display units (mmol/L or mg/dL per show_mgdl).
  float yellowLow = 4.5f;
  float yellowHigh = 9.0f;
  float redLow = 3.9f;
  float redHigh = 11.0f;

  // New keys (this firmware only).
  int staleMinutes = 10;
  bool nightEnabled = false;  // opt-in: dim between nightStart and nightEnd
  int nightStart = 22 * 60;  // minutes after midnight
  int nightEnd = 7 * 60;
  int nightBrightness = 5;   // percent

  // [wlanN]
  int wifiCount = 0;
  char ssid[kMaxWifi][33] = {};
  char pass[kMaxWifi][65] = {};
};

// Reads the INI file from the (already mounted) SD card.
// Returns false if the file could not be opened; cfg then holds defaults.
bool loadConfig(const char* path, AppConfig& cfg);

// Writes every setting this firmware manages back into the INI file, editing
// matching keys in place so unknown keys, comments and the original firmware's
// settings are kept. Wi-Fi sections are rewritten as [wlan1]..[wlanN].
// The previous file is kept as /M5NS.PRV. Returns false on an SD error.
bool saveConfig(const char* path, const AppConfig& cfg);
