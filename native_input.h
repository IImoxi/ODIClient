#pragma once
#include "native_cursor.h"

// Launcher 1.8.4, x86_64, libstdc++ ABI. GameWindow has a vptr followed by
// 32-byte std::function callbacks. Keep this bridge alongside native_cursor.h;
// it borrows the launcher's callbacks without linking a host C++ runtime.
namespace native_input {
inline void* callback(GameWindowHandle* handle, int index) {
    auto window = static_cast<unsigned char*>(nativeWindow(handle));
    return window ? window + 8 + index * 32 : nullptr;
}
inline void* invoker(void* function) {
    return function ? reinterpret_cast<void**>(function)[3] : nullptr;
}
inline bool available(GameWindowHandle* handle) {
    return invoker(callback(handle, 9)) && invoker(callback(handle, 10));
}
inline void key(GameWindowHandle* handle, int code, int action) {
    void* function = callback(handle, 9);
    int modifiers = 0;
    reinterpret_cast<void (*)(const void*, int*, int*, int*)>(invoker(function))
        (function, &code, &action, &modifiers);
}
inline void text(GameWindowHandle* handle, const char* value, unsigned long length) {
    // Read-only view of a host std::string; never destroyed or retained here.
    struct StringView { const char* data; unsigned long size, capacity, padding; };
    StringView view{value, length, length, 0};
    void* function = callback(handle, 10);
    reinterpret_cast<void (*)(const void*, const void*)>(invoker(function))(function, &view);
}
}
