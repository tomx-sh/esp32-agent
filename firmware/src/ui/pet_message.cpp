#include "pet_message.h"

#include "pin_config.h"

namespace {
lv_obj_t *balloon = nullptr;
lv_obj_t *messageLabel = nullptr;
PetMessageStyle activeStyle = {};

int32_t max_message_height() {
  const int32_t lineHeight = lv_font_get_line_height(activeStyle.font);
  const int32_t lineSpace = lv_obj_get_style_text_line_space(messageLabel, LV_PART_MAIN);
  return lineHeight * 2 + lineSpace;
}

void update_message_layout() {
  if (balloon == nullptr || messageLabel == nullptr) {
    return;
  }

  lv_obj_update_layout(balloon);
  int32_t messageWidth = lv_obj_get_content_width(balloon);
  if (messageWidth <= 0) {
    messageWidth = LCD_WIDTH - activeStyle.padding * 2;
  }

  lv_point_t textSize = {};
  lv_text_get_size(
      &textSize,
      lv_label_get_text(messageLabel),
      activeStyle.font,
      0,
      lv_obj_get_style_text_line_space(messageLabel, LV_PART_MAIN),
      messageWidth,
      LV_TEXT_FLAG_NONE);

  lv_obj_set_size(
      messageLabel,
      messageWidth,
      LV_MIN(textSize.y, max_message_height()));
  lv_obj_align(messageLabel, LV_ALIGN_CENTER, 0, 0);
}

void apply_style() {
  if (balloon == nullptr || messageLabel == nullptr) {
    return;
  }

  lv_obj_set_height(balloon, activeStyle.height);
  lv_obj_set_style_pad_left(balloon, activeStyle.padding * 2, 0);
  lv_obj_set_style_pad_right(balloon, activeStyle.padding * 2, 0);
  lv_obj_set_style_pad_top(balloon, activeStyle.padding, 0);
  lv_obj_set_style_pad_bottom(balloon, activeStyle.padding, 0);
  lv_obj_set_style_bg_color(balloon, activeStyle.backgroundColor, 0);
  lv_obj_set_style_bg_opa(balloon, static_cast<lv_opa_t>(255), 0);
  lv_obj_set_style_border_width(balloon, 0, 0);
  lv_obj_set_style_radius(balloon, activeStyle.cornerRadius, 0);
  lv_obj_set_style_shadow_width(balloon, 0, 0);

  lv_obj_set_width(messageLabel, lv_pct(100));
  lv_obj_set_style_text_color(messageLabel, activeStyle.textColor, 0);
  lv_obj_set_style_text_font(messageLabel, activeStyle.font, 0);
  lv_obj_set_style_text_align(messageLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_DOT);
  update_message_layout();
}
}  // namespace

PetMessageStyle pet_message_default_style() {
  return {
      2 * DISPLAY_CORNER_RADIUS_PX,
      DISPLAY_CORNER_RADIUS_PX,
      12,
      &lv_font_montserrat_40,
      lv_color_white(),
      lv_color_black(),
  };
}

bool pet_message_init(lv_obj_t *parent, const PetMessageStyle *style) {
  if (parent == nullptr) {
    return false;
  }

  activeStyle = style == nullptr ? pet_message_default_style() : *style;

  if (balloon == nullptr) {
    balloon = lv_obj_create(parent);
    lv_obj_remove_flag(balloon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(balloon, LV_OBJ_FLAG_HIDDEN);
  }

  if (messageLabel == nullptr) {
    messageLabel = lv_label_create(balloon);
  }

  apply_style();
  lv_obj_set_width(balloon, lv_pct(100));
  lv_obj_align(balloon, LV_ALIGN_BOTTOM_MID, 0, 0);
  update_message_layout();
  lv_obj_move_foreground(balloon);
  return true;
}

bool pet_message_show(const char *message) {
  if (balloon == nullptr || messageLabel == nullptr || message == nullptr || message[0] == '\0') {
    return false;
  }

  lv_label_set_text(messageLabel, message);
  update_message_layout();
  lv_obj_remove_flag(balloon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(balloon);
  return true;
}

void pet_message_clear() {
  if (balloon == nullptr || messageLabel == nullptr) {
    return;
  }

  lv_label_set_text(messageLabel, "");
  lv_obj_add_flag(balloon, LV_OBJ_FLAG_HIDDEN);
}
