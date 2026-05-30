#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>

#include "debug/debug_log.h"
#include "pin_config.h"

namespace {
constexpr uint32_t kI2cClockHz = 100000;
constexpr uint32_t kI2cTimeoutMs = 50;

bool initialized = false;
}  // namespace

bool i2c_bus_init() {
  if (initialized) {
    debug_log_i2c("init skipped; already initialized");
    return true;
  }

  debug_log_i2c("Wire.begin");
  if (!Wire.begin(IIC_SDA, IIC_SCL, kI2cClockHz)) {
    Serial.println("I2C init failed");
    return false;
  }

  Wire.setTimeOut(kI2cTimeoutMs);
  initialized = true;
  debug_log_heap("i2c-init");
  return true;
}

bool i2c_bus_recover() {
  debug_log_i2c("recover");
  initialized = false;
  Wire.end();
  delay(2);
  return i2c_bus_init();
}
