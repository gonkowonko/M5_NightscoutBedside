#include "timekeeping.h"

#include <Arduino.h>

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
