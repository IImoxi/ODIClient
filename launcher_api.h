#pragma once

// C ABI provided by the launcher; no Minecraft internals or ImGui headers needed.
struct GameWindowHandle;
extern "C" {
GameWindowHandle* game_window_get_primary_window();
void game_window_add_window_creation_callback(void* user, void (*callback)(void*));
void game_window_add_keyboard_callback(GameWindowHandle*, void* user,
                                       bool (*callback)(void*, int key, int action));
void game_window_add_mouse_button_callback(GameWindowHandle*, void* user,
                                           bool (*callback)(void*, double x, double y,
                                                            int button, int action));
void game_window_add_mouse_position_callback(GameWindowHandle*, void* user,
                                             bool (*callback)(void*, double, double, bool relative));
void game_window_add_mouse_scroll_callback(GameWindowHandle*, void* user,
    bool (*callback)(void*, double, double, double, double));
bool game_window_is_mouse_locked(GameWindowHandle*);
void game_window_add_swap_buffers_callback(void* user, void (*callback)(void*, void*, void*));
void* mcpelauncher_host_dlopen(const char* path, int flags);
void* mcpelauncher_host_dlsym(void* handle, const char* name);
}
