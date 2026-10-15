#include "config.h"

#include <SD.h>

#include "IniFile.h"
#include "eventlog.h"
#include "initext.h"

namespace {

constexpr const char* kDefaultTz = "GMT0BST,M3.5.0/1,M10.5.0";
constexpr size_t kBufLen = 160;  // longest INI line we accept

// Thin helper around IniFile so each key is one line below.
class Reader {
 public:
  explicit Reader(IniFile& ini) : ini_(ini) {}

  bool str(const char* section, const char* key, char* out, size_t outLen) {
    if (!ini_.getValue(section, key, buf_, kBufLen)) return false;
    strlcpy(out, trim(buf_), outLen);
    return true;
  }
  bool integer(const char* key, int& out) {
    if (!ini_.getValue("config", key, buf_, kBufLen)) return false;
    out = atoi(buf_);
    return true;
  }
  bool number(const char* key, float& out) {
    if (!ini_.getValue("config", key, buf_, kBufLen)) return false;
    out = atof(buf_);
    return true;
  }
  // "HH:MM" -> minutes after midnight.
  bool hhmm(const char* key, int& out) {
    if (!ini_.getValue("config", key, buf_, kBufLen)) return false;
    int h = 0, m = 0;
    if (sscanf(buf_, "%d:%d", &h, &m) < 1 || h < 0 || h > 23 || m < 0 || m > 59) return false;
    out = h * 60 + m;
    return true;
  }

 private:
  static char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '"') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '"')) *--e = 0;
    return s;
  }

  IniFile& ini_;
  char buf_[kBufLen];
};

int clampPercent(int v, int fallback) { return (v < 0 || v > 100) ? fallback : v; }

}  // namespace

bool loadConfig(const char* path, AppConfig& cfg) {
  cfg = AppConfig();
  strlcpy(cfg.tz, kDefaultTz, sizeof(cfg.tz));

  IniFile ini(path);
  if (!ini.open()) {
    Serial.printf("config: %s not found\n", path);
    return false;
  }
  Reader r(ini);

  r.str("config", "nightscout", cfg.nightscout, sizeof(cfg.nightscout));
  r.str("config", "token", cfg.token, sizeof(cfg.token));

  int v = 0;
  if (r.integer("show_mgdl", v)) cfg.showMgdl = v == 1;
  r.integer("time_format", cfg.timeFormat);
  r.integer("date_format", cfg.dateFormat);
  for (int i = 0; i < 3; i++) {
    char key[16];
    snprintf(key, sizeof(key), "brightness%d", i + 1);
    if (r.integer(key, v)) cfg.brightness[i] = clampPercent(v, cfg.brightness[i]);
    if (cfg.brightness[i] == 0) cfg.brightness[i] = 1;
  }

  // Thresholds are written in the display unit, defaults are mmol/L.
  if (cfg.showMgdl) {
    cfg.yellowLow *= 18;
    cfg.yellowHigh *= 18;
    cfg.redLow *= 18;
    cfg.redHigh *= 18;
  }
  r.number("yellow_low", cfg.yellowLow);
  r.number("yellow_high", cfg.yellowHigh);
  r.number("red_low", cfg.redLow);
  r.number("red_high", cfg.redHigh);

  // Time zone: tz (POSIX) wins. Without it, M5_NightscoutMon's time_zone/dst
  // offsets are used. That firmware had no automatic summer time (users
  // changed dst by hand), so time_zone picks the matching European rule and
  // dst is ignored. Other offsets stay fixed.
  if (!r.str("config", "tz", cfg.tz, sizeof(cfg.tz)) || cfg.tz[0] == 0) {
    int zone = 0, dst = 0;
    bool hasZone = r.integer("time_zone", zone);
    bool hasDst = r.integer("dst", dst);
    if (hasZone && zone == 0) {
      strlcpy(cfg.tz, kDefaultTz, sizeof(cfg.tz));
    } else if (hasZone && zone == 3600) {
      strlcpy(cfg.tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(cfg.tz));
    } else if (hasZone && zone == 7200) {
      strlcpy(cfg.tz, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(cfg.tz));
    } else if (hasZone || hasDst) {
      // POSIX offsets are west-positive, so the sign is inverted.
      int total = zone + dst;
      int mins = abs(total) / 60;
      snprintf(cfg.tz, sizeof(cfg.tz), "UTC%c%d:%02d", total > 0 ? '-' : '+', mins / 60, mins % 60);
      logEvent("Fixed time zone: set it in settings");
    } else {
      strlcpy(cfg.tz, kDefaultTz, sizeof(cfg.tz));
    }
  }

  if (r.integer("stale_minutes", v) && v > 0) cfg.staleMinutes = v;
  r.number("temp_offset", cfg.tempOffset);
  if (r.integer("night_mode", v)) cfg.nightEnabled = v == 1;
  r.hhmm("night_start", cfg.nightStart);
  r.hhmm("night_end", cfg.nightEnd);
  if (r.integer("night_brightness", v)) cfg.nightBrightness = clampPercent(v, cfg.nightBrightness);

  for (int i = 0; i < kMaxWifi; i++) {
    char section[8];
    snprintf(section, sizeof(section), "wlan%d", i);
    char* ssid = cfg.ssid[cfg.wifiCount];
    if (!r.str(section, "ssid", ssid, sizeof(cfg.ssid[0])) || ssid[0] == 0) continue;
    r.str(section, "pass", cfg.pass[cfg.wifiCount], sizeof(cfg.pass[0]));
    cfg.wifiCount++;
  }

  ini.close();
  Serial.printf("config: nightscout=%s token=%s tz=%s wifi=%d unit=%s\n", cfg.nightscout,
                cfg.token[0] ? "(set)" : "(none)", cfg.tz, cfg.wifiCount,
                cfg.showMgdl ? "mg/dL" : "mmol/L");
  return true;
}

// ---- Writing ----------------------------------------------------------------

namespace {

String fmtHHMM(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "%02d:%02d", minutes / 60, minutes % 60);
  return b;
}

String fmtNumber(float v) {
  char b[16];
  snprintf(b, sizeof(b), "%g", v);
  return b;
}

bool copyFile(const char* from, const char* to) {
  File in = SD.open(from, FILE_READ);
  if (!in) return false;
  File out = SD.open(to, FILE_WRITE);
  if (!out) return false;
  uint8_t buf[256];
  while (int n = in.read(buf, sizeof(buf))) out.write(buf, n);
  return true;
}

}  // namespace

bool saveConfig(const char* path, const AppConfig& cfg) {
  IniText ini;
  if (File f = SD.open(path, FILE_READ)) ini.parse(f.readString());

  ini.set("config", "nightscout", cfg.nightscout);
  ini.set("config", "token", cfg.token);
  ini.set("config", "tz", cfg.tz);
  ini.set("config", "show_mgdl", cfg.showMgdl ? "1" : "0");
  ini.set("config", "time_format", String(cfg.timeFormat));
  ini.set("config", "date_format", String(cfg.dateFormat));
  ini.set("config", "yellow_low", fmtNumber(cfg.yellowLow));
  ini.set("config", "yellow_high", fmtNumber(cfg.yellowHigh));
  ini.set("config", "red_low", fmtNumber(cfg.redLow));
  ini.set("config", "red_high", fmtNumber(cfg.redHigh));
  ini.set("config", "brightness1", String(cfg.brightness[0]));
  ini.set("config", "brightness2", String(cfg.brightness[1]));
  ini.set("config", "brightness3", String(cfg.brightness[2]));
  ini.set("config", "stale_minutes", String(cfg.staleMinutes));
  ini.set("config", "temp_offset", fmtNumber(cfg.tempOffset));
  ini.set("config", "night_mode", cfg.nightEnabled ? "1" : "0");
  ini.set("config", "night_start", fmtHHMM(cfg.nightStart));
  ini.set("config", "night_end", fmtHHMM(cfg.nightEnd));
  ini.set("config", "night_brightness", String(cfg.nightBrightness));

  ini.removeSectionsStartingWith("wlan");
  for (int i = 0; i < cfg.wifiCount; i++) {
    char section[8];
    snprintf(section, sizeof(section), "wlan%d", i + 1);
    ini.set(section, "ssid", cfg.ssid[i]);
    ini.set(section, "pass", cfg.pass[i]);
  }

  // Write a temp file, keep the old one as M5NS.PRV, then swap in the new one.
  const char* tmp = "/M5NS.TMP";
  const char* prev = "/M5NS.PRV";
  SD.remove(tmp);
  {
    File out = SD.open(tmp, FILE_WRITE);
    if (!out) return false;
    String text = ini.render();
    if (out.print(text) != text.length()) return false;
    out.close();
  }
  if (SD.exists(path)) {
    SD.remove(prev);
    if (!copyFile(path, prev)) return false;
    SD.remove(path);
  }
  return SD.rename(tmp, path);
}
