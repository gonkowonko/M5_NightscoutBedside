# Changelog

## 1.2.0 — 2026-10-15

First public release.

- **Install from your browser** at https://gonkowonko.github.io/M5_NightscoutBedside/. Release
  builds are made by GitHub Actions.
- Pressing **C** shows "Refreshing…" until the fetch finishes, or "Refresh failed" in red.
- **Temperature correction** setting (`temp_offset`), shown on the settings page as a picker. The
  temperature is now shown to one decimal place.
- The date and temperature use a larger font.
- Fonts are built into the firmware, so the SD card only needs `M5NS.INI`.
- Build platform pinned (espressif32 7.1.3) for reproducible builds.

## 1.1.0 — 2026-10-14

- **Firmware updates over Wi-Fi**, from PlatformIO or by uploading a file on the settings page. This
  changes the flash layout to two app slots, so the first install needs USB.
- **Settings page** at `http://m5clock.local`: it edits `M5NS.INI` in place and applies display
  settings immediately.
- Night dimming is opt-in (`night_mode`), and the brightness level picked with button A is remembered.
- **Event log page** (button B): Wi-Fi, Nightscout, NTP, sensor and the reason for each restart.
- Fresh readings show "just now", and the divider is a neutral grey.

## 1.0.0 — 2026-10-14

- First version: the bedside clock with Nightscout glucose, trend arrow, delta and age, a stale
  warning, and the DHT12 room temperature. It reads M5_NightscoutMon's `M5NS.INI`.
