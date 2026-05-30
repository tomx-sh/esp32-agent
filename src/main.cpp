#include <Arduino.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_SH8601.h>
#include <draw/sw/lv_draw_sw_utils.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "audio/audio.h"
#include "ota_update/ota_update.h"
#include "pin_config.h"
#include "touch/touch.h"
#include "ui/ui.h"
#include "wifi_config/wifi_config.h"

namespace {
constexpr uint32_t kLvglTickMs = 5;
constexpr uint32_t kDrawBufferLines = 40;
constexpr lv_display_rotation_t kDisplayRotation = LV_DISPLAY_ROTATION_90;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,
    LCD_SCLK,
    LCD_SDIO0,
    LCD_SDIO1,
    LCD_SDIO2,
    LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus,
    GFX_NOT_DEFINED,
    0,
    LCD_WIDTH,
    LCD_HEIGHT);

lv_display_t *display = nullptr;
lv_color_t *drawBuffer = nullptr;
lv_color_t *rotatedDrawBuffer = nullptr;

lv_color_t *allocateDrawBuffer(const char *name) {
  constexpr size_t bufferBytes = LCD_WIDTH * kDrawBufferLines * sizeof(lv_color_t);
  void *buffer = heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buffer == nullptr) {
    buffer = heap_caps_malloc(bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (buffer == nullptr) {
    Serial.printf("LVGL %s buffer allocation failed (%zu bytes)\n", name, bufferBytes);
    return nullptr;
  }

  Serial.printf("LVGL %s buffer allocated: %zu bytes\n", name, bufferBytes);
  return static_cast<lv_color_t *>(buffer);
}

void flushDisplay(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  lv_area_t targetArea = *area;
  uint8_t *targetPxMap = pxMap;

  const lv_display_rotation_t rotation = lv_display_get_rotation(disp);
  if (rotation != LV_DISPLAY_ROTATION_0) {
    const lv_color_format_t colorFormat = lv_display_get_color_format(disp);
    const int32_t sourceWidth = lv_area_get_width(area);
    const int32_t sourceHeight = lv_area_get_height(area);
    const uint32_t sourceStride = lv_draw_buf_width_to_stride(sourceWidth, colorFormat);

    lv_display_rotate_area(disp, &targetArea);
    const uint32_t targetStride =
        lv_draw_buf_width_to_stride(lv_area_get_width(&targetArea), colorFormat);

    lv_draw_sw_rotate(
        pxMap,
        rotatedDrawBuffer,
        sourceWidth,
        sourceHeight,
        sourceStride,
        targetStride,
        rotation,
        colorFormat);

    targetPxMap = reinterpret_cast<uint8_t *>(rotatedDrawBuffer);
  }

  const uint32_t width = lv_area_get_width(&targetArea);
  const uint32_t height = lv_area_get_height(&targetArea);

  gfx->draw16bitRGBBitmap(
      targetArea.x1,
      targetArea.y1,
      reinterpret_cast<uint16_t *>(targetPxMap),
      width,
      height);

  lv_display_flush_ready(disp);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting LVGL display hello world");
  Serial.printf(
      "Heap: internal=%u, psram=%u/%u, largest_psram=%u\n",
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      ESP.getFreePsram(),
      ESP.getPsramSize(),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!gfx->begin()) {
    Serial.println("Display init failed");
    return;
  }

  gfx->setBrightness(180);
  gfx->fillScreen(RGB565_BLACK);

  drawBuffer = allocateDrawBuffer("draw");
  rotatedDrawBuffer = allocateDrawBuffer("rotation");
  if (drawBuffer == nullptr || rotatedDrawBuffer == nullptr) {
    Serial.println("Display buffer init failed");
    return;
  }

  lv_init();
  display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(display, flushDisplay);
  lv_display_set_buffers(
      display,
      drawBuffer,
      nullptr,
      LCD_WIDTH * kDrawBufferLines * sizeof(lv_color_t),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_rotation(display, kDisplayRotation);

  touch_init(display);
  audio_play_beep();
  ui_create();
  wifi_config_init();
  ota_update_init();

  Serial.println("Display ready");
}

void loop() {
  static uint32_t lastTick = millis();
  const uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;

  lv_timer_handler();
  wifi_config_loop();
  ota_update_loop();
  delay(kLvglTickMs);
}
