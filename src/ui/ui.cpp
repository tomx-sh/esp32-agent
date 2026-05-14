#include "ui.h"

#include <lvgl.h>

void ui_create() {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello, LVGL 9");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}
