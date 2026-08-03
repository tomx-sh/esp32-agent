#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t kMaxCodexMessageLength = 240;

void ui_create();
void ui_set_network_info(const char *ssid, const char *url);
void ui_set_status_message(const char *title, const char *message);
void ui_set_connection_overview(const char *headline, const char *details);
bool ui_show_pet_sprite(const char *name, const char *lvglPath);
void ui_clear_pet_sprite(const char *message);
bool ui_show_pet_message(const char *message);
void ui_clear_pet_message();
bool ui_is_pet_page_active();
uint8_t ui_get_codex_quota_remaining_percent();
void ui_set_codex_quota_remaining_percent(uint8_t remainingPercent);
const char *ui_get_codex_message();
bool ui_get_codex_message_muted();
void ui_set_codex_message(const char *message, bool muted);
uint64_t ui_get_codex_reset_at();
void ui_set_codex_reset_at(uint64_t resetAt);
uint16_t ui_get_codex_reset_credits();
void ui_set_codex_reset_credits(uint16_t credits);
uint8_t ui_get_codex_context_remaining_percent();
void ui_set_codex_context_remaining_percent(uint8_t percent);
size_t ui_get_page_count();
const char *ui_get_page_name(size_t index);
int32_t ui_get_active_page_index();
bool ui_set_active_page_index(size_t index, bool animate);
