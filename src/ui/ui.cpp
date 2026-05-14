#include "ui.h"

#include <lvgl.h>

namespace {
lv_obj_t *titleLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *urlLabel = nullptr;
}

void ui_create() {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  titleLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(titleLabel, "Wi-Fi Config");
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_32, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 48);

  ssidLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(ssidLabel, "1. Connect to this WiFi hotspot:\nstarting...");
  lv_obj_set_style_text_color(ssidLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ssidLabel, 320);
  lv_obj_align(ssidLabel, LV_ALIGN_TOP_MID, 0, 138);

  urlLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(urlLabel, "2. Visit\nstarting...");
  lv_obj_set_style_text_color(urlLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(urlLabel, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(urlLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(urlLabel, 320);
  lv_obj_align(urlLabel, LV_ALIGN_TOP_MID, 0, 250);
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
