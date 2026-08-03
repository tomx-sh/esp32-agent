#pragma once

#include <lvgl.h>

struct PetMessageStyle {
  int32_t height;
  int32_t cornerRadius;
  int32_t padding;
  const lv_font_t *font;
  lv_color_t backgroundColor;
  lv_color_t textColor;
};

PetMessageStyle pet_message_default_style();
bool pet_message_init(lv_obj_t *parent, const PetMessageStyle *style = nullptr);
bool pet_message_show(const char *message);
void pet_message_clear();
