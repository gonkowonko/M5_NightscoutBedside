// Nightscout API client: fetches the latest two SGV entries.
#pragma once

#include <Arduino.h>

#include "config.h"

struct NsReading {
  bool valid = false;
  int sgv = 0;              // mg/dL, newest entry
  bool hasPrev = false;
  int prevSgv = 0;          // mg/dL, previous entry
  char direction[16] = "";  // e.g. "Flat", "SingleUp"
  int64_t dateMs = 0;       // epoch milliseconds of the newest entry
};

// Blocking HTTPS GET of /api/v1/entries/sgv.json?count=2. Must be called with
// Wi-Fi connected. On success fills `out` and returns true. `status` always
// receives a short human-readable result (never contains the token).
bool nightscoutFetch(const AppConfig& cfg, NsReading& out, char* status, size_t statusLen);
