#include "network.h"

#include <WiFi.h>
#include <WiFiMulti.h>

#include "timekeeping.h"

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

  Lock lock;
  if (ok) {
    gReading = reading;
    gGeneration++;
  }
  gStatus.everFetched = true;
  gStatus.lastFetchOk = ok;
  gStatus.lastFetchMillis = millis();
  gStatus.lastFetchTime = timeIsValid() ? time(nullptr) : 0;
  strlcpy(gStatus.lastResult, result, sizeof(gStatus.lastResult));
}

void networkTask(void*) {
  bool ntpStarted = false;
  uint32_t lastFetch = 0;
  bool fetchedOnce = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      // Tries every configured network and picks the strongest one in range.
      if (gWifi.run(kWifiConnectTimeoutMs) == WL_CONNECTED) {
        Serial.printf("wifi: connected to %s, %s\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str());
      } else {
        updateWifiStatus();
        // Nothing in range: back off before scanning again.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWifiRetryMs));
        continue;
      }
    }
    updateWifiStatus();

    if (WiFi.status() == WL_CONNECTED) {
      if (!ntpStarted) {
        timeStartSync();
        ntpStarted = true;
      }
      if (!fetchedOnce || millis() - lastFetch >= kFetchIntervalMs) {
        fetch();
        lastFetch = millis();
        fetchedOnce = true;
      }
    }

    // Sleep until the next check, or until a refresh is requested.
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0) fetchedOnce = false;
  }
}

}  // namespace

void networkBegin(const AppConfig& cfg) {
  gCfg = &cfg;
  gLock = xSemaphoreCreateMutex();

  WiFi.persistent(false);
  WiFi.setHostname("m5-bedside-clock");  // must precede mode() on arduino-esp32 2.x
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // WiFiMulti handles reconnects across all networks
  for (int i = 0; i < cfg.wifiCount; i++) gWifi.addAP(cfg.ssid[i], cfg.pass[i]);

  if (cfg.wifiCount == 0) {
    strlcpy(gStatus.lastResult, "no Wi-Fi in INI", sizeof(gStatus.lastResult));
    return;
  }
  // TLS needs a generous stack. Core 0 keeps it off the UI loop's core.
  xTaskCreatePinnedToCore(networkTask, "net", 12 * 1024, nullptr, 1, &gTask, 0);
}

void networkRequestRefresh() {
  if (gTask) xTaskNotifyGive(gTask);
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
