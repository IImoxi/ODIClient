#include "launcher_api.h"

// Link-time placeholders ONLY. Never install these libraries into mods:
// the launcher supplies their real implementations at runtime.
#ifdef WINDOW_API
extern "C" GameWindowHandle* game_window_get_primary_window() { return nullptr; }
extern "C" void game_window_add_window_creation_callback(void*, void (*)(void*)) {}
extern "C" void game_window_add_keyboard_callback(GameWindowHandle*, void*, bool (*)(void*, int, int)) {}
extern "C" void game_window_add_mouse_button_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, int, int)) {}
extern "C" void game_window_add_mouse_position_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, bool)) {}
extern "C" void game_window_add_mouse_scroll_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, double, double)) {}
extern "C" bool game_window_is_mouse_locked(GameWindowHandle*) { return false; }
extern "C" void game_window_add_swap_buffers_callback(void*, void (*)(void*, void*, void*)) {}
#elif defined(HOST_API)
extern "C" void* mcpelauncher_host_dlopen(const char*, int) { return nullptr; }
extern "C" void* mcpelauncher_host_dlsym(void*, const char*) { return nullptr; }
#endif
