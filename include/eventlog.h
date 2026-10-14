// Small in-memory log of notable events (Wi-Fi, Nightscout, NTP, sensor,
// boot reason) for the log page. Safe to call from any task.
//
// Consecutive identical messages are collapsed into one entry with a count
// and the time of the first and last occurrence. Lost on reboot.
#pragma once

#include <stdint.h>
#include <stddef.h>

constexpr int kLogEntries = 13;  // what fits on the log page

struct LogEntry {
  uint32_t firstMs;  // millis() of the first occurrence
  uint32_t lastMs;   // millis() of the latest repeat
  uint16_t count;
  char text[40];
};

void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Copies up to `max` entries, newest first. Returns how many were copied.
int logSnapshot(LogEntry* out, int max);
