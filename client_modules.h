#pragma once

void client_set_zoom(bool value);
bool client_zoom_enabled();
void client_set_zoom_key(int value);
int client_zoom_key();
void client_set_zoom_default(int value);
int client_zoom_default();
void client_set_zoom_scroll(int value);
int client_zoom_scroll();

// Module controls shared by the client lifecycle and menu declarations.
void client_set_sprint(bool value);
bool client_sprint_enabled();
void client_set_blur(bool value);
bool client_blur_enabled();
void client_set_fps_limit(bool value);
bool client_fps_limit_enabled();
void client_set_analog(bool value);
bool client_analog_enabled();
void client_set_auto_gg(bool value);
bool client_auto_gg_enabled();
void client_set_blur_strength(int value);
int client_blur_strength();
void client_set_blur_average(bool value);
bool client_blur_average();
void client_set_blur_average_hz(int value);
int client_blur_average_hz();
void client_set_screen_blur(bool value);
bool client_screen_blur();
void client_set_fps_value(int value);
int client_fps_value();


void client_set_center_cursor(bool value);
bool client_center_cursor_enabled();

void client_set_render(bool value);
bool client_render_enabled();
void client_set_render_below(bool value);
bool client_render_below();
void client_set_render_above(bool value);
bool client_render_above();
void client_set_render_below_distance(int value);
int client_render_below_distance();
void client_set_render_above_distance(int value);
int client_render_above_distance();

void client_set_tablist(bool value);
bool client_tablist_enabled();

void client_set_particles(bool value);
bool client_particles_enabled();

void client_set_chat_mods(bool value);
bool client_chat_mods_enabled();
void client_set_message_blacklist(bool value);
bool client_message_blacklist();
void client_set_chat_keywords(const char* value);
void client_set_lobby_watch(bool value);
bool client_lobby_watch_enabled();
void client_set_lobby_watch_rules(const char* value);
void client_set_cc_utils(bool value);
bool client_cc_utils_enabled();
void client_set_party_invites(bool value);
bool client_party_invites_enabled();

void client_set_experimental(bool value);
bool client_experimental_enabled();

void client_set_render_horizontal(bool value);
bool client_render_horizontal();
void client_set_render_radius(int value);
int client_render_radius();

void client_set_fps_display(bool value);
bool client_fps_display_enabled();
void client_set_fps_low(bool value);
bool client_fps_low();
void client_set_fps_interval(int value);
int client_fps_interval();

void client_set_fps_font_scale(int value);
int client_fps_font_scale();
void client_set_fps_anchor(int value);
int client_fps_anchor();

void client_set_environment(bool value);
bool client_environment_enabled();
void client_set_environment_time(bool value);
bool client_environment_time();
void client_set_environment_fog(bool value);
bool client_environment_fog();
void client_set_environment_ticks(int value);
int client_environment_ticks();
void client_set_environment_hue(int value);
int client_environment_hue();
void client_set_environment_saturation(int value);
int client_environment_saturation();
void client_set_environment_value(int value);
int client_environment_value();
