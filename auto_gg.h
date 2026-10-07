#pragma once
void auto_gg_init();
const char* auto_gg_label();
const char* auto_gg_status();
void auto_gg_get_text(char* trigger, char* response); // Each buffer: 256 bytes.
void auto_gg_toggle();
bool auto_gg_is_enabled();
void auto_gg_set_enabled(bool enabled);
void auto_gg_set_trigger(const char*);
void auto_gg_set_response(const char*);
void auto_gg_on_keyboard(int action);
void auto_gg_update(bool gameplay, bool focused);
// Queue a passive Shift/right-click; targeting and sending run in a live callback.
void auto_gg_ping_click();
void auto_gg_lobby_player(void* handler, const char* name);

// Only pass borrowed handlers from native dispatch; no native object is retained.
void auto_gg_lobby_dispatch(void* handler);
void auto_gg_lobby_reset();
void auto_gg_world_reset();
