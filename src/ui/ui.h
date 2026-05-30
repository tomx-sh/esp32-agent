#pragma once

void ui_create();
void ui_set_network_info(const char *ssid, const char *url);
void ui_set_status_message(const char *title, const char *message);
void ui_set_connection_overview(const char *headline, const char *details);
bool ui_show_pet_sprite(const char *name, const char *lvglPath);
void ui_clear_pet_sprite(const char *message);
bool ui_show_pet_message(const char *message);
void ui_clear_pet_message();
bool ui_is_pet_page_active();
