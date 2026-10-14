// Glucose units, ranges and trends. Shared by the display and (later) alarms.
#pragma once

#include <math.h>
#include <string.h>

#include "config.h"

enum class BgRange : uint8_t { InRange, Warning, Urgent };

enum class Trend : uint8_t {
  None,
  DoubleUp,
  SingleUp,
  FortyFiveUp,
  Flat,
  FortyFiveDown,
  SingleDown,
  DoubleDown,
};

constexpr float kMgdlPerMmol = 18.0f;  // same factor the Nightscout web UI uses

// Value as shown on screen: mg/dL as-is, or mmol/L rounded to one decimal.
inline float toDisplayUnits(int mgdl, const AppConfig& cfg) {
  if (cfg.showMgdl) return mgdl;
  return roundf(mgdl / kMgdlPerMmol * 10.0f) / 10.0f;
}

// Classify the displayed value against the INI thresholds (display units).
inline BgRange classify(float value, const AppConfig& cfg) {
  if (value < cfg.redLow || value > cfg.redHigh) return BgRange::Urgent;
  if (value < cfg.yellowLow || value > cfg.yellowHigh) return BgRange::Warning;
  return BgRange::InRange;
}

inline Trend parseTrend(const char* direction) {
  static const struct { const char* name; Trend trend; } kMap[] = {
      {"DoubleUp", Trend::DoubleUp},           {"SingleUp", Trend::SingleUp},
      {"FortyFiveUp", Trend::FortyFiveUp},     {"Flat", Trend::Flat},
      {"FortyFiveDown", Trend::FortyFiveDown}, {"SingleDown", Trend::SingleDown},
      {"DoubleDown", Trend::DoubleDown},
  };
  for (const auto& m : kMap)
    if (strcmp(direction, m.name) == 0) return m.trend;
  return Trend::None;
}
