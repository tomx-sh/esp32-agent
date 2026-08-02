#include "ui.h"

#include <cstdio>
#include <ctime>

#include <lvgl.h>

#include "debug/debug_log.h"
#include "fonts/jetbrains_mono_36.h"
#include "icons/flask_round_36.h"
#include "page_view.h"
#include "pet_message.h"

namespace {
lv_obj_t *titleLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *urlLabel = nullptr;
lv_obj_t *pageView = nullptr;
lv_obj_t *petContent = nullptr;
lv_obj_t *codexUsageGifContent = nullptr;
lv_obj_t *petGif = nullptr;
lv_obj_t *petStatusLabel = nullptr;
lv_obj_t *codexUsageStatusLabel = nullptr;
lv_obj_t *codexUsageBar = nullptr;
lv_obj_t *codexUsageLabel = nullptr;
lv_obj_t *codexResetLabel = nullptr;
lv_obj_t *codexResetCreditsRow = nullptr;
lv_obj_t *codexResetCreditsLabel = nullptr;
lv_obj_t *codexContextArc = nullptr;
lv_timer_t *codexResetTimer = nullptr;
bool spritePageActive = false;
uint8_t codexUsagePercent = 0;
uint8_t codexContextRemainingPercent = 100;
uint64_t codexResetAt = 0;
uint16_t codexResetCredits = 0;
char petSpritePath[96] = "";
char petSpriteName[40] = "";

constexpr size_t kCodexUsagePageIndex = 0;
constexpr size_t kPetPageIndex = 1;
constexpr uint32_t kCodexGaugeEmptyColor = 0x303030;
constexpr int32_t kCodexResetCreditsRowHeight = 36;
constexpr int32_t kCodexContextArcSize = 84;
constexpr int32_t kCodexContextArcWidth = 10;
constexpr int32_t kCodexGaugeRowHeight = 52;
constexpr int32_t kCodexGridColumns[] = {
    kCodexContextArcSize,
    LV_GRID_FR(1),
    kCodexContextArcSize,
    LV_GRID_TEMPLATE_LAST};
constexpr int32_t kCodexGridRows[] = {
    kCodexResetCreditsRowHeight,
    LV_GRID_FR(1),
    kCodexGaugeRowHeight,
    LV_GRID_TEMPLATE_LAST};
constexpr time_t kValidClockEpoch = 1700000000;
constexpr uint64_t kAbsoluteResetDateThresholdSeconds = 24 * 60 * 60;

int32_t getDigitVisualTopOffset(const lv_font_t *font, int32_t targetHeight) {
  lv_font_glyph_dsc_t digit = {};
  if (!lv_font_get_glyph_dsc(font, &digit, '0', 0)) {
    return (targetHeight - lv_font_get_line_height(font)) / 2;
  }

  const int32_t glyphTop =
      lv_font_get_line_height(font) - font->base_line - digit.box_h - digit.ofs_y;
  return (targetHeight - digit.box_h) / 2 - glyphTop;
}

void hideCodexResetLabel() {
  lv_obj_add_flag(codexResetLabel, LV_OBJ_FLAG_HIDDEN);
}

void updateCodexResetLabel() {
  if (codexResetLabel == nullptr) {
    return;
  }

  const time_t now = time(nullptr);
  if (
      codexResetAt == 0 ||
      now < kValidClockEpoch ||
      codexResetAt <= static_cast<uint64_t>(now)) {
    hideCodexResetLabel();
    return;
  }

  const uint64_t remainingSeconds = codexResetAt - static_cast<uint64_t>(now);

  if (remainingSeconds >= kAbsoluteResetDateThresholdSeconds) {
    const time_t resetTime = static_cast<time_t>(codexResetAt);
    tm utcReset = {};
    if (gmtime_r(&resetTime, &utcReset) == nullptr) {
      hideCodexResetLabel();
      return;
    }

    constexpr const char *kMonthNames[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    lv_label_set_text_fmt(
        codexResetLabel,
        "#%06lx resets# %s %d",
        static_cast<unsigned long>(kCodexGaugeEmptyColor),
        kMonthNames[utcReset.tm_mon],
        utcReset.tm_mday);
  } else {
    const uint32_t remainingMinutes = static_cast<uint32_t>((remainingSeconds + 59) / 60);
    const uint32_t hours = remainingMinutes / 60;
    const uint32_t minutes = remainingMinutes % 60;

    if (hours > 0) {
      lv_label_set_text_fmt(
          codexResetLabel,
          "#%06lx resets# %luh %lum",
          static_cast<unsigned long>(kCodexGaugeEmptyColor),
          static_cast<unsigned long>(hours),
          static_cast<unsigned long>(minutes));
    } else {
      lv_label_set_text_fmt(
          codexResetLabel,
          "#%06lx resets# %lum",
          static_cast<unsigned long>(kCodexGaugeEmptyColor),
          static_cast<unsigned long>(minutes));
    }
  }

  lv_obj_remove_flag(codexResetLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(codexResetLabel);
}

void updateCodexResetCreditsLabel() {
  if (codexResetCreditsLabel == nullptr) {
    return;
  }

  lv_label_set_text_fmt(
      codexResetCreditsLabel,
      "%u",
      static_cast<unsigned int>(codexResetCredits));
  if (codexResetCreditsRow != nullptr) {
    lv_obj_move_foreground(codexResetCreditsRow);
  }
}

void updateCodexContextArc() {
  if (codexContextArc == nullptr) {
    return;
  }

  lv_arc_set_value(codexContextArc, codexContextRemainingPercent);
  lv_obj_move_foreground(codexContextArc);
}

void handleCodexResetTimer(lv_timer_t *timer) {
  updateCodexResetLabel();

  const time_t now = time(nullptr);
  if (
      codexResetAt == 0 ||
      (now >= kValidClockEpoch && codexResetAt <= static_cast<uint64_t>(now))) {
    lv_timer_pause(timer);
  }
}

void configureLabel(lv_obj_t *label, const lv_font_t *font, lv_text_align_t align) {
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

void applyContainScale(lv_obj_t *gif, const char *lvglPath) {
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

  const uint32_t scaleX =
      static_cast<uint32_t>((static_cast<uint64_t>(availableWidth) * LV_SCALE_NONE) / gifWidth);
  const uint32_t scaleY =
      static_cast<uint32_t>((static_cast<uint64_t>(availableHeight) * LV_SCALE_NONE) / gifHeight);
  uint32_t containScale = scaleX < scaleY ? scaleX : scaleY;
  if (containScale == 0) {
    containScale = 1;
  }
  lv_image_set_scale(gif, containScale);
}

int32_t activePageIndex() {
  if (pageView == nullptr) {
    return -1;
  }

  lv_obj_t *activeTile = lv_tileview_get_tile_active(pageView);
  return activeTile == nullptr ? -1 : lv_obj_get_index(activeTile);
}

lv_obj_t *activeSpriteContent() {
  return activePageIndex() == static_cast<int32_t>(kCodexUsagePageIndex)
             ? codexUsageGifContent
             : petContent;
}

lv_obj_t *activeSpriteStatusLabel() {
  return activePageIndex() == static_cast<int32_t>(kCodexUsagePageIndex)
             ? codexUsageStatusLabel
             : petStatusLabel;
}

lv_obj_t *ensurePetGif() {
  lv_obj_t *content = activeSpriteContent();
  if (petGif != nullptr || content == nullptr) {
    return petGif;
  }

  debug_log_heap("pet-gif-create-before");
  petGif = lv_gif_create(content);
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
  lv_obj_t *content = activeSpriteContent();
  lv_obj_t *statusLabel = activeSpriteStatusLabel();
  if (!spritePageActive || petSpritePath[0] == '\0' || content == nullptr || statusLabel == nullptr) {
    return false;
  }

  lv_obj_t *gif = ensurePetGif();
  if (gif == nullptr) {
    return false;
  }

  if (lv_obj_get_parent(gif) != content) {
    lv_obj_set_parent(gif, content);
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
    lv_obj_remove_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(
        statusLabel,
        "Could not load %s",
        petSpriteName[0] == '\0' ? "sprite" : petSpriteName);
    return false;
  }

  applyContainScale(gif, petSpritePath);
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
  lv_obj_add_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  if (activePageIndex() == static_cast<int32_t>(kCodexUsagePageIndex)) {
    updateCodexResetLabel();
    updateCodexResetCreditsLabel();
    updateCodexContextArc();
  }
  if (activePageIndex() == static_cast<int32_t>(kPetPageIndex)) {
    pet_message_init(petContent);
  }
  return true;
}

void updateSpritePageActiveState() {
  if (pageView == nullptr) {
    return;
  }

  const int32_t activeIndex = activePageIndex();
  const bool active =
      activeIndex == static_cast<int32_t>(kPetPageIndex) ||
      activeIndex == static_cast<int32_t>(kCodexUsagePageIndex);
  const bool contentChanged =
      active && petGif != nullptr && lv_obj_get_parent(petGif) != activeSpriteContent();
  if (active == spritePageActive && !contentChanged) {
    return;
  }

  spritePageActive = active;
#if APP_DEBUG_GRAPHICS
  Serial.printf(
      "[debug][tile] sprite_active=%d active_index=%ld\n",
      spritePageActive,
      static_cast<long>(activeIndex));
#endif
  if (spritePageActive) {
    loadPendingPetSprite();
  } else {
    hidePetGif();
  }
}

void handlePageChanged(lv_event_t *) {
  updateSpritePageActiveState();
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
  pet_message_init(petContent);

  petStatusLabel = lv_label_create(petContent);
  lv_label_set_text(petStatusLabel, "No pet sprite loaded");
  configureLabel(petStatusLabel, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(petStatusLabel, LV_ALIGN_CENTER, 0, 0);
}

void buildCodexUsagePage(lv_obj_t *parent) {
  lv_obj_t *content = lv_obj_create(parent);
  lv_obj_remove_style_all(content);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_left(content, 12, 0);
  lv_obj_set_style_pad_right(content, 12, 0);
  lv_obj_set_style_pad_top(content, 20, 0);
  lv_obj_set_style_pad_bottom(content, 20, 0);
  lv_obj_set_style_pad_column(content, 12, 0);
  lv_obj_set_style_pad_row(content, 14, 0);
  lv_obj_set_grid_dsc_array(content, kCodexGridColumns, kCodexGridRows);

  lv_obj_t *topRow = lv_obj_create(content);
  lv_obj_remove_style_all(topRow);
  lv_obj_remove_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_cell(
      topRow,
      LV_GRID_ALIGN_STRETCH,
      0,
      3,
      LV_GRID_ALIGN_STRETCH,
      0,
      1);

  codexResetLabel = lv_label_create(topRow);
  lv_obj_set_width(codexResetLabel, lv_pct(100));
  lv_obj_set_style_text_color(codexResetLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(codexResetLabel, &jetbrains_mono_36, 0);
  lv_obj_set_style_text_align(codexResetLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_recolor(codexResetLabel, true);
  lv_label_set_long_mode(codexResetLabel, LV_LABEL_LONG_CLIP);
  const int32_t resetTextTopOffset = getDigitVisualTopOffset(
      &jetbrains_mono_36,
      kCodexResetCreditsRowHeight);
  lv_obj_align(codexResetLabel, LV_ALIGN_TOP_LEFT, 0, resetTextTopOffset);
  updateCodexResetLabel();

  codexResetCreditsRow = lv_obj_create(topRow);
  lv_obj_remove_style_all(codexResetCreditsRow);
  lv_obj_remove_flag(codexResetCreditsRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      codexResetCreditsRow,
      LV_SIZE_CONTENT,
      kCodexResetCreditsRowHeight);
  lv_obj_set_style_pad_column(codexResetCreditsRow, 8, 0);
  lv_obj_set_flex_flow(codexResetCreditsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      codexResetCreditsRow,
      LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  lv_obj_align(codexResetCreditsRow, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_t *resetCreditsIcon = lv_image_create(codexResetCreditsRow);
  lv_image_set_src(resetCreditsIcon, &flask_round_36);
  lv_obj_set_style_image_recolor(
      resetCreditsIcon,
      lv_color_hex(kCodexGaugeEmptyColor),
      0);
  lv_obj_set_style_image_recolor_opa(resetCreditsIcon, LV_OPA_COVER, 0);

  codexResetCreditsLabel = lv_label_create(codexResetCreditsRow);
  lv_obj_set_width(codexResetCreditsLabel, LV_SIZE_CONTENT);
  lv_obj_set_style_text_color(codexResetCreditsLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(codexResetCreditsLabel, &jetbrains_mono_36, 0);
  lv_obj_set_style_text_align(codexResetCreditsLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(codexResetCreditsLabel, LV_LABEL_LONG_CLIP);
  const int32_t resetCreditsLineBoxTopOffset =
      (kCodexResetCreditsRowHeight -
       lv_font_get_line_height(&jetbrains_mono_36)) /
      2;
  lv_obj_set_style_translate_y(
      codexResetCreditsLabel,
      resetTextTopOffset - resetCreditsLineBoxTopOffset,
      0);
  updateCodexResetCreditsLabel();

  codexResetTimer = lv_timer_create(handleCodexResetTimer, 60 * 1000, nullptr);
  if (codexResetTimer != nullptr && codexResetAt == 0) {
    lv_timer_pause(codexResetTimer);
  }

  codexUsageGifContent = lv_obj_create(content);
  lv_obj_remove_style_all(codexUsageGifContent);
  lv_obj_remove_flag(codexUsageGifContent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(codexUsageGifContent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_grid_cell(
      codexUsageGifContent,
      LV_GRID_ALIGN_STRETCH,
      1,
      1,
      LV_GRID_ALIGN_STRETCH,
      1,
      1);

  codexUsageStatusLabel = lv_label_create(codexUsageGifContent);
  lv_label_set_text(codexUsageStatusLabel, "No pet sprite loaded");
  configureLabel(codexUsageStatusLabel, &lv_font_montserrat_32, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(codexUsageStatusLabel, LV_ALIGN_CENTER, 0, 0);

  codexContextArc = lv_arc_create(content);
  lv_obj_set_size(codexContextArc, kCodexContextArcSize, kCodexContextArcSize);
  lv_obj_remove_flag(codexContextArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(codexContextArc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_color(
      codexContextArc,
      lv_color_hex(kCodexGaugeEmptyColor),
      LV_PART_MAIN);
  lv_obj_set_style_arc_opa(codexContextArc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_width(
      codexContextArc,
      kCodexContextArcWidth,
      LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(codexContextArc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_color(
      codexContextArc,
      lv_color_white(),
      LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(codexContextArc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(
      codexContextArc,
      kCodexContextArcWidth,
      LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(codexContextArc, true, LV_PART_INDICATOR);
  lv_arc_set_rotation(codexContextArc, 270);
  lv_arc_set_bg_angles(codexContextArc, 0, 360);
  lv_arc_set_range(codexContextArc, 0, 100);
  lv_obj_set_grid_cell(
      codexContextArc,
      LV_GRID_ALIGN_CENTER,
      0,
      1,
      LV_GRID_ALIGN_CENTER,
      1,
      1);
  updateCodexContextArc();

  lv_obj_t *gaugeRow = lv_obj_create(content);
  lv_obj_remove_style_all(gaugeRow);
  lv_obj_remove_flag(gaugeRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_cell(
      gaugeRow,
      LV_GRID_ALIGN_STRETCH,
      0,
      3,
      LV_GRID_ALIGN_STRETCH,
      2,
      1);
  lv_obj_set_style_pad_column(gaugeRow, 12, 0);
  lv_obj_set_flex_flow(gaugeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      gaugeRow,
      LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  codexUsageBar = lv_bar_create(gaugeRow);
  lv_obj_set_height(codexUsageBar, 18);
  lv_obj_set_flex_grow(codexUsageBar, 1);
  lv_obj_set_style_radius(codexUsageBar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(codexUsageBar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(codexUsageBar, lv_color_hex(kCodexGaugeEmptyColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(codexUsageBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(codexUsageBar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(codexUsageBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(codexUsageBar, 0, 100);
  lv_bar_set_value(codexUsageBar, codexUsagePercent, LV_ANIM_OFF);

  codexUsageLabel = lv_label_create(gaugeRow);
  lv_point_t maximumPercentSize = {};
  lv_text_get_size(
      &maximumPercentSize,
      "100%",
      &jetbrains_mono_36,
      0,
      0,
      LV_COORD_MAX,
      LV_TEXT_FLAG_NONE);
  lv_obj_set_width(codexUsageLabel, maximumPercentSize.x + 4);
  lv_obj_set_style_text_color(codexUsageLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(codexUsageLabel, &jetbrains_mono_36, 0);
  lv_obj_set_style_text_align(codexUsageLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(codexUsageLabel, LV_LABEL_LONG_CLIP);
  lv_label_set_text_fmt(codexUsageLabel, "%u%%", codexUsagePercent);
}

constexpr UiPageDefinition kPages[] = {
    {"Codex Usage", buildCodexUsagePage},
    {"Pet", buildPetPage},
    {"Wi-Fi", buildWifiConfigPage},
    {"Hello", buildHelloPage},
};
}

void ui_create() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  pageView = ui_create_page_view(screen, kPages, sizeof(kPages) / sizeof(kPages[0]));
  lv_obj_add_event_cb(pageView, handlePageChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  updateSpritePageActiveState();
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
  if (!spritePageActive) {
    lv_label_set_text_fmt(petStatusLabel, "%s ready", petSpriteName);
    if (codexUsageStatusLabel != nullptr) {
      lv_label_set_text_fmt(codexUsageStatusLabel, "%s ready", petSpriteName);
    }
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

  if (codexUsageStatusLabel != nullptr) {
    lv_label_set_text(
        codexUsageStatusLabel,
        message == nullptr ? "No pet sprite loaded" : message);
    lv_obj_remove_flag(codexUsageStatusLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

bool ui_show_pet_message(const char *message) {
  return pet_message_show(message);
}

void ui_clear_pet_message() {
  pet_message_clear();
}

bool ui_is_pet_page_active() {
  return spritePageActive;
}

uint8_t ui_get_codex_usage_percent() {
  return codexUsagePercent;
}

void ui_set_codex_usage_percent(uint8_t percent) {
  codexUsagePercent = percent > 100 ? 100 : percent;

  if (codexUsageBar != nullptr) {
    lv_bar_set_value(codexUsageBar, codexUsagePercent, LV_ANIM_ON);
  }

  if (codexUsageLabel != nullptr) {
    lv_label_set_text_fmt(codexUsageLabel, "%u%%", codexUsagePercent);
  }
}

uint64_t ui_get_codex_reset_at() {
  return codexResetAt;
}

void ui_set_codex_reset_at(uint64_t resetAt) {
  codexResetAt = resetAt;
  updateCodexResetLabel();

  if (codexResetTimer == nullptr) {
    return;
  }

  lv_timer_reset(codexResetTimer);
  if (codexResetAt > 0) {
    lv_timer_resume(codexResetTimer);
  } else {
    lv_timer_pause(codexResetTimer);
  }
}

uint16_t ui_get_codex_reset_credits() {
  return codexResetCredits;
}

void ui_set_codex_reset_credits(uint16_t credits) {
  codexResetCredits = credits;
  updateCodexResetCreditsLabel();
}

uint8_t ui_get_codex_context_remaining_percent() {
  return codexContextRemainingPercent;
}

void ui_set_codex_context_remaining_percent(uint8_t percent) {
  codexContextRemainingPercent = percent > 100 ? 100 : percent;
  updateCodexContextArc();
}

size_t ui_get_page_count() {
  return sizeof(kPages) / sizeof(kPages[0]);
}

const char *ui_get_page_name(size_t index) {
  if (index >= ui_get_page_count()) {
    return "";
  }

  return kPages[index].name;
}

int32_t ui_get_active_page_index() {
  if (pageView == nullptr) {
    return -1;
  }

  lv_obj_t *activeTile = lv_tileview_get_tile_active(pageView);
  if (activeTile == nullptr) {
    return -1;
  }

  return lv_obj_get_index(activeTile);
}

bool ui_set_active_page_index(size_t index, bool animate) {
  if (pageView == nullptr || index >= ui_get_page_count()) {
    return false;
  }

  lv_tileview_set_tile_by_index(
      pageView,
      static_cast<uint32_t>(index),
      0,
      animate ? LV_ANIM_ON : LV_ANIM_OFF);
  updateSpritePageActiveState();
  return true;
}
