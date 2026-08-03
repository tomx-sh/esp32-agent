#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#ifndef APP_DEBUG_GRAPHICS
#define APP_DEBUG_GRAPHICS 0
#endif

#ifndef APP_DEBUG_FLUSH
#define APP_DEBUG_FLUSH 0
#endif

#ifndef APP_DEBUG_I2C
#define APP_DEBUG_I2C 0
#endif

inline void debug_log_heap(const char *tag) {
#if APP_DEBUG_GRAPHICS || APP_DEBUG_I2C
  lv_mem_monitor_t lvMem = {};
  if (lv_is_initialized()) {
    lv_mem_monitor(&lvMem);
  }
  Serial.printf(
      "[debug][heap] %s internal=%u largest_internal=%u psram=%u largest_psram=%u lv_free=%u lv_biggest=%u lv_used=%u%%\n",
      tag,
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
      static_cast<unsigned>(lvMem.free_size),
      static_cast<unsigned>(lvMem.free_biggest_size),
      static_cast<unsigned>(lvMem.used_pct));
#else
  (void)tag;
#endif
}

inline void debug_log_i2c(const char *message) {
#if APP_DEBUG_I2C
  Serial.printf("[debug][i2c] %s\n", message);
#else
  (void)message;
#endif
}
