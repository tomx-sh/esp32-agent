#pragma once

#include <cstddef>

#include <lvgl.h>

struct UiPageIndicatorStyle {
  lv_coord_t dotSize = 6;
  lv_coord_t dotGap = 6;
  lv_coord_t bottomOffset = 10;
  lv_opa_t inactiveOpacity = LV_OPA_40;
  lv_opa_t activeOpacity = LV_OPA_COVER;
  lv_color_t color = lv_color_white();
};

lv_obj_t *ui_create_page_indicator(
    lv_obj_t *parent,
    size_t pageCount,
    const UiPageIndicatorStyle &style = UiPageIndicatorStyle{});

void ui_page_indicator_set_active(lv_obj_t *indicator, size_t activeIndex);
