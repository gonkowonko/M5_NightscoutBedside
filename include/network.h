// Background network task: keeps Wi-Fi connected, starts NTP, and fetches
// Nightscout every 60 s. The UI never blocks on the network.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "nightscout.h"

struct NetStatus {
  bool connected = false;
  char ssid[33] = "";
  int rssi = 0;
  char ip[16] = "";
  time_t lastFetchTime = 0;         // epoch seconds of last attempt (0 = never/unknown)
  uint32_t lastFetchMillis = 0;     // millis() of last attempt
  bool everFetched = false;
  bool lastFetchOk = false;
  char lastResult[48] = "not fetched yet";
};

void networkBegin(const AppConfig& cfg);  // cfg must outlive the task
void networkRequestRefresh();             // fetch as soon as possible

// Copies the last good reading. Returns a counter that increments with
// every successful fetch, so callers can cheaply detect new data.
uint32_t networkLatest(NsReading& out);
void networkStatus(NetStatus& out);
