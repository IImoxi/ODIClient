#include <cassert>
#include <cstring>
#include <cstdio>
#include "../autosprint.cpp"
struct NativeWindow { void** table; alignas(8) unsigned char callbacks[18 * 32]{}; };
static NativeWindow window{};
static void* handle = &window;
static bool physical[512]{};
static void* focused = &window;
static int downs, ups, warps;
extern "C" GameWindowHandle* game_window_get_primary_window() { return reinterpret_cast<GameWindowHandle*>(&handle); }
static void invokeKey(const void*, int* key, int* action, int*) {
    assert(*key == 17); // Launcher Left Ctrl, not GLFW's 341.
    if (*action == 0) ++downs; else { assert(*action == 2); ++ups; }
}
static const char* driver() { return "wayland"; }
static void* focus() { return focused; }
static const bool* keys(int* count) { *count = 512; return physical; }
static bool size(void*, int* w, int* h) { *w = 1280; *h = 720; return true; }
static void warp(void* target, float x, float y) {
    assert(target == &window && x == 640 && y == 360); ++warps;
}
extern "C" void* mcpelauncher_host_dlopen(const char* name, int) {
    assert(std::strcmp(name, "libSDL3.so.0") == 0); return &window;
}
extern "C" void* mcpelauncher_host_dlsym(void*, const char* name) {
#define SYMBOL(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<void*>(f)
    SYMBOL("SDL_GetCurrentVideoDriver", driver);
    SYMBOL("SDL_GetKeyboardFocus", focus);
    SYMBOL("SDL_GetKeyboardState", keys);
    SYMBOL("SDL_GetWindowSize", size);
    SYMBOL("SDL_WarpMouseInWindow", warp);
#undef SYMBOL
    assert(false); return nullptr;
}
int main() {
    *reinterpret_cast<void**>(window.callbacks + 9 * 32 + 24) = reinterpret_cast<void*>(invokeKey);
    assert(autosprint_init()); autosprint_bind_window();
    autosprint_update(true, true); assert(autosprint_has_focus() && downs == 0);
    physical[26] = true; autosprint_update(true, true); assert(downs == 1);
    autosprint_update(true, true); assert(downs == 1);
    physical[225] = true; autosprint_update(true, true); assert(ups == 1);
    physical[225] = false; autosprint_update(true, true); assert(downs == 2);
    physical[224] = true; autosprint_update(false, true); assert(ups == 1);
    physical[26] = physical[224] = false;
    autosprint_update(true, true); assert(ups == 1);
    physical[26] = true; autosprint_update(true, true); assert(downs == 3);
    focused = nullptr; autosprint_update(true, true); assert(ups == 2 && !autosprint_has_focus());
    autosprint_center_cursor(); assert(warps == 0);
    focused = &window; autosprint_update(true, true); assert(ups == 3);
    autosprint_update(true, true); assert(downs == 4);
    autosprint_update(true, false); assert(ups == 4);
    autosprint_center_cursor(); assert(warps == 1);
    autosprint_update(true, true); assert(ups == 5); // Clear the menu release.
    autosprint_update(true, true); assert(downs == 5);
    physical[26] = false; autosprint_update(true, true); assert(ups == 6);
    physical[26] = true; autosprint_update(true, true); assert(downs == 6);
    autosprint_update(false, true); assert(ups == 7);
    std::puts("PASS: SDL Wayland sprint, physical Ctrl, Shift, focus/menu releases, and focused cursor centering");
}
