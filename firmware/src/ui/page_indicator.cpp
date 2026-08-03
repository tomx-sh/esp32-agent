#include "page_indicator.h"

namespace {

void configureDot(lv_obj_t *dot, const UiPageIndicatorStyle &style) {
  lv_obj_remove_style_all(dot);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, style.dotSize, style.dotSize);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, style.color, 0);
  lv_obj_set_style_bg_opa(dot, style.inactiveOpacity, 0);
  lv_obj_set_style_bg_opa(dot, style.activeOpacity, LV_STATE_CHECKED);
}

}  // namespace

lv_obj_t *ui_create_page_indicator(
    lv_obj_t *parent,
    size_t pageCount,
    const UiPageIndicatorStyle &style) {
  if (pageCount == 0) {
    return nullptr;
  }

  lv_obj_t *indicator = lv_obj_create(parent);
  lv_obj_remove_style_all(indicator);
  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(indicator, LV_SIZE_CONTENT, style.dotSize);
  lv_obj_set_style_pad_column(indicator, style.dotGap, 0);
  lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      indicator,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  for (size_t i = 0; i < pageCount; ++i) {
    configureDot(lv_obj_create(indicator), style);
  }

  ui_page_indicator_set_active(indicator, 0);
  lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -style.bottomOffset);
  lv_obj_move_foreground(indicator);
  return indicator;
}

void ui_page_indicator_set_active(lv_obj_t *indicator, size_t activeIndex) {
  if (indicator == nullptr) {
    return;
  }

  const uint32_t dotCount = lv_obj_get_child_count(indicator);
  for (uint32_t i = 0; i < dotCount; ++i) {
    lv_obj_t *dot = lv_obj_get_child(indicator, i);
    if (i == activeIndex) {
      lv_obj_add_state(dot, LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(dot, LV_STATE_CHECKED);
    }
  }
}
