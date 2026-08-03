#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {
constexpr size_t kPreferInternalLimitBytes = 64 * 1024;
}

extern "C" {

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *, size_t) {
  return nullptr;
}

void lv_mem_remove_pool(lv_mem_pool_t) {}

void *lv_malloc_core(size_t size) {
  if (size == 0) {
    return nullptr;
  }

  if (size <= kPreferInternalLimitBytes) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
      return ptr;
    }
  }

  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    return ptr;
  }

  return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *ptr, size_t newSize) {
  if (ptr == nullptr) {
    return lv_malloc_core(newSize);
  }

  if (newSize == 0) {
    heap_caps_free(ptr);
    return nullptr;
  }

  const uint32_t caps = newSize <= kPreferInternalLimitBytes
                            ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                            : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void *newPtr = heap_caps_realloc(ptr, newSize, caps);
  if (newPtr != nullptr) {
    return newPtr;
  }

  return heap_caps_realloc(ptr, newSize, MALLOC_CAP_8BIT);
}

void lv_free_core(void *ptr) {
  heap_caps_free(ptr);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon) {
  if (mon == nullptr) {
    return;
  }

  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  mon->total_size = ESP.getHeapSize() + ESP.getPsramSize();
  mon->free_size = freeInternal + freePsram;
  mon->free_biggest_size = max(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mon->used_pct = mon->total_size == 0 ? 0 : static_cast<uint8_t>(100 - (mon->free_size * 100 / mon->total_size));
  mon->frag_pct = 0;
  mon->free_cnt = 0;
  mon->used_cnt = 0;
  mon->max_used = 0;
}

lv_result_t lv_mem_test_core(void) {
  return LV_RESULT_OK;
}

}
