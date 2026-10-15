// M5 Bedside Clock - a Nightscout bedside clock for the original M5Stack Basic.
// Copyright (C) 2026 Gary Dalley. Licensed under the GNU GPL v3 (see LICENSE).
//
// Wiring of the modules:
//   config      - reads M5NS.INI from the SD card
//   network     - background task: Wi-Fi, NTP, Nightscout fetch every 60 s
//   nightscout  - HTTPS request + JSON parsing
//   sensor      - DHT12 temperature in the standing base
//   display     - flicker-free rendering of the clock, status and log pages
//   eventlog    - in-memory log of Wi-Fi/Nightscout/NTP/sensor events
//   webconfig   - settings page at http://m5clock.local, edits M5NS.INI
//   ota         - firmware updates over Wi-Fi (PlatformIO or the settings page)
//   main        - glue: buttons, brightness, building what the screen shows
//
// Future alarms belong next to buildModel(): they need the same reading,
// age and range classification (see glucose.h).

#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "glucose.h"
#include "network.h"
#include "sensor.h"
#include "timekeeping.h"
#include "updater.h"
#include "ota.h"
#include "version.h"
#include "webconfig.h"

// M5GFX copies one glyph bitmap onto the stack while drawing VLW fonts
// (the clock digits are ~6 KB each), so give the loop task some headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);


namespace {

constexpr const char* kIniPath = "/M5NS.INI";
constexpr uint32_t kWakeMs = 30 * 1000;  // full brightness after a press at night
constexpr uint32_t kFrameMs = 200;

AppConfig gCfg;
enum class Page { Clock, Status, Log };
Page gPage = Page::Clock;  // cycled with button B
int gLevel = 0;                // index into gCfg.brightness (button A)
uint32_t gLastPress = 0;
bool gPressedOnce = false;
int gAppliedBrightness = -1;

bool mountSd() {
  // The SD card shares the SPI bus with the LCD (CS on GPIO 4). It only holds
  // M5NS.INI; saving settings opens up to 3 files at once.
  return SD.begin(GPIO_NUM_4, SPI, 25000000, "/sd", 4);
}

// Between night_start and night_end (used by night dimming and night colours).
bool inNightHours() {
  if (!timeIsValid() || gCfg.nightStart == gCfg.nightEnd) return false;
  time_t t = time(nullptr);
  tm lt;
  localtime_r(&t, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min;
  if (gCfg.nightStart < gCfg.nightEnd) return m >= gCfg.nightStart && m < gCfg.nightEnd;
  return m >= gCfg.nightStart || m < gCfg.nightEnd;  // spans midnight
}

bool isDimmed(uint32_t now) {
  bool awake = gPressedOnce && now - gLastPress < kWakeMs;
  return gCfg.nightEnabled && inNightHours() && !awake;
}

int targetBrightness(uint32_t now) {
  return isDimmed(now) ? gCfg.nightBrightness : gCfg.brightness[gLevel];
}

void applyBrightness(uint32_t now) {
  int percent = targetBrightness(now);
  if (percent == gAppliedBrightness) return;
  gAppliedBrightness = percent;
  M5.Display.setBrightness(percent * 255 / 100);
}

// The brightness level picked with button A survives restarts (kept in the
// ESP32's NVS, not on the SD card).
void loadLevel() {
  Preferences prefs;
  if (prefs.begin("clock", true)) {
    gLevel = prefs.getInt("level", 0) % 3;
    prefs.end();
  }
}

void saveLevel() {
  Preferences prefs;
  if (prefs.begin("clock", false)) {
    prefs.putInt("level", gLevel);
    prefs.end();
  }
}

void startRefresh(uint32_t now);  // button C, defined below

void handleButtons(uint32_t now) {
  bool a = M5.BtnA.wasPressed();
  bool b = M5.BtnB.wasPressed();
  bool c = M5.BtnC.wasPressed();
  if (!a && !b && !c) return;

  // While dimmed for the night, the first press only lights the screen up.
  bool wasDimmed = isDimmed(now);
  gLastPress = now;
  gPressedOnce = true;
  if (wasDimmed) return;

  if (a) {
    gLevel = (gLevel + 1) % 3;
    saveLevel();
  }
  if (b) {
    gPage = gPage == Page::Clock ? Page::Status : gPage == Page::Status ? Page::Log : Page::Clock;
    displayInvalidate();
  }
  if (c) startRefresh(now);
}

void formatAge(int minutes, char* out, size_t len) {
  if (minutes < 1)
    strlcpy(out, "just now", len);
  else
    snprintf(out, len, "%d min ago", minutes);
}

ScreenModel buildModel() {
  ScreenModel m;
  time_t now = time(nullptr);
  bool timeOk = timeIsValid();
  m.night = gCfg.nightColours == 1 || (gCfg.nightColours == 2 && inNightHours());

  if (timeOk) {
    tm lt;
    localtime_r(&now, &lt);
    formatClock(lt, gCfg.timeFormat == 1, m.time, sizeof(m.time));
    formatDate(lt, gCfg.dateFormat, m.date, sizeof(m.date));
  } else {
    strlcpy(m.time, "--:--", sizeof(m.time));
  }

  if (sensorFound())
    snprintf(m.temp, sizeof(m.temp), "%.1f\xC2\xB0" "C", sensorTemperature() + gCfg.tempOffset);

  NsReading r;
  networkLatest(r);
  if (!r.valid) return m;

  m.hasBg = true;
  float value = toDisplayUnits(r.sgv, gCfg);
  m.range = classify(value, gCfg);
  m.trend = parseTrend(r.direction);
  if (gCfg.showMgdl)
    snprintf(m.bg, sizeof(m.bg), "%d", r.sgv);
  else
    snprintf(m.bg, sizeof(m.bg), "%.1f", value);

  if (r.hasPrev) {
    // Difference of the values as displayed, so the numbers on screen add up.
    float delta = value - toDisplayUnits(r.prevSgv, gCfg);
    if (gCfg.showMgdl)
      snprintf(m.delta, sizeof(m.delta), delta == 0 ? "%.0f" : "%+.0f", delta);
    else if (fabsf(delta) < 0.05f)
      strlcpy(m.delta, "0.0", sizeof(m.delta));
    else
      snprintf(m.delta, sizeof(m.delta), "%+.1f", delta);
  }

  if (timeOk) {
    int64_t ageMs = (int64_t)now * 1000 - r.dateMs;
    int minutes = ageMs > 0 ? (int)(ageMs / 60000) : 0;
    formatAge(minutes, m.age, sizeof(m.age));
    m.stale = minutes > gCfg.staleMinutes;
    m.ageAlert = m.stale;
  }
  return m;
}

// An update from GitHub is being installed: show progress instead of the
// normal page, and restart once it's verified. Returns true while it owns the screen.
bool showUpdateProgress(uint32_t now) {
  static uint32_t installedAt = 0;
  static bool showing = false;
  UpdateStatus u;
  updaterStatus(u);
  if (!u.installing && !u.installed) {
    if (showing) displayInvalidate();  // install failed: repaint the current page
    showing = false;
    return false;
  }
  showing = true;
  char detail[48];
  if (u.installed) {
    if (!installedAt) installedAt = now;
    displayProgress("Update installed", "Restarting...", 100);
    if (now - installedAt > 2000) ESP.restart();
    return true;
  }
  int pct = u.size ? (int)((uint64_t)u.done * 100 / u.size) : 0;
  snprintf(detail, sizeof(detail), "v%s  -  %d%%  -  don't unplug", u.latest, pct);
  displayProgress("Updating firmware", detail, pct);
  return true;
}

// ---- Manual refresh (button C) ----------------------------------------------
// The age line shows "Refreshing…" until the network task finishes a fetch,
// then "Refresh failed" in red for a few seconds if it didn't work.

constexpr uint32_t kRefreshTimeoutMs = 15 * 1000;
constexpr uint32_t kFailedNoteMs = 3 * 1000;
bool gRefreshing = false;
uint32_t gRefreshStart = 0;
uint32_t gRefreshBaseCount = 0;
uint32_t gFailedUntil = 0;

void startRefresh(uint32_t now) {
  NetStatus net;
  networkStatus(net);
  gRefreshBaseCount = net.fetchCount;
  gRefreshStart = now;
  gRefreshing = true;
  gFailedUntil = 0;
  networkRequestRefresh();
}

void applyRefreshNote(ScreenModel& m) {
  uint32_t now = millis();
  if (gRefreshing) {
    NetStatus net;
    networkStatus(net);
    if (net.fetchCount != gRefreshBaseCount) {
      gRefreshing = false;
      if (!net.lastFetchOk) gFailedUntil = now + kFailedNoteMs;
    } else if (now - gRefreshStart > kRefreshTimeoutMs) {
      gRefreshing = false;  // e.g. no Wi-Fi, so no fetch happened
      gFailedUntil = now + kFailedNoteMs;
    }
  }
  if (gRefreshing) {
    strlcpy(m.age, "Refreshing\xE2\x80\xA6", sizeof(m.age));
    m.ageAlert = false;
  } else if (gFailedUntil && (int32_t)(gFailedUntil - now) > 0) {
    strlcpy(m.age, "Refresh failed", sizeof(m.age));
    m.ageAlert = true;
  }
}

ScreenModel buildScreen() {
  ScreenModel m = buildModel();
  applyRefreshNote(m);
  return m;
}

void showStatusPage(uint32_t now) {
  static char buf[14][48];
  const char* lines[14];
  int n = 0;
  auto line = [&](const char* fmt, auto... args) {
    snprintf(buf[n], sizeof(buf[n]), fmt, args...);
    lines[n] = buf[n];
    n++;
  };

  NetStatus net;
  networkStatus(net);
  NsReading r;
  networkLatest(r);
  time_t t = time(nullptr);
  tm lt;
  localtime_r(&t, &lt);

  line("Status  v%s   (B: log)", FW_VERSION);
  if (net.connected)
    line("Wi-Fi: %s  %d dBm", net.ssid, net.rssi);
  else
    line("Wi-Fi: not connected (%d configured)", gCfg.wifiCount);
  line("Web: %s  %s.local", net.connected ? net.ip : "-", kMdnsName);
  line("NS: %s", gCfg.nightscout[0] ? gCfg.nightscout : "(not set)");
  if (!net.everFetched) {
    line("Last fetch: never");
  } else if (net.lastFetchTime) {
    tm ft;
    localtime_r(&net.lastFetchTime, &ft);
    line("Last fetch: %02d:%02d:%02d (%lus ago)", ft.tm_hour, ft.tm_min, ft.tm_sec,
         (unsigned long)((now - net.lastFetchMillis) / 1000));
  } else {
    line("Last fetch: %lus ago", (unsigned long)((now - net.lastFetchMillis) / 1000));
  }
  line("Result: %s", net.lastResult);
  if (r.valid)
    line("Reading: %d mg/dL %s, %ld min old", r.sgv, r.direction,
         timeIsValid() ? (long)(((int64_t)t * 1000 - r.dateMs) / 60000) : -1L);
  else
    line("Reading: none yet");
  if (timeIsValid())
    line("Time: %02d:%02d:%02d  TZ %s", lt.tm_hour, lt.tm_min, lt.tm_sec, gCfg.tz);
  else
    line("Time: not synced  TZ %s", gCfg.tz);
  line("Brightness: %d%% (level %d)%s", targetBrightness(now), gLevel + 1,
       !gCfg.nightEnabled ? "" : inNightHours() ? ", night dim on" : ", night dim off-hours");
  if (sensorFound())
    line("Sensor: %.1f C (raw %.1f), %.0f %%RH", sensorTemperature() + gCfg.tempOffset,
         sensorTemperature(), sensorHumidity());
  else
    line("Sensor: DHT12 not found");
  line("Heap: %u free, %u min, %u max block", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
       ESP.getMaxAllocHeap());
  UpdateStatus upd;
  updaterStatus(upd);
  if (upd.installing)
    line("Update: installing v%s", upd.latest);
  else if (upd.available)
    line("Update: v%s available (settings page)", upd.latest);
  else if (upd.error[0])
    line("Update: %s", upd.error);
  else if (upd.checkedAt) {
    tm ct;
    localtime_r(&upd.checkedAt, &ct);
    line("Update: up to date (checked %02d:%02d)", ct.tm_hour, ct.tm_min);
  } else {
    line("Update: not checked yet");
  }
  uint32_t up = now / 1000;
  line("Uptime: %lud %02luh %02lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
       (unsigned long)(up / 60 % 60));

  displayDebug(lines, n);
}

// "02:10", or "Tue 02:10" if not today, or "+12s" (since boot) before NTP.
void formatLogTime(uint32_t atMs, uint32_t nowMs, char* out, size_t len) {
  if (!timeIsValid()) {
    snprintf(out, len, "+%lus", (unsigned long)(atMs / 1000));
    return;
  }
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  time_t now = time(nullptr);
  time_t at = now - (time_t)((nowMs - atMs) / 1000);
  tm today, t;
  localtime_r(&now, &today);
  localtime_r(&at, &t);
  if (t.tm_yday == today.tm_yday && t.tm_year == today.tm_year)
    snprintf(out, len, "%02d:%02d", t.tm_hour, t.tm_min);
  else
    snprintf(out, len, "%s %02d:%02d", kDays[t.tm_wday], t.tm_hour, t.tm_min);
}

void showLogPage(uint32_t now) {
  static char buf[kLogEntries + 1][48];
  const char* lines[kLogEntries + 1];
  LogEntry entries[kLogEntries];
  int n = logSnapshot(entries, kLogEntries);

  strlcpy(buf[0], "Log  newest first   (B: clock)", sizeof(buf[0]));
  lines[0] = buf[0];
  for (int i = 0; i < n; i++) {
    const LogEntry& e = entries[i];
    char first[12], last[12];
    formatLogTime(e.firstMs, now, first, sizeof(first));
    if (e.count > 1) {
      formatLogTime(e.lastMs, now, last, sizeof(last));
      // "02:10-02:24 NS HTTP 502 x14"; the range is dropped if it's one minute.
      if (strcmp(first, last) == 0)
        snprintf(buf[i + 1], sizeof(buf[i + 1]), "%s %s x%u", first, e.text, e.count);
      else
        snprintf(buf[i + 1], sizeof(buf[i + 1]), "%s-%s %s x%u", first, last, e.text, e.count);
    } else {
      snprintf(buf[i + 1], sizeof(buf[i + 1]), "%s %s", first, e.text);
    }
    lines[i + 1] = buf[i + 1];
  }
  displayDebug(lines, n + 1);
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "wake from sleep";
    case ESP_RST_EXT: return "reset button";
    default: return "unknown reason";
  }
}

// Events that main can see best: NTP sync and readings going stale.
void watchEvents(uint32_t now) {
  static uint32_t lastCheck = 0;
  static bool timeWasValid = false;
  static bool wasStale = false;
  if (now - lastCheck < 1000) return;
  lastCheck = now;

  timeWasValid = timeIsValid();
  if (!timeWasValid) return;

  NsReading r;
  networkLatest(r);
  if (!r.valid) return;
  int64_t ageMs = (int64_t)time(nullptr) * 1000 - r.dateMs;
  bool stale = ageMs / 60000 > gCfg.staleMinutes;  // same rule as the screen
  if (stale && !wasStale) {
    NetStatus net;
    networkStatus(net);
    if (net.lastFetchOk)
      logEvent("Stale: Nightscout has no new data");
    else
      logEvent("Stale: fetches failing");
  } else if (!stale && wasStale) {
    logEvent("Data current again");
  }
  wasStale = stale;
}

// Settings saved from the web page. Network settings (Nightscout, token,
// Wi-Fi) are left alone: the web module restarts the clock for those.
void applySettings(const AppConfig& s) {
  if (strcmp(s.tz, gCfg.tz) != 0) {
    strlcpy(gCfg.tz, s.tz, sizeof(gCfg.tz));
    timeSetup(gCfg.tz);
  loadLevel();
  }
  gCfg.showMgdl = s.showMgdl;
  gCfg.timeFormat = s.timeFormat;
  gCfg.dateFormat = s.dateFormat;
  memcpy(gCfg.brightness, s.brightness, sizeof(gCfg.brightness));
  gCfg.yellowLow = s.yellowLow;
  gCfg.yellowHigh = s.yellowHigh;
  gCfg.redLow = s.redLow;
  gCfg.redHigh = s.redHigh;
  gCfg.staleMinutes = s.staleMinutes;
  gCfg.tempOffset = s.tempOffset;
  gCfg.nightEnabled = s.nightEnabled;
  gCfg.nightStart = s.nightStart;
  gCfg.nightEnd = s.nightEnd;
  gCfg.nightBrightness = s.nightBrightness;
  gCfg.nightColours = s.nightColours;
  gAppliedBrightness = -1;  // re-evaluate on the next loop
  displayInvalidate();
}

// ---- Startup splash -----------------------------------------------------------
// Shown from power-on until the first reading arrives (plus a moment to see
// the ticks), for at most 30 s, or until any button is pressed.

constexpr uint32_t kSplashTimeoutMs = 30 * 1000;
constexpr uint32_t kSplashReadyHoldMs = 2500;
bool gSplashActive = true;
uint32_t gSplashReadyAt = 0;

void setStep(SplashStep& s, StepState state, const char* detail = "") {
  s.state = state;
  strlcpy(s.detail, detail, sizeof(s.detail));
}

SplashModel baseSplash() {
  SplashModel m;
  const char* labels[] = {"Settings", "Wi-Fi", "Time", "Nightscout"};
  for (int i = 0; i < 4; i++) strlcpy(m.steps[i].label, labels[i], sizeof(m.steps[i].label));
  strlcpy(m.footer, "Starting up\xE2\x80\xA6", sizeof(m.footer));
  return m;
}

void setFooter(SplashModel& m, const char* text, bool alert) {
  if (m.footerAlert) return;  // the first problem keeps the footer
  strlcpy(m.footer, text, sizeof(m.footer));
  m.footerAlert = alert;
}

SplashModel buildSplash(uint32_t now) {
  SplashModel m = baseSplash();
  m.frame = (now / 350) % 3;
  NetStatus net;
  networkStatus(net);
  NsReading r;
  networkLatest(r);
  char buf[40];

  setStep(m.steps[0], StepState::Ok, "M5NS.INI");

  if (gCfg.wifiCount == 0) {
    setStep(m.steps[1], StepState::Fail, "not set");
    setFooter(m, "Add your Wi-Fi network to M5NS.INI", true);
  } else if (net.connected) {
    strlcpy(buf, net.ssid, 23);  // fits the line; longer names get an ellipsis
    if (strlen(net.ssid) > 22) strlcat(buf, "\xE2\x80\xA6", sizeof(buf));
    setStep(m.steps[1], StepState::Ok, buf);
  } else if (net.wifiProblem[0]) {
    setStep(m.steps[1], StepState::Fail, net.wifiProblem);
    setFooter(m, strstr(net.wifiProblem, "password") ? "Check the Wi-Fi password in M5NS.INI"
                 : strstr(net.wifiProblem, "found")  ? "Check the Wi-Fi name in M5NS.INI"
                                                     : "Can't connect to Wi-Fi, retrying",
              true);
  } else {
    setStep(m.steps[1], StepState::Wait);
  }

  if (timeIsValid()) {
    time_t t = time(nullptr);
    tm lt;
    localtime_r(&t, &lt);
    formatClock(lt, gCfg.timeFormat == 1, buf, sizeof(buf));
    setStep(m.steps[2], StepState::Ok, buf);
  } else if (net.connected) {
    setStep(m.steps[2], StepState::Wait);
  }

  if (r.valid) {
    float v = toDisplayUnits(r.sgv, gCfg);
    if (gCfg.showMgdl)
      snprintf(buf, sizeof(buf), "%d mg/dL", r.sgv);
    else
      snprintf(buf, sizeof(buf), "%.1f mmol/L", v);
    setStep(m.steps[3], StepState::Ok, buf);
  } else if (!gCfg.nightscout[0]) {
    setStep(m.steps[3], StepState::Fail, "not set");
    setFooter(m, "Add your Nightscout address to M5NS.INI", true);
  } else if (net.everFetched && !net.lastFetchOk) {
    strlcpy(buf, net.lastResult, 22);
    setStep(m.steps[3], StepState::Fail, buf);
    setFooter(m, strstr(net.lastResult, "401") ? "Check the Nightscout token in M5NS.INI"
                                               : "Can't reach Nightscout, retrying",
              true);
  } else if (net.connected) {
    setStep(m.steps[3], StepState::Wait);
  }

  if (net.connected) {
    snprintf(buf, sizeof(buf), "Settings: %s.local \xC2\xB7 %s", kMdnsName, net.ip);
    setFooter(m, buf, false);
  }
  return m;
}

// True once the splash should give way to the clock.
bool splashFinished(const SplashModel& m, uint32_t now) {
  bool allOk = true;
  for (const SplashStep& s : m.steps) allOk &= s.state == StepState::Ok;
  if (allOk && !gSplashReadyAt) gSplashReadyAt = now;
  if (gSplashReadyAt && now - gSplashReadyAt >= kSplashReadyHoldMs) return true;
  return now >= kSplashTimeoutMs;
}

}  // namespace

void setup() {
  auto m5cfg = M5.config();
  m5cfg.serial_baudrate = 115200;
  m5cfg.internal_spk = false;  // no sound yet; keeps the DAC quiet at night
  m5cfg.internal_mic = false;
  M5.begin(m5cfg);
  M5.Display.setBrightness(gCfg.brightness[0] * 255 / 100);
  Serial.printf("\nM5 Bedside Clock %s\n", FW_VERSION);
  logEvent("Boot v%s: %s", FW_VERSION, resetReason());

  // Fonts live in flash, so the splash can show before the SD card is read.
  displayBegin();
  SplashModel boot = baseSplash();
  setStep(boot.steps[0], StepState::Wait);
  displaySplash(boot, FW_VERSION);
  while (!mountSd()) {
    setStep(boot.steps[0], StepState::Fail, "no SD card");
    setFooter(boot, "Insert the SD card with M5NS.INI", true);
    displaySplash(boot, FW_VERSION);
    delay(2000);
  }
  while (!loadConfig(kIniPath, gCfg)) {
    setStep(boot.steps[0], StepState::Fail, "M5NS.INI missing");
    setFooter(boot, "Put M5NS.INI in the root of the SD card", true);
    displaySplash(boot, FW_VERSION);
    delay(3000);
    SD.end();
    mountSd();
  }

  timeSetup(gCfg.tz);
  loadLevel();
  if (gCfg.wifiCount == 0) logEvent("No Wi-Fi networks in M5NS.INI");
  networkBegin(gCfg);
  webBegin(gCfg, kIniPath, {applySettings, buildScreen});
  sensorBegin();
  sensorUpdate(millis());
  applyBrightness(millis());
}

void loop() {
  M5.update();
  webLoop();
  otaLoop();
  uint32_t now = millis();
  if (gSplashActive) {
    // Any button skips the splash (and does nothing else).
    bool pressed = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
    SplashModel splash = buildSplash(now);
    if (pressed || splashFinished(splash, now)) {
      gSplashActive = false;
      displayInvalidate();
    } else {
      static uint32_t lastSplash = 0;
      if (now - lastSplash >= 100) {
        lastSplash = now;
        displaySplash(splash, FW_VERSION);
      }
    }
  } else {
    handleButtons(now);
  }
  sensorUpdate(now);
  applyBrightness(now);
  watchEvents(now);

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= kFrameMs) {
    lastFrame = now;
    if (showUpdateProgress(now)) return;
    if (gSplashActive) return;
    switch (gPage) {
      case Page::Clock: displayUpdate(buildScreen()); break;
      case Page::Status: showStatusPage(now); break;
      case Page::Log: showLogPage(now); break;
    }
  }
  delay(10);
}
