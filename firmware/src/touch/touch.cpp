#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "i2c_bus/i2c_bus.h"
#include "pin_config.h"

namespace {
constexpr uint8_t kFt3168Address = 0x38;
constexpr uint8_t kFingerCountRegister = 0x02;
constexpr uint8_t kTouchXHighRegister = 0x03;
constexpr uint8_t kTouchXLowRegister = 0x04;
constexpr uint8_t kTouchYHighRegister = 0x05;
constexpr uint8_t kTouchYLowRegister = 0x06;
constexpr uint8_t kPowerModeRegister = 0xA5;
constexpr uint8_t kPowerActiveMode = 0x00;
constexpr uint8_t kCoordinateHighMask = 0x0F;

lv_point_t lastTouchPoint = {0, 0};
lv_indev_t *touchInput = nullptr;

bool read_register(uint8_t reg, uint8_t *value) {
  Wire.beginTransmission(kFt3168Address);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    i2c_bus_recover();
    return false;
  }

  if (Wire.requestFrom(kFt3168Address, static_cast<uint8_t>(1)) != 1) {
    i2c_bus_recover();
    return false;
  }

  *value = Wire.read();
  return true;
}

bool write_register(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kFt3168Address);
  Wire.write(reg);
  Wire.write(value);
  if (Wire.endTransmission() == 0) {
    return true;
  }

  i2c_bus_recover();
  return false;
}

bool read_touch_point(lv_point_t *point) {
#ifdef TOUCH_INT
  if (digitalRead(TOUCH_INT) == HIGH) {
    return false;
  }
#endif

  uint8_t touchCount = 0;
  uint8_t xHigh = 0;
  uint8_t xLow = 0;
  uint8_t yHigh = 0;
  uint8_t yLow = 0;

  if (!read_register(kFingerCountRegister, &touchCount)) {
    return false;
  }

  if (touchCount == 0) {
    return false;
  }

  if (!read_register(kTouchXHighRegister, &xHigh) ||
      !read_register(kTouchXLowRegister, &xLow) ||
      !read_register(kTouchYHighRegister, &yHigh) ||
      !read_register(kTouchYLowRegister, &yLow)) {
    return false;
  }

  point->x = static_cast<int16_t>(((xHigh & kCoordinateHighMask) << 8) | xLow);
  point->y = static_cast<int16_t>(((yHigh & kCoordinateHighMask) << 8) | yLow);
  return true;
}

void read_touch(lv_indev_t *, lv_indev_data_t *data) {
  lv_point_t point;
  if (read_touch_point(&point)) {
    lastTouchPoint = point;
    data->point = point;
    data->state = LV_INDEV_STATE_PRESSED;
    return;
  }

  data->point = lastTouchPoint;
  data->state = LV_INDEV_STATE_RELEASED;
}
}  // namespace

void touch_init(lv_display_t *display) {
  if (!i2c_bus_init()) {
    return;
  }

#ifdef TOUCH_INT
  pinMode(TOUCH_INT, INPUT_PULLUP);
#endif

  write_register(kPowerModeRegister, kPowerActiveMode);

  touchInput = lv_indev_create();
  lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(touchInput, display);
  lv_indev_set_read_cb(touchInput, read_touch);
}
