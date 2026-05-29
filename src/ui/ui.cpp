#include "ui.h"

#include <lvgl.h>

namespace {
lv_obj_t *screenContent = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *urlLabel = nullptr;

void configureLabel(lv_obj_t *label, const lv_font_t *font, lv_text_align_t align) {
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}
}

void ui_create() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

  screenContent = lv_obj_create(screen);
  lv_obj_remove_style_all(screenContent);
  lv_obj_set_size(screenContent, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_left(screenContent, 28, 0);
  lv_obj_set_style_pad_right(screenContent, 28, 0);
  lv_obj_set_style_pad_top(screenContent, 32, 0);
  lv_obj_set_style_pad_bottom(screenContent, 24, 0);
  lv_obj_set_style_pad_row(screenContent, 28, 0);
  lv_obj_set_flex_flow(screenContent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      screenContent,
      LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  titleLabel = lv_label_create(screenContent);
  lv_label_set_text(titleLabel, "Wi-Fi Config");
  configureLabel(titleLabel, &lv_font_montserrat_32, LV_TEXT_ALIGN_CENTER);

  ssidLabel = lv_label_create(screenContent);
  lv_label_set_text(ssidLabel, "1. Connect to this WiFi hotspot:\nstarting...");
  configureLabel(ssidLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);

  urlLabel = lv_label_create(screenContent);
  lv_label_set_text(urlLabel, "2. Visit\nstarting...");
  configureLabel(urlLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
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
