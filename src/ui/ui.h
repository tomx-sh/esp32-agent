#pragma once

#include <cstddef>
#include <cstdint>

void ui_create();
void ui_set_network_info(const char *ssid, const char *url);
void ui_set_status_message(const char *title, const char *message);
void ui_set_connection_overview(const char *headline, const char *details);
bool ui_show_pet_sprite(const char *name, const char *lvglPath);
void ui_clear_pet_sprite(const char *message);
bool ui_show_pet_message(const char *message);
void ui_clear_pet_message();
bool ui_is_pet_page_active();
uint8_t ui_get_codex_usage_percent();
void ui_set_codex_usage_percent(uint8_t percent);
uint32_t ui_get_codex_reset_minutes();
void ui_set_codex_reset_minutes(uint32_t minutes);
uint16_t ui_get_codex_reset_credits();
void ui_set_codex_reset_credits(uint16_t credits);
size_t ui_get_page_count();
const char *ui_get_page_name(size_t index);
int32_t ui_get_active_page_index();
bool ui_set_active_page_index(size_t index, bool animate);
