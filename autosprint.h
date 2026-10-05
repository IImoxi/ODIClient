#pragma once
bool autosprint_init();
const char* autosprint_error();
void autosprint_bind_window();
void autosprint_update(bool enabled, bool gameplay);
bool autosprint_has_focus();
void autosprint_release_movement_keys();

void autosprint_center_cursor();
