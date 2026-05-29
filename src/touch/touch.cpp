#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"

namespace {
constexpr uint8_t kFt3168Address = 0x38;
constexpr uint8_t kTouchDataRegister = 0x02;
constexpr uint8_t kTouchCountMask = 0x0F;
constexpr uint8_t kCoordinateHighMask = 0x0F;

lv_point_t lastTouchPoint = {0, 0};
lv_indev_t *touchInput = nullptr;

bool read_touch_point(lv_point_t *point) {
  Wire.beginTransmission(kFt3168Address);
  Wire.write(kTouchDataRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  constexpr uint8_t kBytesToRead = 5;
  if (Wire.requestFrom(kFt3168Address, kBytesToRead) != kBytesToRead) {
    return false;
  }

  const uint8_t touchCount = Wire.read() & kTouchCountMask;
  const uint8_t xHigh = Wire.read();
  const uint8_t xLow = Wire.read();
  const uint8_t yHigh = Wire.read();
  const uint8_t yLow = Wire.read();

  if (touchCount == 0) {
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
  Wire.begin(IIC_SDA, IIC_SCL);

#ifdef TOUCH_INT
  pinMode(TOUCH_INT, INPUT_PULLUP);
#endif

  touchInput = lv_indev_create();
  lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(touchInput, display);
  lv_indev_set_read_cb(touchInput, read_touch);
}
