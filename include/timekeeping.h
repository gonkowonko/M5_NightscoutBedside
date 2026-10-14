// Local time from NTP using a POSIX TZ string (handles BST/GMT switching).
#pragma once

#include <time.h>

void timeSetup(const char* posixTz);  // call once at boot
void timeStartSync();                 // call once Wi-Fi is up (network task)
bool timeIsValid();                   // false until NTP has set the clock

// Formats into the caller's buffers. Before sync: "--:--" and "".
void formatClock(const tm& t, bool twelveHour, char* out, size_t len);
void formatDate(const tm& t, int dateFormat, char* out, size_t len);
