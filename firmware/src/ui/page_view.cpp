#include "page_view.h"

#include "page_indicator.h"

namespace {
constexpr uint32_t kIndicatorVisibleMs = 5000;

struct PageIndicatorVisibility {
  lv_obj_t *pageView = nullptr;
  lv_obj_t *indicator = nullptr;
  lv_timer_t *hideTimer = nullptr;
};

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

void hideIndicator(lv_timer_t *timer) {
  auto *visibility = static_cast<PageIndicatorVisibility *>(lv_timer_get_user_data(timer));
  if (visibility == nullptr || visibility->indicator == nullptr) {
    return;
  }

  lv_obj_add_flag(visibility->indicator, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(timer);
}

void showIndicator(PageIndicatorVisibility *visibility) {
  if (visibility == nullptr || visibility->indicator == nullptr || visibility->hideTimer == nullptr) {
    return;
  }

  lv_obj_remove_flag(visibility->indicator, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(visibility->indicator);
  lv_timer_reset(visibility->hideTimer);
  lv_timer_resume(visibility->hideTimer);
}

void handlePageViewIndicatorEvent(lv_event_t *event) {
  auto *visibility = static_cast<PageIndicatorVisibility *>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);

  if (visibility == nullptr) {
    return;
  }

  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *activeTile = lv_tileview_get_tile_active(visibility->pageView);
    const int32_t activeIndex = lv_obj_get_index(activeTile);

    if (activeIndex >= 0) {
      ui_page_indicator_set_active(visibility->indicator, static_cast<size_t>(activeIndex));
    }
  }

  if (
      code == LV_EVENT_PRESSED ||
      code == LV_EVENT_SCROLL_BEGIN ||
      code == LV_EVENT_SCROLL ||
      code == LV_EVENT_SCROLL_END ||
      code == LV_EVENT_VALUE_CHANGED) {
    showIndicator(visibility);
  }
}

void handlePageTouch(lv_event_t *event) {
  auto *visibility = static_cast<PageIndicatorVisibility *>(lv_event_get_user_data(event));
  showIndicator(visibility);
}

void addPageTouchCallbacks(lv_obj_t *object, PageIndicatorVisibility *visibility) {
  lv_obj_add_event_cb(object, handlePageTouch, LV_EVENT_CLICKED, visibility);

  const uint32_t childCount = lv_obj_get_child_count(object);
  for (uint32_t i = 0; i < childCount; ++i) {
    addPageTouchCallbacks(lv_obj_get_child(object, i), visibility);
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
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);

    auto *visibility = static_cast<PageIndicatorVisibility *>(lv_malloc(sizeof(PageIndicatorVisibility)));
    if (visibility != nullptr) {
      visibility->pageView = pageView;
      visibility->indicator = indicator;
      visibility->hideTimer = lv_timer_create(hideIndicator, kIndicatorVisibleMs, visibility);
      if (visibility->hideTimer != nullptr) {
        lv_timer_pause(visibility->hideTimer);

        lv_obj_add_event_cb(
            pageView,
            handlePageViewIndicatorEvent,
            LV_EVENT_ALL,
            visibility);

        for (uint32_t i = 0; i < pageCount; ++i) {
          addPageTouchCallbacks(lv_obj_get_child(pageView, i), visibility);
        }
      }
    }
  }

  return pageView;
}
