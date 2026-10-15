# Changelog

## 1.4.0 — 2026-10-15

- **Startup splash:** a moon logo, the version, and a live checklist (Settings → Wi-Fi → Time →
  Nightscout) that ticks off as the clock starts. A problem shows a red ✗ and a one-line hint, such as
  "Check the Wi-Fi password in M5NS.INI". The splash switches to the clock 2.5 s after the first
  reading, or after 30 s, or when you press any button.
- Missing SD card or `M5NS.INI` now shows on the splash instead of as a plain message.
- The log notes when NTP first sets the time ("Time synced (NTP)").
- The splash shows just the Wi-Fi network name (signal strength is on the status page).

## 1.3.2 — 2026-10-15

- Settings page: the Save bar stays clear of the iPhone's rounded corners, home bar and Safari's toolbar
  (safe-area padding), so the Save button is never clipped.

## 1.3.1 — 2026-10-15

- Settings page works properly on phones:
  - Brightness levels and night times sit three across instead of wrapping.
  - Time fields no longer overflow on iPhone.
  - Wi-Fi name and password are stacked.
  - The Firmware section's button is full width.
  - Spacing is fixed above "Dim at night" and "Time zone".
  - Links are green.
- Browser installer: the clock image is no longer stretched on phones.

## 1.3.0 — 2026-10-15

- **Update checks:** the clock checks GitHub for a new release a minute after starting and every 2 days.
  New versions appear in the Firmware section of the settings page, with release notes and an
  **Install** button. Nothing installs without you pressing it.
- Downloads are verified with HTTPS certificate checking (Mozilla's trusted roots, embedded) and the
  SHA-256 checksum GitHub publishes for the release file. A failed or tampered download is rejected
  and the clock keeps running its current firmware.
- Progress shows on the clock during an install, and the status page shows update state.
- The settings page's file upload is now under "Install from a file".

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
