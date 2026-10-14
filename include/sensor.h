// DHT12 temperature/humidity sensor in the BTC Standing Base (I2C 0x5C).
#pragma once

#include <stdint.h>

void sensorBegin();                 // logs which I2C devices are present
void sensorUpdate(uint32_t nowMs);  // call often; reads every 30 s
bool sensorFound();                 // last read succeeded
float sensorTemperature();          // degrees C, valid if sensorFound()
float sensorHumidity();             // %RH, valid if sensorFound()
