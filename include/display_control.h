#pragma once

#include <Arduino.h>

constexpr uint8_t kDisplayBrightnessMin = 0;
constexpr uint8_t kDisplayBrightnessMax = 255;

void display_control_load_settings();
uint8_t display_control_get_brightness();
void display_control_set_brightness(uint8_t brightness, bool persist);
