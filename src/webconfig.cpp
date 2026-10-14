#include "webconfig.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "eventlog.h"
#include "webpage.h"

namespace {

WebServer gServer(80);
const AppConfig* gCurrent = nullptr;
const char* gIniPath = nullptr;
WebHooks gHooks = {};
uint32_t gRestartAt = 0;  // millis() when to restart, 0 = not pending

String hhmm(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "%02d:%02d", minutes / 60, minutes % 60);
  return b;
}

bool parseHhmm(const char* s, int& out) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
  out = h * 60 + m;
  return true;
}

// Copies user text into a fixed buffer, dropping control characters so a
// value can never break the INI file's line structure.
bool copyClean(char* dst, size_t len, const char* src) {
  if (!src) src = "";
  size_t o = 0;
  for (const char* p = src; *p; p++) {
    if ((uint8_t)*p < 0x20 || *p == 0x7F) continue;
    if (o + 1 >= len) return false;  // too long
    dst[o++] = *p;
  }
  dst[o] = 0;
  return true;
}

float roundTo(float v, bool mgdl) { return mgdl ? roundf(v) : roundf(v * 10) / 10; }

void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  gServer.sendHeader("Cache-Control", "no-store");
  gServer.send(code, "application/json", out);
}

void sendError(const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(400, doc);
}

void handlePage() {
  gServer.sendHeader("Cache-Control", "no-store");
  gServer.send_P(200, "text/html", kSettingsPage);
}

void handleGetConfig() {
  const AppConfig& c = *gCurrent;
  JsonDocument doc;
  doc["nightscout"] = c.nightscout;
  doc["tokenSet"] = c.token[0] != 0;
  doc["showMgdl"] = c.showMgdl;
  doc["redLow"] = roundTo(c.redLow, c.showMgdl);
  doc["yellowLow"] = roundTo(c.yellowLow, c.showMgdl);
  doc["yellowHigh"] = roundTo(c.yellowHigh, c.showMgdl);
  doc["redHigh"] = roundTo(c.redHigh, c.showMgdl);
  doc["staleMinutes"] = c.staleMinutes;
  JsonArray b = doc["brightness"].to<JsonArray>();
  for (int v : c.brightness) b.add(v);
  doc["nightBrightness"] = c.nightBrightness;
  doc["nightStart"] = hhmm(c.nightStart);
  doc["nightEnd"] = hhmm(c.nightEnd);
  doc["timeFormat"] = c.timeFormat;
  doc["dateFormat"] = c.dateFormat;
  doc["tz"] = c.tz;
  JsonArray wifi = doc["wifi"].to<JsonArray>();
  for (int i = 0; i < c.wifiCount; i++) {
    JsonObject w = wifi.add<JsonObject>();
    w["ssid"] = c.ssid[i];
    w["orig"] = i;
    w["hasPass"] = c.pass[i][0] != 0;  // passwords never leave the device
  }
  sendJson(200, doc);
}

void handleStatus() {
  ScreenModel m = gHooks.screen();
  JsonDocument doc;
  doc["time"] = m.time;
  doc["date"] = m.date;
  doc["temp"] = m.temp;
  doc["hasBg"] = m.hasBg;
  doc["bg"] = m.bg;
  doc["delta"] = m.delta;
  doc["age"] = m.age;
  doc["stale"] = m.stale;
  doc["range"] = m.range == BgRange::Urgent    ? "Urgent"
                 : m.range == BgRange::Warning ? "Warning"
                                               : "InRange";
  sendJson(200, doc);
}

bool sameNetworkSettings(const AppConfig& a, const AppConfig& b) {
  if (strcmp(a.nightscout, b.nightscout) || strcmp(a.token, b.token) || a.wifiCount != b.wifiCount)
    return false;
  for (int i = 0; i < a.wifiCount; i++)
    if (strcmp(a.ssid[i], b.ssid[i]) || strcmp(a.pass[i], b.pass[i])) return false;
  return true;
}

void handlePostConfig() {
  JsonDocument in;
  if (deserializeJson(in, gServer.arg("plain"))) return sendError("Invalid request");

  // Big structs: keep them off the loop task's stack.
  static AppConfig next;
  static AppConfig saved;
  next = *gCurrent;

  if (!copyClean(next.nightscout, sizeof(next.nightscout), in["nightscout"] | ""))
    return sendError("Nightscout site is too long");
  if (in["clearToken"] | false) {
    next.token[0] = 0;
  } else if (const char* t = in["token"]; t && *t) {
    if (!copyClean(next.token, sizeof(next.token), t)) return sendError("Token is too long");
  }

  next.showMgdl = in["showMgdl"] | false;
  next.redLow = in["redLow"] | NAN;
  next.yellowLow = in["yellowLow"] | NAN;
  next.yellowHigh = in["yellowHigh"] | NAN;
  next.redHigh = in["redHigh"] | NAN;
  float th[] = {next.redLow, next.yellowLow, next.yellowHigh, next.redHigh};
  for (float v : th)
    if (!(v > 0 && v < 1000)) return sendError("Enter all four glucose thresholds");
  if (!(next.redLow <= next.yellowLow && next.yellowLow < next.yellowHigh &&
        next.yellowHigh <= next.redHigh))
    return sendError("Thresholds must go: urgent below ≤ warn below < warn above ≤ urgent above");

  next.staleMinutes = in["staleMinutes"] | 0;
  if (next.staleMinutes < 1 || next.staleMinutes > 240) return sendError("Stale minutes must be 1–240");

  for (int i = 0; i < 3; i++) {
    next.brightness[i] = in["brightness"][i] | 0;
    if (next.brightness[i] < 1 || next.brightness[i] > 100) return sendError("Brightness levels must be 1–100%");
  }
  next.nightBrightness = in["nightBrightness"] | -1;
  if (next.nightBrightness < 0 || next.nightBrightness > 100) return sendError("Night brightness must be 0–100%");
  if (!parseHhmm(in["nightStart"] | "", next.nightStart) || !parseHhmm(in["nightEnd"] | "", next.nightEnd))
    return sendError("Night times must be HH:MM");

  next.timeFormat = (in["timeFormat"] | 0) == 1 ? 1 : 0;
  next.dateFormat = (in["dateFormat"] | 0) == 1 ? 1 : 0;
  const char* tz = in["tz"] | "";
  if (!*tz || strchr(tz, ' ') || !copyClean(next.tz, sizeof(next.tz), tz))
    return sendError("Time zone must be a POSIX TZ string, e.g. GMT0BST,M3.5.0/1,M10.5.0");

  JsonArray wifi = in["wifi"];
  if (wifi.size() > (size_t)kMaxWifi) return sendError("At most 10 Wi-Fi networks");
  next.wifiCount = 0;
  for (JsonObject w : wifi) {
    int n = next.wifiCount;
    if (!copyClean(next.ssid[n], sizeof(next.ssid[n]), w["ssid"] | "") || !next.ssid[n][0])
      return sendError("Wi-Fi network names must be 1–32 characters");
    const char* pass = w["pass"] | "";
    int orig = w["orig"] | -1;
    if (*pass) {
      if (!copyClean(next.pass[n], sizeof(next.pass[n]), pass)) return sendError("Wi-Fi password is too long");
    } else if (orig >= 0 && orig < gCurrent->wifiCount) {
      strlcpy(next.pass[n], gCurrent->pass[orig], sizeof(next.pass[n]));  // unchanged
    } else {
      next.pass[n][0] = 0;  // open network
    }
    next.wifiCount++;
  }

  if (!saveConfig(gIniPath, next)) return sendError("Could not write M5NS.INI to the SD card");
  // Re-read what was written, so the clock runs on exactly what's on the card.
  if (!loadConfig(gIniPath, saved)) return sendError("Saved, but could not read M5NS.INI back");

  bool restart = !sameNetworkSettings(saved, *gCurrent);
  logEvent("Settings saved (web)%s", restart ? ", restarting" : "");
  gHooks.applied(saved);
  if (restart) gRestartAt = millis() + 1500;  // let the response go out first

  JsonDocument out;
  out["ok"] = true;
  out["restart"] = restart;
  sendJson(200, out);
}

}  // namespace

void webBegin(const AppConfig& current, const char* iniPath, WebHooks hooks) {
  gCurrent = &current;
  gIniPath = iniPath;
  gHooks = hooks;
  gServer.on("/", HTTP_GET, handlePage);
  gServer.on("/api/config", HTTP_GET, handleGetConfig);
  gServer.on("/api/config", HTTP_POST, handlePostConfig);
  gServer.on("/api/status", HTTP_GET, handleStatus);
  gServer.onNotFound([] {
    gServer.sendHeader("Location", "/");
    gServer.send(302, "text/plain", "");
  });
  gServer.begin();
}

void webLoop() {
  gServer.handleClient();
  if (gRestartAt && (int32_t)(millis() - gRestartAt) >= 0) {
    delay(100);
    ESP.restart();
  }
}
