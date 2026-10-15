#include "updater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "eventlog.h"
#include "network.h"
#include "timekeeping.h"
#include "version.h"

// Root CAs (tools/make_ca_bundle.py), embedded via board_build.embed_files.
extern const uint8_t kCaBundle[] asm("_binary_certs_x509_crt_bundle_bin_start");

namespace {

constexpr uint32_t kFirstCheckDelayMs = 60 * 1000;
constexpr uint32_t kCheckIntervalMs = 2 * 24 * 60 * 60 * 1000UL;  // 2 days
constexpr uint32_t kStallTimeoutMs = 20 * 1000;
constexpr const char* kLatestUrl = "https://api.github.com/repos/" FW_REPO "/releases/latest";

SemaphoreHandle_t gLock = xSemaphoreCreateMutex();
UpdateStatus gStatus;  // guarded by gLock

// Only touched by the network task.
char gAssetUrl[256] = "";
char gAssetSha[65] = "";
uint32_t gAssetSize = 0;
bool gHaveCheckTime = false;
uint32_t gLastCheckMs = 0;

volatile bool gCheckRequested = false;
volatile bool gInstallRequested = false;

struct Lock {
  Lock() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gLock); }
};

// "v1.12.3" -> comparable number; anything unparsable is 0.
uint32_t versionNumber(const char* v) {
  if (*v == 'v' || *v == 'V') v++;
  unsigned a = 0, b = 0, c = 0;
  if (sscanf(v, "%u.%u.%u", &a, &b, &c) < 2) return 0;
  return a * 1000000u + b * 1000u + c;
}

void setError(const char* fmt, const char* detail = "") {
  char msg[sizeof(gStatus.error)];
  snprintf(msg, sizeof(msg), fmt, detail);
  logEvent("Update: %s", msg);
  Lock lock;
  strlcpy(gStatus.error, msg, sizeof(gStatus.error));
}

void secureClient(WiFiClientSecure& client) {
  client.setCACertBundle(kCaBundle);  // real certificate checks, unlike the Nightscout fetch
  client.setTimeout(15);              // seconds
}

void check() {
  { Lock lock; gStatus.checking = true; gStatus.error[0] = 0; }

  WiFiClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed from the stream
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.begin(client, kLatestUrl);
  http.setUserAgent("M5_NightscoutBedside/" FW_VERSION);
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["body"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
    filter["assets"][0]["digest"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) {
      setError("bad reply from GitHub (%s)", err.c_str());
    } else {
      const char* tag = doc["tag_name"] | "";
      JsonObject asset;
      for (JsonObject a : doc["assets"].as<JsonArray>()) {
        String name = a["name"] | "";
        if (name.endsWith("-ota.bin")) asset = a;
      }
      const char* digest = asset.isNull() ? "" : asset["digest"] | "";
      if (!*tag || asset.isNull() || strncmp(digest, "sha256:", 7) != 0 || strlen(digest) != 71) {
        setError("release %s has no verifiable firmware file", tag);
      } else {
        strlcpy(gAssetUrl, asset["browser_download_url"] | "", sizeof(gAssetUrl));
        strlcpy(gAssetSha, digest + 7, sizeof(gAssetSha));
        gAssetSize = asset["size"] | 0;
        bool newer = versionNumber(tag) > versionNumber(FW_VERSION);
        bool announce;
        {
          Lock lock;
          const char* bare = (*tag == 'v' || *tag == 'V') ? tag + 1 : tag;
          announce = newer && strcmp(gStatus.latest, bare) != 0;
          strlcpy(gStatus.latest, bare, sizeof(gStatus.latest));
          strlcpy(gStatus.notes, doc["body"] | "", sizeof(gStatus.notes));
          gStatus.available = newer;
          gStatus.checkedAt = time(nullptr);
        }
        if (announce) logEvent("Update available: %s", tag);
      }
    }
  } else if (code == HTTP_CODE_NOT_FOUND) {
    setError("no releases published yet");
  } else if (code < 0) {
    setError("can't reach GitHub (%s)", HTTPClient::errorToString(code).c_str());
  } else {
    char c[8];
    snprintf(c, sizeof(c), "%d", code);
    setError("GitHub returned HTTP %s", c);
  }
  http.end();
  Lock lock;
  gStatus.checking = false;
}

void install() {
  char tag[16];
  {
    Lock lock;
    if (!gStatus.available || !gAssetUrl[0]) {
      gStatus.installing = false;
      return;
    }
    strlcpy(tag, gStatus.latest, sizeof(tag));
    gStatus.installing = true;
    gStatus.error[0] = 0;
    gStatus.size = gAssetSize;
    gStatus.done = 0;
  }
  logEvent("Installing update v%s", tag);

  WiFiClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  // GitHub redirects release downloads to its file host (also verified).
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, gAssetUrl);
  http.setUserAgent("M5_NightscoutBedside/" FW_VERSION);
  int code = http.GET();

  const char* failure = nullptr;
  char detail[48] = "";
  int len = http.getSize();
  if (code != HTTP_CODE_OK) {
    snprintf(detail, sizeof(detail), "%d %s", code, code < 0 ? HTTPClient::errorToString(code).c_str() : "");
    failure = "download failed (HTTP %s)";
  } else if (len <= 0 || (uint32_t)len != gAssetSize) {
    failure = "download size doesn't match the release";
  } else if (Update.isRunning()) {
    failure = "another update is already running";
  } else if (!Update.begin(len, U_FLASH)) {
    strlcpy(detail, Update.errorString(), sizeof(detail));
    failure = "can't start update (%s)";
  }

  if (!failure) {
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[2048];
    uint32_t done = 0, lastData = millis();
    while (done < (uint32_t)len) {
      size_t avail = stream->available();
      if (avail) {
        int n = stream->readBytes(buf, std::min(avail, sizeof(buf)));
        if (n <= 0) continue;
        mbedtls_sha256_update_ret(&sha, buf, n);
        if (Update.write(buf, n) != (size_t)n) {
          strlcpy(detail, Update.errorString(), sizeof(detail));
          failure = "write failed (%s)";
          break;
        }
        done += n;
        lastData = millis();
        Lock lock;
        gStatus.done = done;
      } else if (!stream->connected() && !stream->available()) {
        failure = "connection closed early";
        break;
      } else if (millis() - lastData > kStallTimeoutMs) {
        failure = "download stalled";
        break;
      } else {
        delay(5);
      }
    }

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);

    if (!failure && strcasecmp(hex, gAssetSha) != 0) failure = "checksum mismatch, file rejected";
    if (failure) {
      Update.abort();  // the running firmware stays as it is
    } else if (!Update.end(true)) {
      strlcpy(detail, Update.errorString(), sizeof(detail));
      failure = "verify failed (%s)";
    }
  }
  http.end();

  if (failure) {
    setError(failure, detail);
    Lock lock;
    gStatus.installing = false;
    return;
  }
  logEvent("Update v%s installed, restarting", tag);
  Lock lock;
  gStatus.installing = false;
  gStatus.installed = true;
}

}  // namespace

void updaterService() {
  if (!timeIsValid()) return;  // certificate checks need the correct date
  uint32_t now = millis();
  bool due = gHaveCheckTime ? now - gLastCheckMs >= kCheckIntervalMs : now >= kFirstCheckDelayMs;
  if (gCheckRequested || due) {
    gCheckRequested = false;
    gHaveCheckTime = true;
    gLastCheckMs = now;
    check();
  }
  if (gInstallRequested) {
    gInstallRequested = false;
    install();
  }
}

void updaterRequestCheck() {
  gCheckRequested = true;
  { Lock lock; gStatus.checking = true; }  // show "Checking…" straight away
  networkWake();
}

bool updaterRequestInstall() {
  {
    Lock lock;
    if (!gStatus.available || gStatus.installing || gStatus.installed) return false;
    gStatus.installing = true;  // shown at once; install() confirms
    gStatus.done = 0;
    gStatus.error[0] = 0;
  }
  gInstallRequested = true;
  networkWake();
  return true;
}

void updaterStatus(UpdateStatus& out) {
  Lock lock;
  out = gStatus;
}

bool updaterBusy() {
  Lock lock;
  return gStatus.installing;
}
