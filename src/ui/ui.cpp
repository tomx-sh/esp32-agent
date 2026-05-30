#include "ui.h"

#include <cstdio>

#include <lvgl.h>

#include "debug/debug_log.h"
#include "page_view.h"

namespace {
lv_obj_t *titleLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *urlLabel = nullptr;
lv_obj_t *pageView = nullptr;
lv_obj_t *petContent = nullptr;
lv_obj_t *petGif = nullptr;
lv_obj_t *petStatusLabel = nullptr;
bool petPageActive = false;
char petSpritePath[96] = "";
char petSpriteName[40] = "";

constexpr size_t kPetPageIndex = 1;

void configureLabel(lv_obj_t *label, const lv_font_t *font, lv_text_align_t align) {
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

void applyPixelArtScale(lv_obj_t *gif, const char *lvglPath) {
  uint16_t gifWidth = 0;
  uint16_t gifHeight = 0;
  if (!lv_gif_get_size(lvglPath, &gifWidth, &gifHeight) || gifWidth == 0 || gifHeight == 0) {
    lv_image_set_scale(gif, LV_SCALE_NONE);
    return;
  }

  lv_obj_t *container = lv_obj_get_parent(gif);
  lv_obj_update_layout(container);

  const int32_t availableWidth = lv_obj_get_width(container);
  const int32_t availableHeight = lv_obj_get_height(container);
  if (availableWidth <= 0 || availableHeight <= 0) {
    lv_image_set_scale(gif, LV_SCALE_NONE);
    return;
  }

  const int32_t scaleX = availableWidth / gifWidth;
  const int32_t scaleY = availableHeight / gifHeight;
  const int32_t integerScale = LV_MAX(1, LV_MIN(scaleX, scaleY));
  lv_image_set_scale(gif, static_cast<uint32_t>(integerScale * LV_SCALE_NONE));
}

lv_obj_t *ensurePetGif() {
  if (petGif != nullptr || petContent == nullptr) {
    return petGif;
  }

  debug_log_heap("pet-gif-create-before");
  petGif = lv_gif_create(petContent);
  lv_obj_set_size(petGif, lv_pct(100), lv_pct(100));
  lv_image_set_inner_align(petGif, LV_IMAGE_ALIGN_CENTER);
  lv_image_set_antialias(petGif, false);
  lv_gif_set_auto_pause_invisible(petGif, true);
  lv_obj_add_flag(petGif, LV_OBJ_FLAG_HIDDEN);
  debug_log_heap("pet-gif-create-after");
  return petGif;
}

void hidePetGif() {
  if (petGif == nullptr) {
    return;
  }

  lv_gif_pause(petGif);
  lv_obj_add_flag(petGif, LV_OBJ_FLAG_HIDDEN);
}

void deletePetGif() {
  if (petGif == nullptr) {
    return;
  }

  debug_log_heap("pet-gif-delete-before");
  lv_obj_delete(petGif);
  petGif = nullptr;
  debug_log_heap("pet-gif-delete-after");
}

bool loadPendingPetSprite() {
  if (!petPageActive || petSpritePath[0] == '\0' || petStatusLabel == nullptr) {
    return false;
  }

  lv_obj_t *gif = ensurePetGif();
  if (gif == nullptr) {
    return false;
  }

  debug_log_heap("pet-gif-set-src-before");
#if APP_DEBUG_GRAPHICS
  Serial.printf(
      "[debug][gif] set_src name=%s path=%s\n",
      petSpriteName,
      petSpritePath);
#endif
  lv_gif_set_src(gif, petSpritePath);
  debug_log_heap("pet-gif-set-src-after");

  if (!lv_gif_is_loaded(gif)) {
    lv_obj_add_flag(gif, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(
        petStatusLabel,
        "Could not load %s",
        petSpriteName[0] == '\0' ? "sprite" : petSpriteName);
    return false;
  }

  applyPixelArtScale(gif, petSpritePath);
  lv_gif_restart(gif);
  lv_gif_set_loop_count(gif, 0);
  lv_gif_resume(gif);
#if APP_DEBUG_GRAPHICS
  Serial.printf(
      "[debug][gif] loaded path=%s frames=%ld current=%ld\n",
      petSpritePath,
      static_cast<long>(lv_gif_get_frame_count(gif)),
      static_cast<long>(lv_gif_get_current_frame_index(gif)));
#endif
  lv_obj_remove_flag(gif, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(gif, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void updatePetPageActiveState() {
  if (pageView == nullptr) {
    return;
  }

  lv_obj_t *activeTile = lv_tileview_get_tile_active(pageView);
  const bool active = activeTile != nullptr &&
                      lv_obj_get_index(activeTile) == static_cast<int32_t>(kPetPageIndex);
  if (active == petPageActive) {
    return;
  }

  petPageActive = active;
#if APP_DEBUG_GRAPHICS
  Serial.printf("[debug][tile] pet_active=%d active_index=%ld\n", petPageActive, static_cast<long>(activeTile == nullptr ? -1 : lv_obj_get_index(activeTile)));
#endif
  if (petPageActive) {
    loadPendingPetSprite();
  } else {
    hidePetGif();
  }
}

void handlePageChanged(lv_event_t *) {
  updatePetPageActiveState();
}

lv_obj_t *createPageContent(lv_obj_t *parent) {
  lv_obj_t *content = lv_obj_create(parent);
  lv_obj_remove_style_all(content);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_left(content, 28, 0);
  lv_obj_set_style_pad_right(content, 28, 0);
  lv_obj_set_style_pad_top(content, 32, 0);
  lv_obj_set_style_pad_bottom(content, 24, 0);
  lv_obj_set_style_pad_row(content, 28, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      content,
      LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  return content;
}

void buildHelloPage(lv_obj_t *parent) {
  lv_obj_t *content = createPageContent(parent);
  lv_obj_set_flex_align(
      content,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  lv_obj_t *label = lv_label_create(content);
  lv_label_set_text(label, "Hello World");
  configureLabel(label, &lv_font_montserrat_32, LV_TEXT_ALIGN_CENTER);
}

void buildWifiConfigPage(lv_obj_t *parent) {
  lv_obj_t *content = createPageContent(parent);

  titleLabel = lv_label_create(content);
  lv_label_set_text(titleLabel, "Wi-Fi Config");
  configureLabel(titleLabel, &lv_font_montserrat_32, LV_TEXT_ALIGN_CENTER);

  ssidLabel = lv_label_create(content);
  lv_label_set_text(ssidLabel, "1. Connect to this WiFi hotspot:\nstarting...");
  configureLabel(ssidLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);

  urlLabel = lv_label_create(content);
  lv_label_set_text(urlLabel, "2. Visit\nstarting...");
  configureLabel(urlLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
}

void buildPetPage(lv_obj_t *parent) {
  petContent = lv_obj_create(parent);
  lv_obj_remove_style_all(petContent);
  lv_obj_remove_flag(petContent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(petContent, lv_pct(100), lv_pct(100));
  ensurePetGif();

  petStatusLabel = lv_label_create(petContent);
  lv_label_set_text(petStatusLabel, "No pet sprite loaded");
  configureLabel(petStatusLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(petStatusLabel, LV_ALIGN_CENTER, 0, 0);
}

constexpr UiPageDefinition kPages[] = {
    {"Hello", buildHelloPage},
    {"Pet", buildPetPage},
    {"Wi-Fi", buildWifiConfigPage},
};
}

void ui_create() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  pageView = ui_create_page_view(screen, kPages, sizeof(kPages) / sizeof(kPages[0]));
  lv_obj_add_event_cb(pageView, handlePageChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  updatePetPageActiveState();
}

void ui_set_network_info(const char *ssid, const char *url) {
  if (ssidLabel != nullptr) {
    lv_label_set_text_fmt(ssidLabel, "1. Connect to this WiFi hotspot:\n%s", ssid);
  }

  if (urlLabel != nullptr) {
    lv_label_set_text_fmt(urlLabel, "2. Visit\n%s", url);
  }
}

void ui_set_status_message(const char *title, const char *message) {
  if (titleLabel != nullptr) {
    lv_label_set_text(titleLabel, title);
  }

  if (ssidLabel != nullptr) {
    lv_label_set_text(ssidLabel, message);
  }

  if (urlLabel != nullptr) {
    lv_label_set_text(urlLabel, "");
  }
}

void ui_set_connection_overview(const char *headline, const char *details) {
  if (titleLabel != nullptr) {
    lv_label_set_text(titleLabel, "Wi-Fi Status");
  }

  if (ssidLabel != nullptr) {
    lv_label_set_text(ssidLabel, headline);
  }

  if (urlLabel != nullptr) {
    lv_label_set_text(urlLabel, details);
  }
}

bool ui_show_pet_sprite(const char *name, const char *lvglPath) {
  if (petStatusLabel == nullptr || lvglPath == nullptr) {
    return false;
  }

  snprintf(petSpritePath, sizeof(petSpritePath), "%s", lvglPath);
  snprintf(petSpriteName, sizeof(petSpriteName), "%s", name == nullptr ? "sprite" : name);
  if (!petPageActive) {
    lv_label_set_text_fmt(petStatusLabel, "%s ready", petSpriteName);
    return true;
  }

  return loadPendingPetSprite();
}

void ui_clear_pet_sprite(const char *message) {
  petSpritePath[0] = '\0';
  petSpriteName[0] = '\0';
  hidePetGif();

  if (petStatusLabel != nullptr) {
    lv_label_set_text(petStatusLabel, message == nullptr ? "No pet sprite loaded" : message);
    lv_obj_remove_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

bool ui_is_pet_page_active() {
  return petPageActive;
}
