#include "page_view.h"

#include "page_indicator.h"

namespace {
lv_dir_t allowedDirections(size_t index, size_t pageCount) {
  if (pageCount <= 1) {
    return LV_DIR_NONE;
  }

  if (index == 0) {
    return LV_DIR_RIGHT;
  }

  if (index == pageCount - 1) {
    return LV_DIR_LEFT;
  }

  return LV_DIR_HOR;
}

void updateIndicatorFromTileview(lv_event_t *event) {
  lv_obj_t *pageView = static_cast<lv_obj_t *>(lv_event_get_target(event));
  lv_obj_t *indicator = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
  lv_obj_t *activeTile = lv_tileview_get_tile_active(pageView);
  const int32_t activeIndex = lv_obj_get_index(activeTile);

  if (activeIndex >= 0) {
    ui_page_indicator_set_active(indicator, static_cast<size_t>(activeIndex));
  }
}
}  // namespace

lv_obj_t *ui_create_page_view(
    lv_obj_t *parent,
    const UiPageDefinition *pages,
    size_t pageCount) {
  lv_obj_t *pageView = lv_tileview_create(parent);
  lv_obj_set_size(pageView, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(pageView, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(pageView, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pageView, 0, 0);
  lv_obj_set_style_pad_all(pageView, 0, 0);
  lv_obj_set_scrollbar_mode(pageView, LV_SCROLLBAR_MODE_OFF);

  lv_obj_update_layout(pageView);

  for (size_t i = 0; i < pageCount; ++i) {
    lv_obj_t *page = lv_tileview_add_tile(
        pageView,
        static_cast<uint8_t>(i),
        0,
        allowedDirections(i, pageCount));
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    if (pages[i].build != nullptr) {
      pages[i].build(page);
    }
  }

  lv_tileview_set_tile_by_index(pageView, 0, 0, LV_ANIM_OFF);

  if (pageCount > 1) {
    lv_obj_t *indicator = ui_create_page_indicator(parent, pageCount);
    lv_obj_add_event_cb(
        pageView,
        updateIndicatorFromTileview,
        LV_EVENT_VALUE_CHANGED,
        indicator);
  }

  return pageView;
}
