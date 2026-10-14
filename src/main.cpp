// M5 Bedside Clock - a Nightscout bedside clock for the original M5Stack Basic.
// Licensed under the GNU GPL v3 (see LICENSE).
//
// Wiring of the modules:
//   config      - reads M5NS.INI from the SD card
//   network     - background task: Wi-Fi, NTP, Nightscout fetch every 60 s
//   nightscout  - HTTPS request + JSON parsing
//   sensor      - DHT12 temperature in the standing base
//   display     - flicker-free rendering of the clock, status and log pages
//   eventlog    - in-memory log of Wi-Fi/Nightscout/NTP/sensor events
//   main        - glue: buttons, brightness, building what the screen shows
//
// Future alarms belong next to buildModel(): they need the same reading,
// age and range classification (see glucose.h).

#include <M5Unified.h>
#include <SD.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "glucose.h"
#include "network.h"
#include "sensor.h"
#include "timekeeping.h"

// M5GFX copies one glyph bitmap onto the stack while drawing VLW fonts
// (the clock digits are ~6 KB each), so give the loop task some headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#define FW_VERSION "1.0.0"

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
  // The SD card shares the SPI bus with the LCD (CS on GPIO 4). 6 open files:
  // 5 fonts stay open for glyph streaming, plus one for the INI.
  return SD.begin(GPIO_NUM_4, SPI, 25000000, "/sd", 6);
}

bool isNight() {
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
  return isNight() && !awake;
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

  if (a) gLevel = (gLevel + 1) % 3;
  if (b) {
    gPage = gPage == Page::Clock ? Page::Status : gPage == Page::Status ? Page::Log : Page::Clock;
    displayInvalidate();
  }
  if (c) networkRequestRefresh();
}

void formatAge(int minutes, char* out, size_t len) { snprintf(out, len, "%d min ago", minutes); }

ScreenModel buildModel() {
  ScreenModel m;
  time_t now = time(nullptr);
  bool timeOk = timeIsValid();

  if (timeOk) {
    tm lt;
    localtime_r(&now, &lt);
    formatClock(lt, gCfg.timeFormat == 1, m.time, sizeof(m.time));
    formatDate(lt, gCfg.dateFormat, m.date, sizeof(m.date));
  } else {
    strlcpy(m.time, "--:--", sizeof(m.time));
  }

  if (sensorFound()) snprintf(m.temp, sizeof(m.temp), "%ld\xC2\xB0" "C", lroundf(sensorTemperature()));

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
  }
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
  line("IP: %s", net.connected ? net.ip : "-");
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
  line("Brightness: %d%%%s", targetBrightness(now), isNight() ? " (night hours)" : "");
  if (sensorFound())
    line("Sensor: DHT12 found, %.1f C, %.0f %%RH", sensorTemperature(), sensorHumidity());
  else
    line("Sensor: DHT12 not found");
  line("Fonts: %d/5 loaded from SD /fonts", displayFontsLoaded());
  line("Heap: %u free, %u min, %u max block", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
       ESP.getMaxAllocHeap());
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

  if (timeIsValid() && !timeWasValid) logEvent("Time synced (NTP)");
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

  displayMessage("Bedside Clock", "Starting...");
  while (!mountSd()) {
    displayMessage("No SD card", "Insert the card with M5NS.INI");
    delay(2000);
  }
  while (!loadConfig(kIniPath, gCfg)) {
    displayMessage("M5NS.INI not found", "Put M5NS.INI in the root of the SD card");
    delay(3000);
    SD.end();
    mountSd();
  }

  timeSetup(gCfg.tz);
  displayBegin();
  if (displayFontsLoaded() < 5) logEvent("Fonts: only %d/5 found in /fonts", displayFontsLoaded());
  if (gCfg.wifiCount == 0) logEvent("No Wi-Fi networks in M5NS.INI");
  networkBegin(gCfg);
  sensorUpdate(millis());
  applyBrightness(millis());
}

void loop() {
  M5.update();
  uint32_t now = millis();
  handleButtons(now);
  sensorUpdate(now);
  applyBrightness(now);
  watchEvents(now);

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= kFrameMs) {
    lastFrame = now;
    switch (gPage) {
      case Page::Clock: displayUpdate(buildModel()); break;
      case Page::Status: showStatusPage(now); break;
      case Page::Log: showLogPage(now); break;
    }
  }
  delay(10);
}
