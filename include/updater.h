// Checks GitHub for new releases and installs them on request.
//
// Checks happen a minute after boot, every 2 days, and when asked (settings
// page). Nothing is installed automatically: the user presses Install on the
// settings page. Downloads are verified twice: HTTPS certificates against the
// embedded root CA bundle, and the file's SHA-256 against the digest GitHub
// publishes for the release asset. Only then does the clock switch over.
#pragma once

#include <stdint.h>
#include <time.h>

struct UpdateStatus {
  bool checking = false;
  bool installing = false;
  bool installed = false;    // verified and ready; the clock restarts into it
  bool available = false;    // latest release is newer than what's running
  char latest[16] = "";      // "1.3.0" (last successful check)
  char notes[700] = "";      // release notes, truncated
  time_t checkedAt = 0;      // epoch seconds of the last successful check
  uint32_t size = 0;         // bytes to download
  uint32_t done = 0;         // bytes downloaded so far
  char error[80] = "";       // last check or install error, "" if none
};

// Called from the network task while Wi-Fi is connected. Does scheduled and
// requested checks/installs; blocks for the duration of an install.
void updaterService();

// Requests from the UI (any task). They wake the network task.
void updaterRequestCheck();
bool updaterRequestInstall();  // false if no update is available or one is running

void updaterStatus(UpdateStatus& out);
bool updaterBusy();  // an install is in progress
