#include <cassert>
#include <cstring>
#include <cstdio>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include "../launcher_api.h"
#include "../render_frame_trace.h"
#include "../motion_blur.h"
#include "../analog_input.h"
#include "../auto_gg.h"
#include "../client_settings.h"
#include "../client_modules.h"

struct MockNativeWindow {
    virtual ~MockNativeWindow() = default;
    virtual void makeCurrent(bool) {}
    virtual void setIcon(const void*) {}
    virtual void show() {}
    virtual void close() {}
    virtual void pollEvents() {}
    virtual void setCursorDisabled(bool value) { disabled = value; }
    virtual bool getCursorDisabled() { return disabled; }
    bool disabled = true;
};
static MockNativeWindow native;
struct GameWindowHandle { MockNativeWindow* window = &native; void* sharedPtrControl = nullptr; };
static GameWindowHandle window;
static void (*created)(void*);
static bool (*keyboard)(void*, int, int);
static bool (*mouseButton)(void*, double, double, int, int);
static int pingClicks;
static bool (*mousePosition)(void*, double, double, bool);
static void (*frame)(void*, void*, void*);
static bool menuOpen;
static bool menuReady;
static bool locked = true;
static bool hasFocus = true;
static bool keys[256];
static int ctrlDowns, ctrlUps, registrations, warps;
static bool blurActive, fpsActive, ggActive;

static float zoomDrawn;
static bool (*scroll)(void*, double, double, double, double);
void zoom_init() {}
void zoom_update(bool active, int value) { zoomDrawn = active ? value / 10.0f : 0; }
void client_settings_get_zoom(bool* enabled, int* key, int* value, int* step) { *enabled = false; *key = 67; *value = 30; *step = 5; }
void client_settings_set_zoom(bool, int, int, int) {}
void client_settings_set_center_cursor(bool) {}
bool client_settings_get_center_cursor() { return false; }
extern "C" void game_window_add_mouse_scroll_callback(GameWindowHandle*, void*, bool (*callback)(void*, double, double, double, double)) { scroll = callback; }
void auto_gg_init() {}
void auto_gg_ping_click() { ++pingClicks; }
void gpu_shader_services_init() {}
void render_init() {}
void render_trace_frame(long long) {}
RenderFrameStamp render_frame_trace_begin() { return {}; }
RenderFrameStamp render_frame_trace_stamp() { return {}; }
void render_frame_trace_record(RenderFrameStage,RenderFrameStamp) {}
void render_frame_trace_end(RenderFrameStamp) {}
void tablist_init() {}
void popup_on_mouse_button(int, int, bool) {}
void popup_render(bool, long long) {}
void environment_init() {}
void particles_init() {}
void particles_update(bool) {}
static bool tabHeld, tabCaptured;
void tablist_render(bool gameplay, long long) { if (!gameplay) tabHeld = false; }
bool tablist_on_keyboard(int key, int action, bool gameplay) {
    if (key != 9) return false;
    if (action == 2) { tabHeld = false; bool captured = tabCaptured; tabCaptured = false; return captured; }
    if (gameplay && action == 0) { tabHeld = true; tabCaptured = true; }
    return tabCaptured;
}
bool tablist_on_scroll(double) { return tabHeld; }
bool tablist_on_mouse_button(int,int,bool) { return false; }
extern "C" void game_window_add_mouse_button_callback(GameWindowHandle*,void*,bool (*callback)(void*,double,double,int,int)) { mouseButton = callback; }
LobbyWatchSettings client_settings_get_lobby_watch() { return {}; }
CCUtilsSettings client_settings_get_cc_utils() { return {}; }
void client_settings_set_cc_utils(CCUtilsSettings) {}
void client_settings_set_lobby_watch(const LobbyWatchSettings&) {}
void client_settings_get_modules(bool* sprint, bool* blur, int* strength) {
    *sprint = false; *blur = false; *strength = 30;
}
void client_settings_set_modules(bool, bool, int) {}
void client_settings_get_fps_limit(bool* enabled, int* limit) { *enabled = false; *limit = 120; }
void client_settings_set_fps_limit(bool, int) {}
void client_settings_get_blur(bool* average, int* hz, bool* screen) {
    *average = false; *hz = 60; *screen = false;
}
void client_settings_set_blur(bool, int, bool) {}
void auto_gg_on_keyboard(int) {}
void auto_gg_update(bool gameplay, bool focused) { ggActive = gameplay && focused; }
bool auto_gg_is_enabled() { return false; }
void auto_gg_set_enabled(bool) {}

void analog_input_preinit() {}
bool analog_input_supported() { return false; }
bool analog_input_connected() { return false; }
bool analog_input_sensor_ready() { return false; }
void analog_input_update(bool, bool, bool, bool) {}
const char* motion_blur_error() { return nullptr; }
void motion_blur_render(bool enabled, float, float, bool, int, long long) { blurActive = enabled; }
bool custom_menu_is_visible() { return menuOpen; }
bool custom_menu_captures_input() { return menuOpen && menuReady; }
bool custom_menu_on_keyboard(int key, int action) {
    if (key == 27 && menuOpen) {
        if (action == 0) menuOpen = false;
        return true;
    }
    if (key != 76 || (!menuOpen && !game_window_is_mouse_locked(&window))) return false;
    if (action == 0) menuOpen = !menuOpen;
    return true;
}
void custom_menu_register_mouse_callback(GameWindowHandle*) {}
void custom_menu_render() {}
const char* fps_limiter_error() { return nullptr; }
void fps_limiter_wait(bool enabled, bool active, int) { fpsActive = enabled && active; }
long long fps_limiter_frame_timestamp_ns() { return 1; }
long long fps_limiter_frame_delta_ns() { return 0; }

extern "C" GameWindowHandle* game_window_get_primary_window() { return &window; }
extern "C" bool game_window_is_mouse_locked(GameWindowHandle*) { return locked; }
extern "C" void game_window_add_swap_buffers_callback(void*, void (*callback)(void*, void*, void*)) { frame = callback; }
extern "C" void game_window_add_window_creation_callback(void* user, void (*callback)(void*)) {
    assert(!user);
    ++registrations;
    created = callback;
}
extern "C" void game_window_add_keyboard_callback(GameWindowHandle*, void*, bool (*callback)(void*, int, int)) {
    keyboard = callback;
}
extern "C" void game_window_add_mouse_position_callback(GameWindowHandle*, void*, bool (*callback)(void*, double, double, bool)) {
    mousePosition = callback;
}

static Display* openDisplay(const char*) { return reinterpret_cast<Display*>(&window); }
static Window getRoot(Display*) { return 99; }
static int getFocus(Display*, Window* focused, int* revert) {
    *focused = hasFocus ? 100 : 200;
    *revert = 0;
    return 1;
}
static KeyCode keycode(Display*, KeySym sym) {
    if (sym == XK_w) return 25;
    if (sym == XK_Control_L) return 37;
    if (sym == XK_Shift_L) return 50;
    if (sym == XK_Shift_R) return 62;
    return 0;
}
static int getKeys(Display*, char* result) {
    std::memset(result, 0, 32);
    for (int i = 0; i < 256; ++i)
        if (keys[i]) result[i / 8] |= 1 << (i % 8);
    return 1;
}
static int flush(Display*) { return 1; }
static Status push(Display*, Window target, Bool propagate, long mask, XEvent* event) {
    assert(target == 100 && event->xkey.window == 100);
    assert(!propagate && event->xkey.root == 99 && event->xkey.keycode == 37);
    if (event->type == KeyPress) {
        assert(mask == KeyPressMask);
        ++ctrlDowns;
    } else {
        assert(event->type == KeyRelease && mask == KeyReleaseMask);
        ++ctrlUps;
    }
    return true;
}
static int warpPointer(Display*, Window source, Window target, int, int, unsigned, unsigned, int x, int y) {
    assert(source == None && target == 100 && x == 640 && y == 360); ++warps; return 1;
}
static Status attributes(Display*, Window target, XWindowAttributes* out) {
    assert(target == 100); out->width = 1280; out->height = 720; return 1;
}
extern "C" void* mcpelauncher_host_dlopen(const char* path, int flags) {
    if (std::strcmp(path, "libSDL3.so.0") == 0) return nullptr;
    assert(std::strcmp(path, "libX11.so.6") == 0 && flags == 2);
    return &window;
}
extern "C" void* mcpelauncher_host_dlsym(void*, const char* name) {
    if (std::strcmp(name, "XOpenDisplay") == 0) return reinterpret_cast<void*>(openDisplay);
    if (std::strcmp(name, "XDefaultRootWindow") == 0) return reinterpret_cast<void*>(getRoot);
    if (std::strcmp(name, "XGetInputFocus") == 0) return reinterpret_cast<void*>(getFocus);
    if (std::strcmp(name, "XKeysymToKeycode") == 0) return reinterpret_cast<void*>(keycode);
    if (std::strcmp(name, "XQueryKeymap") == 0) return reinterpret_cast<void*>(getKeys);
    if (std::strcmp(name, "XSendEvent") == 0) return reinterpret_cast<void*>(push);
    if (std::strcmp(name, "XFlush") == 0) return reinterpret_cast<void*>(flush);
    if (std::strcmp(name, "XWarpPointer") == 0) return reinterpret_cast<void*>(warpPointer);
    if (std::strcmp(name, "XGetWindowAttributes") == 0) return reinterpret_cast<void*>(attributes);
    assert(false);
    return nullptr;
}

extern "C" void mod_preinit();
extern "C" void mod_init();
static void tick() { frame(nullptr, nullptr, nullptr); }

int main() {
    mod_preinit();
    mod_preinit();
    mod_init();
    assert(registrations == 1 && created && !keyboard);
    created(nullptr);
    assert(keyboard && frame);
    tick(); client_set_center_cursor(true);
    client_set_blur(true);
    client_set_fps_limit(true);

    assert(keyboard(nullptr, 76, 0) && menuOpen);
    assert(keyboard(nullptr, 76, 1) && menuOpen);
    assert(keyboard(nullptr, 76, 2) && menuOpen);
    tick();
    assert(native.disabled && !mousePosition(nullptr, 1, 2, true));
    assert(blurActive && fpsActive && ggActive);
    menuReady = true;
    tick();
    assert(warps == 1); tick(); assert(warps == 1);
    assert(!native.disabled && mousePosition(nullptr, 1, 2, true));
    assert(!mousePosition(nullptr, 1, 2, false));

    keys[25] = true;
    client_set_sprint(true);
    tick();
    assert(ctrlDowns == 0); // The custom menu blocks gameplay sprinting.
    assert(blurActive && fpsActive && ggActive);
    locked = false; // A Minecraft screen still pauses blur and AutoGG.
    tick();
    assert(!blurActive && fpsActive && !ggActive);
    locked = true;
    hasFocus = false;
    tick();
    assert(!ggActive);
    hasFocus = true;
    assert(keyboard(nullptr, 76, 0) && !menuOpen);
    tick();
    assert(native.disabled && ctrlDowns == 1);
    assert(keyboard(nullptr, 27, 0) == false);

    assert(keyboard(nullptr, 76, 0) && menuOpen);
    tick();
    assert(ctrlUps == 1 && !native.disabled);
    assert(keyboard(nullptr, 27, 0) && !menuOpen);
    tick();
    assert(native.disabled && ctrlUps == 2);
    keys[25] = false;
    tick();

    assert(!client_zoom_enabled() && client_zoom_key() == 67);
    assert(!keyboard(nullptr, 67, 0)); tick(); assert(zoomDrawn == 0);
    assert(keyboard(nullptr, 9, 0) && tabHeld);
    assert(scroll(nullptr, 0, 0, 0, -1));
    assert(!mousePosition(nullptr, 1, 1, true));
    assert(keyboard(nullptr, 9, 2) && !tabHeld);
    client_set_zoom(true);
    assert(keyboard(nullptr, 67, 0)); tick(); assert(zoomDrawn == 3.0f);
    assert(scroll(nullptr, 0, 0, 0, 1)); tick(); assert(zoomDrawn == 3.5f);
    for (int i = 0; i < 100; ++i) scroll(nullptr, 0, 0, 0, 1);
    tick(); assert(zoomDrawn == 30.0f);
    assert(scroll(nullptr, 0, 0, 0, -1)); tick(); assert(zoomDrawn == 29.5f);
    for (int i = 0; i < 100; ++i) scroll(nullptr, 0, 0, 0, -1);
    tick(); assert(zoomDrawn == 1.5f);
    assert(scroll(nullptr, 0, 0, 0, 1)); tick(); assert(zoomDrawn == 2.0f);
    assert(keyboard(nullptr, 67, 2)); zoomDrawn = 0; tick(); assert(zoomDrawn == 0);
    assert(!scroll(nullptr, 0, 0, 0, 1));
    assert(client_zoom_scroll() == 5);
    client_set_zoom_scroll(1); assert(client_zoom_scroll() == 1);
    assert(keyboard(nullptr, 67, 0));
    assert(scroll(nullptr, 0, 0, 0, 1)); tick(); assert(zoomDrawn == 3.1f);
    client_set_zoom_scroll(50);
    assert(scroll(nullptr, 0, 0, 0, 1)); tick(); assert(zoomDrawn == 8.1f);
    assert(scroll(nullptr, 0, 0, 0, -1)); tick(); assert(zoomDrawn == 3.1f);
    assert(keyboard(nullptr, 67, 2));
    client_set_zoom_scroll(0); assert(client_zoom_scroll() == 1);
    client_set_zoom_scroll(60); assert(client_zoom_scroll() == 50);
    client_set_zoom_scroll(5);
    client_set_zoom_default(40); assert(client_zoom_default() == 40);
    client_set_zoom_default(30);
    client_set_zoom_key(90); assert(client_zoom_key() == 90);
    client_set_zoom_key(76); assert(client_zoom_key() == 90);
    assert(!keyboard(nullptr, 67, 0));
    client_set_zoom_default(40);
    assert(keyboard(nullptr, 90, 0)); tick(); assert(zoomDrawn == 4.0f);
    assert(scroll(nullptr, 0, 0, 0, 1)); tick(); assert(zoomDrawn == 4.5f);
    assert(keyboard(nullptr, 90, 2));
    assert(keyboard(nullptr, 90, 0)); tick(); assert(zoomDrawn == 4.0f);
    assert(keyboard(nullptr, 90, 2));
    client_set_zoom_default(1); assert(client_zoom_default() == 15);
    client_set_zoom_default(400); assert(client_zoom_default() == 300);
    client_set_zoom_default(30);
    assert(keyboard(nullptr, 90, 0)); tick(); assert(zoomDrawn == 3.0f);
    client_set_zoom(false); zoomDrawn = 0; tick(); assert(zoomDrawn == 0);
    assert(!keyboard(nullptr, 90, 0)); assert(keyboard(nullptr, 90, 2));
    client_set_zoom(true); assert(keyboard(nullptr, 90, 0));
    hasFocus = false; zoomDrawn = 0; tick(); assert(zoomDrawn == 0);
    hasFocus = true; tick(); assert(zoomDrawn == 0);
    assert(keyboard(nullptr, 90, 2));
    assert(keyboard(nullptr, 90, 0)); locked = false; zoomDrawn = 0;
    tick(); assert(zoomDrawn == 0); locked = true; assert(keyboard(nullptr, 90, 2));
    assert(keyboard(nullptr, 90, 0)); assert(keyboard(nullptr, 76, 0));
    zoomDrawn = 0; tick(); assert(zoomDrawn == 0);
    assert(!scroll(nullptr, 0, 0, 0, 1)); assert(keyboard(nullptr, 90, 2));
    assert(keyboard(nullptr, 76, 0));
    hasFocus = false;
    client_set_sprint(false);
    menuOpen = false; locked = true; native.disabled = true; hasFocus = true; tick();
    int before = warps;
    locked = false; native.disabled = false; tick(); assert(warps == before + 1);
    tick(); assert(warps == before + 1);
    locked = true; native.disabled = true; tick(); client_set_center_cursor(false);
    locked = false; native.disabled = false; tick(); assert(warps == before + 1);
    locked = true; tick();
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 0);
    keys[50] = true;
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 1);
    assert(!mouseButton(nullptr, 0, 0, 2, 2) && pingClicks == 1);
    assert(!mouseButton(nullptr, 0, 0, 1, 0) && pingClicks == 1);
    keys[50] = false; keys[62] = true;
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 2);
    menuOpen = true;
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 2);
    menuOpen = false; hasFocus = false;
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 2);
    hasFocus = true; locked = false;
    assert(!mouseButton(nullptr, 0, 0, 2, 0) && pingClicks == 2);
    std::puts("PASS: zoom hold/rebind/scroll bounds/disable/focus/menu handling; L opens the custom-menu route, Escape closes it, and cursor/sprint state follows the menu; blur, FPS cap, and AutoGG keep running");
}

ChatModsSettings client_settings_get_chat_mods() { return {}; }
void client_settings_set_chat_mods(const ChatModsSettings&) {}

FpsDisplaySettings client_settings_get_fps_display() { return {}; }
void client_settings_set_fps_display(FpsDisplaySettings) {}
void fps_display_render(bool, long long) {}

bool display_layout_begin_frame() { return true; }
