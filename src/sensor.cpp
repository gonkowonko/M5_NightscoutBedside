// DHT12 reading adapted from the DHT12 library by Bobadas (public domain) as
// used in mlukasek/M5_NightscoutMon. It uses M5Unified's internal I2C bus,
// which on the Basic Core is the same bus (SDA 21 / SCL 22) as Port A.
#include "sensor.h"

#include <M5Unified.h>

#include "eventlog.h"

namespace {

constexpr uint8_t kAddress = 0x5C;
constexpr uint32_t kFreq = 100000;
constexpr uint32_t kIntervalMs = 30 * 1000;

bool gFound = false;
int gFailures = 0;
bool gReadOnce = false;
uint32_t gLastRead = 0;
float gTemp = 0;
float gHum = 0;

bool readDht12() {
  // Same sequence as the original library: set register 0, stop, read 5 bytes.
  auto& bus = M5.In_I2C;
  uint8_t d[5];
  if (!bus.start(kAddress, false, kFreq) || !bus.write(0x00)) {
    bus.stop();
    return false;
  }
  bus.stop();
  bool ok = bus.start(kAddress, true, kFreq) && bus.read(d, sizeof(d), true);
  bus.stop();
  if (!ok) return false;
  if (d[4] != (uint8_t)(d[0] + d[1] + d[2] + d[3])) return false;
  gHum = d[0] + d[1] / 10.0f;
  gTemp = d[2] + (d[3] & 0x7F) / 10.0f;
  if (d[3] & 0x80) gTemp = -gTemp;  // sign bit for sub-zero readings
  return true;
}

}  // namespace

void sensorBegin() {
  // Diagnostic: list every device on the bus (IP5306 power chip is 0x75).
  char found[40] = "";
  size_t len = 0;
  for (uint8_t a = 0x08; a < 0x78; a++)
    if (M5.In_I2C.scanID(a, kFreq) && len + 6 < sizeof(found))
      len += snprintf(found + len, sizeof(found) - len, " %02X", a);
  logEvent("I2C:%s", len ? found : " no devices");
}

void sensorUpdate(uint32_t nowMs) {
  if (gReadOnce && nowMs - gLastRead < kIntervalMs) return;
  gReadOnce = true;
  gLastRead = nowMs;
  // Ride out the odd I2C glitch: only give up after three misses in a row.
  gFailures = readDht12() ? 0 : gFailures + 1;
  bool found = gFailures == 0 || (gFound && gFailures < 3);
  static bool first = true;
  if (found != gFound || (first && !found)) logEvent("Sensor: DHT12 %s", found ? "found" : "not found");
  first = false;
  gFound = found;
}

bool sensorFound() { return gFound; }
float sensorTemperature() { return gTemp; }
float sensorHumidity() { return gHum; }
