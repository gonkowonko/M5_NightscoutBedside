#include "ota.h"

#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFi.h>

#include "display.h"
#include "eventlog.h"
#include "updater.h"
#include "webconfig.h"

namespace {

bool gStarted = false;
size_t gExpected = 0;
size_t gWritten = 0;
char gError[64] = "";

void showProgress(size_t done, size_t total) {
  static int last = -1;
  int pct = total ? (int)(done * 100 / total) : -1;
  if (pct == last) return;
  last = pct;
  char detail[32];
  if (pct >= 0)
    snprintf(detail, sizeof(detail), "%d%%  -  don't unplug", pct);
  else
    snprintf(detail, sizeof(detail), "%u KB  -  don't unplug", (unsigned)(done / 1024));
  displayProgress("Updating firmware", detail, pct);
}

void setError(const char* where) {
  snprintf(gError, sizeof(gError), "%s: %s", where, Update.errorString());
  logEvent("Update failed: %s", Update.errorString());
}

void beginArduinoOta() {
  ArduinoOTA.setHostname(kMdnsName);
  ArduinoOTA.setMdnsEnabled(false);  // network.cpp already runs mDNS
  ArduinoOTA.onStart([] {
    logEvent("Update started (PlatformIO)");
    showProgress(0, 1);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) { showProgress(done, total); });
  ArduinoOTA.onEnd([] { displayProgress("Update complete", "Restarting...", 100); });
  ArduinoOTA.onError([](ota_error_t e) {
    logEvent("Update failed (PlatformIO, error %d)", (int)e);
    displayInvalidate();
  });
  ArduinoOTA.begin();
}

}  // namespace

void otaLoop() {
  if (!gStarted) {
    if (WiFi.status() != WL_CONNECTED) return;
    beginArduinoOta();
    gStarted = true;
  }
  ArduinoOTA.handle();
}

bool otaWebStart(size_t expectedSize) {
  gError[0] = 0;
  if (updaterBusy() || Update.isRunning()) {
    strlcpy(gError, "another update is already running", sizeof(gError));
    return false;
  }
  gExpected = expectedSize;
  gWritten = 0;
  logEvent("Update started (web)");
  showProgress(0, gExpected);
  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    setError("start");
    return false;
  }
  return true;
}

bool otaWebWrite(const uint8_t* data, size_t len) {
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    setError("write");
    Update.abort();
    return false;
  }
  gWritten += len;
  showProgress(gWritten, gExpected);
  return true;
}

bool otaWebFinish() {
  if (!Update.end(true)) {
    setError("verify");
    displayInvalidate();
    return false;
  }
  displayProgress("Update complete", "Restarting...", 100);
  return true;
}

void otaWebAbort() {
  Update.abort();
  if (!gError[0]) strlcpy(gError, "upload aborted", sizeof(gError));
  logEvent("Update aborted (web)");
  displayInvalidate();
}

const char* otaError() { return gError; }
