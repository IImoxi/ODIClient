#include "launcher_api.h"
#include "autosprint.h"
#include "motion_blur.h"
#include "zoom.h"
#include "native_cursor.h"
#include "analog_input.h"
#include "auto_gg.h"
#include "client_settings.h"
#include "fps_limiter.h"
#include "fps_display.h"
#include "display_layout.h"
#include "render.h"
#include "render_frame_trace.h"
#include "gpu_shader_services.h"
#include "tablist.h"
#include "particles.h"
#include "environment.h"
#include "projection_jitter.h"
#include "popup.h"
#include "custom_menu.h"
#include "client_modules.h"

namespace {
bool zoomEnabled = false, zoomHeld = false;
int zoomDefault = 30, zoomScroll = 5;
int zoomKey = 67, zoomLevel = 30; // 3x, represented in tenths.
bool zoomConsumed[512]{};
bool initialized = false;
bool sprintEnabled = false;
bool sprintReady = false;
bool blurEnabled = false;
bool screenBlurEnabled = false;
bool blurFpsAverage = false;
int blurAverageHz = 60;
int blurStrength = 30;
bool fpsLimitEnabled = false;
bool jitterEnabled = false;
int jitterSampleMode = 0;
bool fpsNative = false;
int fpsLimit = 120;
bool centerCursorEnabled = false;
bool cursorWasCaptured = false;
bool cursorReleased = false;
bool restoreCursor = false;
bool analogEnabled = false;
bool analogCapture = false;

void updateCursor(bool menuCapturesInput) {
    auto handle = game_window_get_primary_window();
    if (menuCapturesInput) {
        if (!cursorReleased) {
            restoreCursor = cursorDisabled(handle);
            cursorReleased = true;
        }
        if (cursorDisabled(handle))
            setCursorDisabled(handle, false);
    } else if (cursorReleased) {
        // Respect a game screen that requested an unlocked cursor meanwhile.
        setCursorDisabled(handle, restoreCursor && game_window_is_mouse_locked(handle));
        cursorReleased = false;
    }
}

bool blockRelativeMouseMotion(void*, double, double, bool relative) {
    // Absolute motion still reaches the custom menu for pointer input.
    return relative && custom_menu_captures_input();
}

bool isSprintReady() {
    return __atomic_load_n(&sprintReady, __ATOMIC_ACQUIRE);
}

bool zoomGameplay() {
    return !custom_menu_is_visible()
        && game_window_is_mouse_locked(game_window_get_primary_window())
        && isSprintReady() && autosprint_has_focus();
}
bool onMouseButton(void*, double, double, int button, int action) {
    popup_on_mouse_button(button, action, isSprintReady() && autosprint_has_focus());
    if (tablist_on_mouse_button(button, action, zoomGameplay())) return true;
    if (button == 2 && action == 0 && zoomGameplay() && autosprint_shift_down())
        auto_gg_ping_click();
    return false;
}
bool onScroll(void*, double, double, double, double dy) {
    if (zoomGameplay() && tablist_on_scroll(dy)) return true;
    if (!__atomic_load_n(&zoomEnabled, __ATOMIC_RELAXED)
        || !__atomic_load_n(&zoomHeld, __ATOMIC_RELAXED) || !zoomGameplay()) return false;
    if (dy > 0 || dy < 0) {
        int old = __atomic_load_n(&zoomLevel, __ATOMIC_RELAXED), next;
        do {
            int step = client_zoom_scroll();
            next = old + (dy > 0 ? step : -step);
            if (next < 15) next = 15;
            if (next > 300) next = 300;
        } while (!__atomic_compare_exchange_n(&zoomLevel, &old, next, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    }
    zoom_update(true, __atomic_load_n(&zoomLevel, __ATOMIC_RELAXED));
    return true;
}
void onFrame(void*, void*, void*) {
    auto frameTrace=render_frame_trace_begin();
    bool gameActive = game_window_is_mouse_locked(game_window_get_primary_window());
    bool gameplay = !custom_menu_captures_input() && gameActive;
    // Inventory screens can release mouse capture while Minecraft is still
    // rendering. Keep the cap active there and while the client menu is open.
    auto limiterTrace=render_frame_trace_stamp();
    fps_limiter_native_update(client_fps_limit_enabled() && client_fps_native(), client_fps_value());
    fps_limiter_wait(__atomic_load_n(&fpsLimitEnabled, __ATOMIC_RELAXED), true,
                     __atomic_load_n(&fpsLimit, __ATOMIC_RELAXED));
    render_frame_trace_record(FrameLimiter,limiterTrace);
    bool adaptiveAverage = __atomic_load_n(&blurFpsAverage, __ATOMIC_RELAXED);
    int averageHz = adaptiveAverage ? __atomic_load_n(&blurAverageHz, __ATOMIC_RELAXED) : 0;
    bool blurOn = __atomic_load_n(&blurEnabled, __ATOMIC_RELAXED) && gameActive;
    long long frameDeltaNs = fps_limiter_frame_delta_ns();
    render_trace_frame(frameDeltaNs);
    if (adaptiveAverage && frameDeltaNs > 1000000000LL / averageHz) blurOn = false;
    auto overlayTrace=render_frame_trace_stamp();
    motion_blur_render(blurOn,
                       __atomic_load_n(&blurStrength, __ATOMIC_RELAXED) / 100.0f,
                       1.0f,
                       __atomic_load_n(&screenBlurEnabled, __ATOMIC_RELAXED),
                       averageHz,
                       fps_limiter_frame_timestamp_ns());
    particles_update(zoomGameplay());
    tablist_render(zoomGameplay(), fps_limiter_frame_timestamp_ns());
    display_layout_begin_frame();
    auto viewport = display_layout_viewport();
    projection_jitter_frame(viewport.width, viewport.height);
    fps_display_render(isSprintReady() && autosprint_has_focus(), fps_limiter_frame_timestamp_ns());
    custom_menu_render();
    popup_render(isSprintReady() && autosprint_has_focus(), fps_limiter_frame_timestamp_ns());
    render_frame_trace_record(FrameOverlays,overlayTrace);
    // Native cursor changes belong on the frame thread.
    updateCursor(custom_menu_captures_input());
    bool captured = cursorDisabled(game_window_get_primary_window());
    if (cursorWasCaptured && !captured && client_center_cursor_enabled() && isSprintReady())
        autosprint_center_cursor();
    cursorWasCaptured = captured;
    gameplay = !custom_menu_captures_input()
        && game_window_is_mouse_locked(game_window_get_primary_window());
    if (isSprintReady())
        autosprint_update(__atomic_load_n(&sprintEnabled, __ATOMIC_RELAXED)
                         && !__atomic_load_n(&analogCapture, __ATOMIC_RELAXED),
                         gameplay);
    if (!zoomGameplay()) { __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0); }
    zoom_update(__atomic_load_n(&zoomEnabled, __ATOMIC_RELAXED)
                && __atomic_load_n(&zoomHeld, __ATOMIC_RELAXED),
                __atomic_load_n(&zoomLevel, __ATOMIC_RELAXED));
    bool analogOn = __atomic_load_n(&analogEnabled, __ATOMIC_RELAXED);
    analog_input_update(analogOn, gameplay, isSprintReady() && autosprint_has_focus(),
                        __atomic_load_n(&sprintEnabled, __ATOMIC_RELAXED));
    bool capture = analogOn && analog_input_connected() && gameplay;
    if (__atomic_exchange_n(&analogCapture, capture, __ATOMIC_RELAXED) != capture && capture)
        autosprint_release_movement_keys();
    auto_gg_update(game_window_is_mouse_locked(game_window_get_primary_window()), isSprintReady() && autosprint_has_focus());
    render_frame_trace_end(frameTrace);
}

bool onKeyboard(void*, int key, int action) {
    auto_gg_on_keyboard(action);
    // Retry connection initialization if the display was initially unavailable.
    if (!isSprintReady() && autosprint_init())
        __atomic_store_n(&sprintReady, true, __ATOMIC_RELEASE);
    if (isSprintReady())
        autosprint_bind_window();
    if ((key == 87 || key == 65 || key == 83 || key == 68) && action != 2
        && __atomic_load_n(&analogCapture, __ATOMIC_RELAXED))
        return true; // Let releases through to clear any old digital movement.
    bool menuConsumed = custom_menu_on_keyboard(key, action);
    bool tabConsumed = tablist_on_keyboard(key, action, !menuConsumed && zoomGameplay());
    if (tabConsumed) return true;
    if (key >= 0 && key < 512 && action == 2 && zoomConsumed[key]) {
        zoomConsumed[key] = false;
        __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0);
        return true;
    }
    if (menuConsumed) { __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0); return true; }
    if (key == __atomic_load_n(&zoomKey, __ATOMIC_RELAXED)
        && __atomic_load_n(&zoomEnabled, __ATOMIC_RELAXED) && zoomGameplay()) {
        if (action == 0) {
            __atomic_store_n(&zoomLevel, client_zoom_default(), __ATOMIC_RELAXED);
            __atomic_store_n(&zoomHeld, true, __ATOMIC_RELAXED);
            zoom_update(true, client_zoom_default());
        }
        if (action == 2) { __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0); }
        if (key >= 0 && key < 512 && action != 2) zoomConsumed[key] = true;
        return action != 2;
    }
    return false;
}

void onWindowCreated(void*) {
    __atomic_store_n(&sprintReady, autosprint_init(), __ATOMIC_RELEASE);
    game_window_add_keyboard_callback(game_window_get_primary_window(), nullptr, onKeyboard);
    game_window_add_mouse_position_callback(game_window_get_primary_window(), nullptr, blockRelativeMouseMotion);
    game_window_add_mouse_scroll_callback(game_window_get_primary_window(), nullptr, onScroll);
    game_window_add_mouse_button_callback(game_window_get_primary_window(), nullptr, onMouseButton);
    custom_menu_register_mouse_callback(game_window_get_primary_window());
    game_window_add_swap_buffers_callback(nullptr, onFrame);
}
}

void client_set_center_cursor(bool value) {
    __atomic_store_n(&centerCursorEnabled, value, __ATOMIC_RELAXED);
    client_settings_set_center_cursor(value);
}
bool client_center_cursor_enabled() { return __atomic_load_n(&centerCursorEnabled, __ATOMIC_RELAXED); }

void client_set_zoom(bool value) {
    __atomic_store_n(&zoomEnabled, value, __ATOMIC_RELAXED);
    __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0);
    client_settings_set_zoom(value, client_zoom_key(), client_zoom_default(), client_zoom_scroll());
}
bool client_zoom_enabled() { return __atomic_load_n(&zoomEnabled, __ATOMIC_RELAXED); }
int client_zoom_key() { return __atomic_load_n(&zoomKey, __ATOMIC_RELAXED); }
void client_set_zoom_key(int value) {
    if (value < 32 || value >= 512 || value == 76 || value == 256) return;
    __atomic_store_n(&zoomKey, value, __ATOMIC_RELAXED);
    __atomic_store_n(&zoomHeld, false, __ATOMIC_RELAXED); zoom_update(false, 0);
    client_settings_set_zoom(client_zoom_enabled(), value, client_zoom_default(), client_zoom_scroll());
}
int client_zoom_default() { return __atomic_load_n(&zoomDefault, __ATOMIC_RELAXED); }
void client_set_zoom_default(int value) {
    if (value < 15) value = 15;
    if (value > 300) value = 300;
    __atomic_store_n(&zoomDefault, value, __ATOMIC_RELAXED);
    client_settings_set_zoom(client_zoom_enabled(), client_zoom_key(), value, client_zoom_scroll());
}
int client_zoom_scroll() { return __atomic_load_n(&zoomScroll, __ATOMIC_RELAXED); }
void client_set_zoom_scroll(int value) {
    if (value < 1) value = 1;
    if (value > 50) value = 50;
    __atomic_store_n(&zoomScroll, value, __ATOMIC_RELAXED);
    client_settings_set_zoom(client_zoom_enabled(), client_zoom_key(), client_zoom_default(), value);
}
void client_set_sprint(bool value) {
    if (!isSprintReady()) return;
    __atomic_store_n(&sprintEnabled, value, __ATOMIC_RELAXED);
    client_settings_set_modules(value, __atomic_load_n(&blurEnabled, __ATOMIC_RELAXED),
                                __atomic_load_n(&blurStrength, __ATOMIC_RELAXED));
}
bool client_sprint_enabled() { return __atomic_load_n(&sprintEnabled, __ATOMIC_RELAXED); }
void client_set_blur(bool value) {
    __atomic_store_n(&blurEnabled, value, __ATOMIC_RELAXED);
    client_settings_set_modules(__atomic_load_n(&sprintEnabled, __ATOMIC_RELAXED), value,
                                __atomic_load_n(&blurStrength, __ATOMIC_RELAXED));
}
bool client_blur_enabled() { return __atomic_load_n(&blurEnabled, __ATOMIC_RELAXED); }
void client_set_fps_limit(bool value) {
    __atomic_store_n(&fpsLimitEnabled, value, __ATOMIC_RELAXED);
    client_settings_set_fps_limit(value, __atomic_load_n(&fpsLimit, __ATOMIC_RELAXED));
}
bool client_fps_limit_enabled() { return __atomic_load_n(&fpsLimitEnabled, __ATOMIC_RELAXED); }
void client_set_fps_native(bool value) {
    __atomic_store_n(&fpsNative, value, __ATOMIC_RELAXED);
    client_settings_set_fps_native(value);
}
bool client_fps_native() { return __atomic_load_n(&fpsNative, __ATOMIC_RELAXED); }
bool client_fps_native_unavailable() { return client_fps_native() && !fps_limiter_native_supported(); }
void client_set_analog(bool value) {
    if (!analog_input_supported()) return;
    __atomic_store_n(&analogEnabled, value, __ATOMIC_RELAXED);
}
bool client_analog_enabled() { return __atomic_load_n(&analogEnabled, __ATOMIC_RELAXED); }
void client_set_auto_gg(bool value) { auto_gg_set_enabled(value); }
bool client_auto_gg_enabled() { return auto_gg_is_enabled(); }
void client_set_blur_strength(int value) {
    if (value < 0) value = 0;
    if (value > 80) value = 80;
    __atomic_store_n(&blurStrength, value, __ATOMIC_RELAXED);
    client_settings_set_modules(__atomic_load_n(&sprintEnabled, __ATOMIC_RELAXED),
                                __atomic_load_n(&blurEnabled, __ATOMIC_RELAXED), value);
}
int client_blur_strength() { return __atomic_load_n(&blurStrength, __ATOMIC_RELAXED); }
void client_set_blur_average(bool value) {
    __atomic_store_n(&blurFpsAverage, value, __ATOMIC_RELAXED);
    client_settings_set_blur(value, __atomic_load_n(&blurAverageHz, __ATOMIC_RELAXED),
                              __atomic_load_n(&screenBlurEnabled, __ATOMIC_RELAXED));
}
bool client_blur_average() { return __atomic_load_n(&blurFpsAverage, __ATOMIC_RELAXED); }
void client_set_blur_average_hz(int value) {
    if (value < 30) value = 30;
    if (value > 500) value = 500;
    __atomic_store_n(&blurAverageHz, value, __ATOMIC_RELAXED);
    client_settings_set_blur(__atomic_load_n(&blurFpsAverage, __ATOMIC_RELAXED), value,
                              __atomic_load_n(&screenBlurEnabled, __ATOMIC_RELAXED));
}
int client_blur_average_hz() { return __atomic_load_n(&blurAverageHz, __ATOMIC_RELAXED); }
void client_set_screen_blur(bool value) {
    __atomic_store_n(&screenBlurEnabled, value, __ATOMIC_RELAXED);
    client_settings_set_blur(__atomic_load_n(&blurFpsAverage, __ATOMIC_RELAXED),
                              __atomic_load_n(&blurAverageHz, __ATOMIC_RELAXED), value);
}
bool client_screen_blur() { return __atomic_load_n(&screenBlurEnabled, __ATOMIC_RELAXED); }
void client_set_fps_value(int value) {
    if (value < 30) value = 30;
    if (value > 480) value = 480;
    __atomic_store_n(&fpsLimit, value, __ATOMIC_RELAXED);
    client_settings_set_fps_limit(__atomic_load_n(&fpsLimitEnabled, __ATOMIC_RELAXED), value);
}
int client_fps_value() { return __atomic_load_n(&fpsLimit, __ATOMIC_RELAXED); }

void client_set_jitter(bool value) { __atomic_store_n(&jitterEnabled, value, __ATOMIC_RELAXED); }
bool client_jitter_enabled() { return __atomic_load_n(&jitterEnabled, __ATOMIC_RELAXED); }
void client_set_jitter_sample_mode(int value) {
    if (value < 0) value = 0;
    if (value > 2) value = 2;
    __atomic_store_n(&jitterSampleMode, value, __ATOMIC_RELAXED);
}
int client_jitter_sample_mode() { return __atomic_load_n(&jitterSampleMode, __ATOMIC_RELAXED); }
bool client_jitter_active() {
    return client_jitter_enabled() && zoomGameplay();
}

extern "C" __attribute__((visibility("default"))) void mod_preinit() {
    if (!initialized) {
        initialized = true;
        analog_input_preinit();
        game_window_add_window_creation_callback(nullptr, onWindowCreated);
    }
}

extern "C" __attribute__((visibility("default"))) void mod_init() {
    auto_gg_init();
    bool fpsEnabled;
    int fpsValue;
    client_settings_get_fps_limit(&fpsEnabled, &fpsValue);
    fps_limiter_init(fpsEnabled && client_settings_get_fps_native());
    zoom_init();
    gpu_shader_services_init();
    projection_jitter_init();
    environment_init();
    render_init();
    tablist_init();
    particles_init();
    __atomic_store_n(&centerCursorEnabled, client_settings_get_center_cursor(), __ATOMIC_RELAXED);
    bool zoom; int key, defaultLevel, scrollStep;
    client_settings_get_zoom(&zoom, &key, &defaultLevel, &scrollStep);
    __atomic_store_n(&zoomScroll, scrollStep, __ATOMIC_RELAXED);
    __atomic_store_n(&zoomDefault, defaultLevel, __ATOMIC_RELAXED);
    __atomic_store_n(&zoomEnabled, zoom, __ATOMIC_RELAXED);
    __atomic_store_n(&zoomKey, key, __ATOMIC_RELAXED);
    bool sprint, blur;
    int strength;
    client_settings_get_modules(&sprint, &blur, &strength);
    bool fpsAverage, screenBlur;
    int averageHz;
    client_settings_get_blur(&fpsAverage, &averageHz, &screenBlur);
    __atomic_store_n(&sprintEnabled, sprint, __ATOMIC_RELAXED);
    __atomic_store_n(&blurEnabled, blur, __ATOMIC_RELAXED);
    __atomic_store_n(&blurStrength, strength, __ATOMIC_RELAXED);
    __atomic_store_n(&fpsLimitEnabled, fpsEnabled, __ATOMIC_RELAXED);
    __atomic_store_n(&fpsLimit, fpsValue, __ATOMIC_RELAXED);
    __atomic_store_n(&fpsNative, client_settings_get_fps_native(), __ATOMIC_RELAXED);
    fps_limiter_native_update(fpsEnabled && client_fps_native(), fpsValue);
    __atomic_store_n(&blurFpsAverage, fpsAverage, __ATOMIC_RELAXED);
    __atomic_store_n(&blurAverageHz, averageHz, __ATOMIC_RELAXED);
    __atomic_store_n(&screenBlurEnabled, screenBlur, __ATOMIC_RELAXED);
}

void client_set_chat_mods(bool value) {
    auto settings = client_settings_get_chat_mods(); settings.enabled = value;
    client_settings_set_chat_mods(settings);
}
bool client_chat_mods_enabled() { return client_settings_get_chat_mods().enabled; }
void client_set_message_blacklist(bool value) {
    auto settings = client_settings_get_chat_mods(); settings.blacklist = value;
    client_settings_set_chat_mods(settings);
}
bool client_message_blacklist() { return client_settings_get_chat_mods().blacklist; }
void client_set_chat_keywords(const char* value) {
    if (!value) return;
    auto settings = client_settings_get_chat_mods();
    unsigned int i = 0;
    for (; i < sizeof(settings.keywords) - 1 && value[i]; ++i) settings.keywords[i] = value[i];
    if (value[i]) return;
    settings.keywords[i] = 0;
    client_settings_set_chat_mods(settings);
}
void client_set_lobby_watch(bool value) {
    auto settings = client_settings_get_lobby_watch(); settings.enabled = value;
    client_settings_set_lobby_watch(settings);
}
bool client_lobby_watch_enabled() { return client_settings_get_lobby_watch().enabled; }
void client_set_lobby_watch_rules(const char* value) {
    if (!value) return;
    auto settings = client_settings_get_lobby_watch();
    unsigned int i = 0;
    for (; i < sizeof(settings.rules) - 1 && value[i]; ++i) settings.rules[i] = value[i];
    if (value[i]) return;
    settings.rules[i] = 0;
    client_settings_set_lobby_watch(settings);
}
void client_set_cc_utils(bool value) {
    auto settings = client_settings_get_cc_utils(); settings.enabled = value;
    client_settings_set_cc_utils(settings);
}
bool client_cc_utils_enabled() { return client_settings_get_cc_utils().enabled; }
void client_set_party_invites(bool value) {
    auto settings = client_settings_get_cc_utils(); settings.partyInvites = value;
    client_settings_set_cc_utils(settings);
}
bool client_party_invites_enabled() { return client_settings_get_cc_utils().partyInvites; }
void client_set_player_ping(bool value) {
    auto settings = client_settings_get_cc_utils(); settings.playerPing = value;
    client_settings_set_cc_utils(settings);
}
bool client_player_ping_enabled() { return client_settings_get_cc_utils().playerPing; }

void client_set_fps_display(bool value) {
    auto settings = client_settings_get_fps_display(); settings.enabled = value;
    client_settings_set_fps_display(settings);
}
bool client_fps_display_enabled() { return client_settings_get_fps_display().enabled; }
void client_set_fps_low(bool value) {
    auto settings = client_settings_get_fps_display(); settings.low = value;
    client_settings_set_fps_display(settings);
}
bool client_fps_low() { return client_settings_get_fps_display().low; }
void client_set_fps_interval(int value) {
    auto settings = client_settings_get_fps_display(); settings.intervalMs = value;
    client_settings_set_fps_display(settings);
}
int client_fps_interval() { return client_settings_get_fps_display().intervalMs; }

void client_set_fps_font_scale(int value) {
    auto settings = client_settings_get_fps_display(); settings.fontScale = value;
    client_settings_set_fps_display(settings);
}
int client_fps_font_scale() { return client_settings_get_fps_display().fontScale; }
void client_set_fps_anchor(int value) {
    auto settings = client_settings_get_fps_display(); settings.anchor = value;
    client_settings_set_fps_display(settings);
}
int client_fps_anchor() { return client_settings_get_fps_display().anchor; }
