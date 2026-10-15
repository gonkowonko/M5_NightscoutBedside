#include "network.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "eventlog.h"
#include "timekeeping.h"
#include "updater.h"
#include "webconfig.h"

namespace {

constexpr uint32_t kFetchIntervalMs = 60 * 1000;
constexpr uint32_t kWifiConnectTimeoutMs = 15 * 1000;
constexpr uint32_t kWifiRetryMs = 10 * 1000;

const AppConfig* gCfg = nullptr;
TaskHandle_t gTask = nullptr;
SemaphoreHandle_t gLock = nullptr;

// Guarded by gLock.
NsReading gReading;
uint32_t gGeneration = 0;
NetStatus gStatus;

WiFiMulti gWifi;

// Task notification bits.
constexpr uint32_t kRefreshBit = 1u << 0;  // fetch Nightscout now
constexpr uint32_t kWakeBit = 1u << 1;     // just run the loop (e.g. updater work)

// Sleeps up to `ms`; returns true if a Nightscout refresh was requested.
bool sleepUntilWoken(uint32_t ms) {
  uint32_t bits = 0;
  xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(ms));
  return bits & kRefreshBit;
}

struct Lock {
  Lock() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gLock); }
};

void updateWifiStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  Lock lock;
  gStatus.connected = connected;
  if (connected) {
    strlcpy(gStatus.ssid, WiFi.SSID().c_str(), sizeof(gStatus.ssid));
    gStatus.rssi = WiFi.RSSI();
    strlcpy(gStatus.ip, WiFi.localIP().toString().c_str(), sizeof(gStatus.ip));
  } else {
    gStatus.ssid[0] = 0;
    gStatus.rssi = 0;
    gStatus.ip[0] = 0;
  }
}

void fetch() {
  NsReading reading;
  char result[sizeof(gStatus.lastResult)];
  bool ok = nightscoutFetch(*gCfg, reading, result, sizeof(result));
  Serial.printf("nightscout: %s\n", result);

  // Log failures, and the first success after them; not every good fetch.
  static int failures = 0;
  static bool everOk = false;
  if (!ok) {
    logEvent("NS %s", result);
    failures++;
  } else if (!everOk) {
    logEvent("NS first reading: %d mg/dL", reading.sgv);
  } else if (failures > 0) {
    logEvent("NS OK again after %d failure%s", failures, failures == 1 ? "" : "s");
  }
  if (ok) {
    everOk = true;
    failures = 0;
  }

  Lock lock;
  if (ok) {
    gReading = reading;
    gGeneration++;
  }
  gStatus.everFetched = true;
  gStatus.lastFetchOk = ok;
  gStatus.fetchCount++;
  gStatus.lastFetchMillis = millis();
  gStatus.lastFetchTime = timeIsValid() ? time(nullptr) : 0;
  strlcpy(gStatus.lastResult, result, sizeof(gStatus.lastResult));
}

void networkTask(void*) {
  bool ntpStarted = false;
  uint32_t lastFetch = 0;
  bool fetchedOnce = false;
  bool wasConnected = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (wasConnected) {
        logEvent("Wi-Fi lost");
        wasConnected = false;
      }
      // Tries every configured network and picks the strongest one in range.
      auto st = static_cast<wl_status_t>(gWifi.run(kWifiConnectTimeoutMs));
      if (st == WL_CONNECTED) {
        wasConnected = true;
        logEvent("Wi-Fi: %s %d dBm", WiFi.SSID().c_str(), WiFi.RSSI());
      } else {
        if (st == WL_NO_SSID_AVAIL)
          logEvent("Wi-Fi: no known network in range");
        else if (st == WL_CONNECT_FAILED)
          logEvent("Wi-Fi: connect failed (password?)");
        else
          logEvent("Wi-Fi: connect timed out (%d)", (int)st);
        updateWifiStatus();
        // Nothing in range: back off before scanning again.
        if (sleepUntilWoken(kWifiRetryMs)) fetchedOnce = false;
        continue;
      }
    }
    updateWifiStatus();

    if (WiFi.status() == WL_CONNECTED) {
      if (!ntpStarted) {
        timeStartSync();
        // Settings page at http://m5clock.local
        if (MDNS.begin(kMdnsName)) MDNS.addService("http", "tcp", 80);
        ntpStarted = true;
      }
      if (!fetchedOnce || millis() - lastFetch >= kFetchIntervalMs) {
        fetch();
        lastFetch = millis();
        fetchedOnce = true;
      }
      // Update checks/installs share this task, so only one HTTPS
      // connection is ever open at a time.
      updaterService();
    }

    // Sleep until the next check, or until something is requested.
    if (sleepUntilWoken(1000)) fetchedOnce = false;
  }
}

}  // namespace

void networkBegin(const AppConfig& cfg) {
  gCfg = &cfg;
  gLock = xSemaphoreCreateMutex();

  WiFi.persistent(false);
  WiFi.setHostname("m5-bedside-clock");  // must precede mode() on arduino-esp32 2.x
  WiFi.mode(WIFI_STA);
  // Always on USB power: modem sleep only adds latency, and it makes OTA
  // uploads stall and time out.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);  // WiFiMulti handles reconnects across all networks
  for (int i = 0; i < cfg.wifiCount; i++) gWifi.addAP(cfg.ssid[i], cfg.pass[i]);

  if (cfg.wifiCount == 0) {
    strlcpy(gStatus.lastResult, "no Wi-Fi in INI", sizeof(gStatus.lastResult));
    return;
  }
  // TLS with certificate checks needs a generous stack. Core 0 keeps it off
  // the UI loop's core.
  xTaskCreatePinnedToCore(networkTask, "net", 16 * 1024, nullptr, 1, &gTask, 0);
}

void networkWake() {
  if (gTask) xTaskNotify(gTask, kWakeBit, eSetBits);
}

void networkRequestRefresh() {
  if (gTask) xTaskNotify(gTask, kRefreshBit, eSetBits);
}

uint32_t networkLatest(NsReading& out) {
  Lock lock;
  out = gReading;
  return gGeneration;
}

void networkStatus(NetStatus& out) {
  if (!gLock) return;
  Lock lock;
  out = gStatus;
}
