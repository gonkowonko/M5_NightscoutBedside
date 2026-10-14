#include "eventlog.h"

#include <Arduino.h>

#include <stdarg.h>

namespace {

LogEntry gEntries[kLogEntries];  // ring buffer
int gNext = 0;                   // slot for the next new entry
int gCount = 0;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

void logEvent(const char* fmt, ...) {
  char text[sizeof(LogEntry::text)];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  Serial.printf("log: %s\n", text);

  uint32_t now = millis();
  portENTER_CRITICAL(&gMux);
  LogEntry* newest = gCount ? &gEntries[(gNext + kLogEntries - 1) % kLogEntries] : nullptr;
  if (newest && strcmp(newest->text, text) == 0) {
    newest->lastMs = now;
    if (newest->count < UINT16_MAX) newest->count++;
  } else {
    LogEntry& e = gEntries[gNext];
    e.firstMs = e.lastMs = now;
    e.count = 1;
    strlcpy(e.text, text, sizeof(e.text));
    gNext = (gNext + 1) % kLogEntries;
    if (gCount < kLogEntries) gCount++;
  }
  portEXIT_CRITICAL(&gMux);
}

int logSnapshot(LogEntry* out, int max) {
  portENTER_CRITICAL(&gMux);
  int n = gCount < max ? gCount : max;
  for (int i = 0; i < n; i++) out[i] = gEntries[(gNext + kLogEntries - 1 - i) % kLogEntries];
  portEXIT_CRITICAL(&gMux);
  return n;
}
