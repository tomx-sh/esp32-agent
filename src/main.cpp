#include <Arduino.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_SH8601.h>
#include <lvgl.h>

#include "audio/audio.h"
#include "pin_config.h"
#include "ui/ui.h"
#include "wifi_config/wifi_config.h"

namespace {
constexpr uint32_t kLvglTickMs = 5;
constexpr uint32_t kDrawBufferLines = 40;

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
lv_color_t drawBuffer[LCD_WIDTH * kDrawBufferLines];

void flushDisplay(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

  gfx->draw16bitRGBBitmap(
      area->x1,
      area->y1,
      reinterpret_cast<uint16_t *>(pxMap),
      width,
      height);

  lv_display_flush_ready(disp);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting LVGL display hello world");

  if (!gfx->begin()) {
    Serial.println("Display init failed");
    return;
  }

  gfx->setBrightness(180);
  gfx->fillScreen(RGB565_BLACK);

  lv_init();
  display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(display, flushDisplay);
  lv_display_set_buffers(
      display,
      drawBuffer,
      nullptr,
      sizeof(drawBuffer),
      LV_DISPLAY_RENDER_MODE_PARTIAL);

  audio_play_beep();
  ui_create();
  wifi_config_init();

  Serial.println("Display ready");
}

void loop() {
  static uint32_t lastTick = millis();
  const uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;

  lv_timer_handler();
  wifi_config_loop();
  delay(kLvglTickMs);
}
