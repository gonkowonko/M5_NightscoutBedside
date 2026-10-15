// Local time from NTP using a POSIX TZ string (handles BST/GMT switching).
#pragma once

#include <time.h>

void timeSetup(const char* posixTz);  // call once at boot
void timeStartSync();                 // call once Wi-Fi is up (network task)
bool timeIsValid();                   // false until the clock has been set

// Sets the clock from an HTTP Date header ("Wed, 15 Oct 2026 08:57:12 GMT")
// if it isn't set yet. NTP's first answer can take several seconds; any
// HTTPS reply arrives sooner, and NTP refines the time when it comes in.
void timeSeedFromHttpDate(const char* date);

// Formats into the caller's buffers. Before sync: "--:--" and "".
void formatClock(const tm& t, bool twelveHour, char* out, size_t len);
void formatDate(const tm& t, int dateFormat, char* out, size_t len);
