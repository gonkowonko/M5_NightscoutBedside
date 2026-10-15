#include "timekeeping.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "eventlog.h"

namespace {
char gTz[64] = "UTC0";
// Anything before 2024 means the clock has not been set since boot.
constexpr time_t kMinValidEpoch = 1704067200;
}  // namespace

void timeSetup(const char* posixTz) {
  strlcpy(gTz, posixTz, sizeof(gTz));
  setenv("TZ", gTz, 1);
  tzset();
}

void timeStartSync() {
  configTzTime(gTz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  sntp_set_time_sync_notification_cb([](struct timeval*) {
    static bool logged = false;  // NTP re-syncs hourly; only note the first
    if (!logged) logEvent("Time synced (NTP)");
    logged = true;
  });
}

void timeSeedFromHttpDate(const char* date) {
  if (timeIsValid() || !date || !*date) return;
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = "";
  int d, y, h, mi, s;
  if (sscanf(date, "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &s) != 6) return;
  const char* p = strstr(kMonths, mon);
  if (!p || strlen(mon) != 3) return;
  int m = (p - kMonths) / 3 + 1;
  // Days since 1970-01-01 for a Gregorian date (Howard Hinnant's algorithm).
  y -= m <= 2;
  int era = y / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + doe - 719468;
  timeval tv = {(time_t)(days * 86400L + h * 3600L + mi * 60L + s), 0};
  if (tv.tv_sec < kMinValidEpoch) return;
  settimeofday(&tv, nullptr);
  logEvent("Time set from server (NTP pending)");
}

bool timeIsValid() { return time(nullptr) > kMinValidEpoch; }

void formatClock(const tm& t, bool twelveHour, char* out, size_t len) {
  if (twelveHour) {
    int h = t.tm_hour % 12;
    snprintf(out, len, "%d:%02d", h == 0 ? 12 : h, t.tm_min);
  } else {
    snprintf(out, len, "%02d:%02d", t.tm_hour, t.tm_min);
  }
}

void formatDate(const tm& t, int dateFormat, char* out, size_t len) {
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (dateFormat == 1)
    snprintf(out, len, "%s %s %d", kDays[t.tm_wday], kMonths[t.tm_mon], t.tm_mday);
  else
    snprintf(out, len, "%s %d %s", kDays[t.tm_wday], t.tm_mday, kMonths[t.tm_mon]);
}
