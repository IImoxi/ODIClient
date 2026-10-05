#pragma once
#include "launcher_api.h"

// Launcher 1.8.4 native ABI: GameWindowHandle begins with shared_ptr<GameWindow>.
// Its first word is the native GameWindow pointer. The public GameWindow vtable
// has two destructor slots, makeCurrent, setIcon, show, close, pollEvents,
// setCursorDisabled, then getCursorDisabled. Keep this version-dependent bridge
// here because the launcher's C mod API currently has no cursor-lock setter.
inline void* nativeWindow(GameWindowHandle* handle) {
    return handle ? *reinterpret_cast<void**>(handle) : nullptr;
}
inline bool cursorDisabled(GameWindowHandle* handle) {
    void* window = nativeWindow(handle);
    if (!window) return false;
    auto vtable = *reinterpret_cast<void***>(window);
    return reinterpret_cast<bool (*)(void*)>(vtable[8])(window);
}
inline void setCursorDisabled(GameWindowHandle* handle, bool disabled) {
    void* window = nativeWindow(handle);
    if (!window) return;
    auto vtable = *reinterpret_cast<void***>(window);
    reinterpret_cast<void (*)(void*, bool)>(vtable[7])(window, disabled);
}
