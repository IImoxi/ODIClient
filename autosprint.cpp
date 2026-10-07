#include <X11/Xlib.h>
#include <X11/keysym.h>
#include "launcher_api.h"
#include "autosprint.h"
#include "native_input.h"

namespace {
decltype(&XGetInputFocus) getFocus;
decltype(&XQueryKeymap) queryKeys;
decltype(&XSendEvent) sendEvent;
decltype(&XFlush) flush;
decltype(&XKeysymToKeycode) keycode;
Display* display;
void* (*sdlFocus)();
const bool* (*sdlKeys)(int*);
void (*sdlWarp)(void*, float, float);
bool (*sdlSize)(void*, int*, int*);
void* sdlWindow;
bool sdlBackend;
decltype(&XWarpPointer) warpPointer;
decltype(&XGetWindowAttributes) windowAttributes;
Window root, gameWindow = None;
KeyCode forward, ctrl, leftShift, rightShift;
bool held = false;
bool resetOnReturn = false;
bool hasFocus = false;
const char* error = "Auto Sprint: X11 not initialized";

bool sendCtrl(bool down) {
    if (sdlBackend) {
        auto handle = game_window_get_primary_window();
        if (!native_input::invoker(native_input::callback(handle, 9))) return false;
        native_input::key(handle, 17, down ? 0 : 2); // Launcher KeyCode::LEFT_CTRL.
        return true;
    }
    static Time eventTime = 1;
    XEvent event{};
    event.xkey.type = down ? KeyPress : KeyRelease;
    event.xkey.display = display;
    event.xkey.window = __atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE);
    event.xkey.root = root;
    // Distinct times prevent EGLUT merging an up/down pair into key repeat.
    event.xkey.time = eventTime++;
    event.xkey.same_screen = True;
    event.xkey.keycode = ctrl;
    event.xkey.state = down ? 0 : ControlMask;
    bool sent = sendEvent(display, event.xkey.window, False,
                          down ? KeyPressMask : KeyReleaseMask, &event) != 0;
    flush(display);
    return sent;
}
}

const char* autosprint_error() { return error; }
bool autosprint_has_focus() { return hasFocus; }
bool autosprint_shift_down() {
    if (!hasFocus) return false;
    if (sdlBackend) {
        auto target = __atomic_load_n(&sdlWindow, __ATOMIC_ACQUIRE);
        if (!target || sdlFocus() != target) return false;
        int count = 0;
        const bool* keys = sdlKeys(&count);
        return keys && count > 229 && (keys[225] || keys[229]);
    }
    if (!queryKeys || !getFocus) return false;
    Window focused;
    int revert;
    getFocus(display, &focused, &revert);
    if (focused != __atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE)) return false;
    char keys[32];
    return queryKeys(display, keys)
        && ((keys[leftShift / 8] & (1 << (leftShift % 8)))
            || (keys[rightShift / 8] & (1 << (rightShift % 8))));
}

void autosprint_release_movement_keys() {
    if (sdlBackend) {
        const int movement[] = {87, 65, 83, 68};
        auto handle = game_window_get_primary_window();
        if (native_input::invoker(native_input::callback(handle, 9)))
            for (int key : movement) native_input::key(handle, key, 2);
        return;
    }
    Window target = __atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE);
    if (target == None) return;
    if (!keycode) return;
    const KeySym movement[] = {XK_w, XK_a, XK_s, XK_d};
    for (KeySym key : movement) {
        XEvent event{};
        event.xkey.type = KeyRelease; event.xkey.display = display;
        event.xkey.window = target; event.xkey.root = root; event.xkey.same_screen = True;
        event.xkey.keycode = keycode(display, key);
        sendEvent(display, target, False, KeyReleaseMask, &event);
    }
    flush(display);
}

bool autosprint_init() {
    void* sdl = mcpelauncher_host_dlopen("libSDL3.so.0", 2);
    if (sdl) {
        auto driver = reinterpret_cast<const char* (*)()>(mcpelauncher_host_dlsym(sdl, "SDL_GetCurrentVideoDriver"));
        sdlFocus = reinterpret_cast<decltype(sdlFocus)>(mcpelauncher_host_dlsym(sdl, "SDL_GetKeyboardFocus"));
        sdlKeys = reinterpret_cast<decltype(sdlKeys)>(mcpelauncher_host_dlsym(sdl, "SDL_GetKeyboardState"));
        sdlWarp = reinterpret_cast<decltype(sdlWarp)>(mcpelauncher_host_dlsym(sdl, "SDL_WarpMouseInWindow"));
        sdlSize = reinterpret_cast<decltype(sdlSize)>(mcpelauncher_host_dlsym(sdl, "SDL_GetWindowSize"));
        if (driver && driver() && sdlFocus && sdlKeys && sdlWarp && sdlSize) {
            sdlBackend = true; error = nullptr; return true;
        }
    }
    // A separate connection keeps our X requests independent of EGLUT's locks.
    static void* library;
    if (!library)
        library = mcpelauncher_host_dlopen("libX11.so.6", 2);
    error = "Auto Sprint: X11 library unavailable";
    if (!library)
        return false;
    getFocus = reinterpret_cast<decltype(getFocus)>(mcpelauncher_host_dlsym(library, "XGetInputFocus"));
    queryKeys = reinterpret_cast<decltype(queryKeys)>(mcpelauncher_host_dlsym(library, "XQueryKeymap"));
    sendEvent = reinterpret_cast<decltype(sendEvent)>(mcpelauncher_host_dlsym(library, "XSendEvent"));
    flush = reinterpret_cast<decltype(flush)>(mcpelauncher_host_dlsym(library, "XFlush"));
    auto openDisplay = reinterpret_cast<decltype(&XOpenDisplay)>(mcpelauncher_host_dlsym(library, "XOpenDisplay"));
    auto getRoot = reinterpret_cast<decltype(&XDefaultRootWindow)>(mcpelauncher_host_dlsym(library, "XDefaultRootWindow"));
    keycode = reinterpret_cast<decltype(keycode)>(mcpelauncher_host_dlsym(library, "XKeysymToKeycode"));
    warpPointer = reinterpret_cast<decltype(warpPointer)>(mcpelauncher_host_dlsym(library, "XWarpPointer"));
    windowAttributes = reinterpret_cast<decltype(windowAttributes)>(mcpelauncher_host_dlsym(library, "XGetWindowAttributes"));
    error = "Auto Sprint: X11 API unavailable";
    if (!getFocus || !queryKeys || !sendEvent || !flush || !openDisplay || !getRoot || !keycode)
        return false;
    if (!display)
        display = openDisplay(nullptr);
    error = "Auto Sprint: X11 display unavailable";
    if (!display)
        return false;
    root = getRoot(display);
    forward = keycode(display, XK_w);
    ctrl = keycode(display, XK_Control_L);
    leftShift = keycode(display, XK_Shift_L);
    rightShift = keycode(display, XK_Shift_R);
    error = "Auto Sprint: key mapping unavailable";
    return forward && ctrl && leftShift && rightShift;
}

void autosprint_bind_window() {
    if (sdlBackend) {
        if (!__atomic_load_n(&sdlWindow, __ATOMIC_ACQUIRE))
            __atomic_store_n(&sdlWindow, sdlFocus(), __ATOMIC_RELEASE);
        return;
    }
    if (__atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE) != None)
        return;
    // Called only from a game keyboard callback, after real keyboard focus exists.
    // Never rebind on Alt-Tab: synthetic events must always target Minecraft.
    Window focused;
    int revert;
    getFocus(display, &focused, &revert);
    if (focused != None && focused != PointerRoot)
        __atomic_store_n(&gameWindow, focused, __ATOMIC_RELEASE);
}

void autosprint_update(bool enabled, bool gameplay) {
    Window target = __atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE);
    bool want, active, physicalCtrl;
    if (sdlBackend) {
        void* targetWindow = __atomic_load_n(&sdlWindow, __ATOMIC_ACQUIRE);
        hasFocus = targetWindow && sdlFocus() == targetWindow;
        active = gameplay && hasFocus;
        int count = 0;
        const bool* keys = sdlKeys(&count);
        if (!keys || count <= 229) return;
        physicalCtrl = keys[224];
        want = enabled && active && keys[26] && !keys[225] && !keys[229];
    } else {
        if (target == None) return;
        Window focused;
        int revert;
        getFocus(display, &focused, &revert);
        char keys[32];
        if (!queryKeys(display, keys))
            return;
        auto down = [&keys](KeyCode code) { return (keys[code / 8] & (1 << (code % 8))) != 0; };
        active = gameplay && focused == target;
        hasFocus = focused == target;
        physicalCtrl = down(ctrl);
        want = enabled && active && down(forward) && !down(leftShift) && !down(rightShift);
    }
    // XSendEvent does not change the physical keyboard state. Respect real Ctrl.
    if (physicalCtrl) {
        held = false;
        resetOnReturn = false;
        return;
    }
    // A menu may have consumed the first release, so release again on returning.
    if (active && resetOnReturn) {
        if (sendCtrl(false))
            resetOnReturn = false;
        return;
    }
    if (want != held && sendCtrl(want)) {
        if (!want && !active)
            resetOnReturn = true;
        held = want;
    }
}

void autosprint_center_cursor() {
    if (sdlBackend) {
        void* targetWindow = __atomic_load_n(&sdlWindow, __ATOMIC_ACQUIRE);
        if (!targetWindow || sdlFocus() != targetWindow) return;
        int width, height;
        if (sdlSize(targetWindow, &width, &height)) sdlWarp(targetWindow, width / 2.0f, height / 2.0f);
    } else {
        Window target = __atomic_load_n(&gameWindow, __ATOMIC_ACQUIRE);
        if (!target || !warpPointer || !windowAttributes) return;
        Window focused; int revert;
        getFocus(display, &focused, &revert);
        XWindowAttributes attributes{};
        if (focused == target && windowAttributes(display, target, &attributes)) {
            warpPointer(display, None, target, 0, 0, 0, 0, attributes.width / 2, attributes.height / 2);
            flush(display);
        }
    }
}
