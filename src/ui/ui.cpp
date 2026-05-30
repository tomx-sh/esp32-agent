#include "ui.h"

#include <cstdio>

#include <lvgl.h>

#include "page_view.h"

namespace {
lv_obj_t *titleLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *urlLabel = nullptr;
lv_obj_t *petGif = nullptr;
lv_obj_t *petStatusLabel = nullptr;
char petSpritePath[96] = "";

void configureLabel(lv_obj_t *label, const lv_font_t *font, lv_text_align_t align) {
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
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
  lv_obj_t *content = lv_obj_create(parent);
  lv_obj_remove_style_all(content);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));

  petGif = lv_gif_create(content);
  lv_obj_set_size(petGif, lv_pct(100), lv_pct(100));
  lv_image_set_inner_align(petGif, LV_IMAGE_ALIGN_CONTAIN);
  lv_gif_set_color_format(petGif, LV_COLOR_FORMAT_RGB565);
  lv_gif_set_auto_pause_invisible(petGif, true);
  lv_obj_add_flag(petGif, LV_OBJ_FLAG_HIDDEN);

  petStatusLabel = lv_label_create(content);
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
  ui_create_page_view(screen, kPages, sizeof(kPages) / sizeof(kPages[0]));
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
  if (petGif == nullptr || petStatusLabel == nullptr || lvglPath == nullptr) {
    return false;
  }

  snprintf(petSpritePath, sizeof(petSpritePath), "%s", lvglPath);
  lv_gif_set_src(petGif, petSpritePath);

  if (!lv_gif_is_loaded(petGif)) {
    lv_obj_add_flag(petGif, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(petStatusLabel, "Could not load %s", name == nullptr ? "sprite" : name);
    return false;
  }

  lv_obj_remove_flag(petGif, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(petGif, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void ui_clear_pet_sprite(const char *message) {
  if (petGif != nullptr) {
    lv_obj_add_flag(petGif, LV_OBJ_FLAG_HIDDEN);
  }

  if (petStatusLabel != nullptr) {
    lv_label_set_text(petStatusLabel, message == nullptr ? "No pet sprite loaded" : message);
    lv_obj_remove_flag(petStatusLabel, LV_OBJ_FLAG_HIDDEN);
  }
}
