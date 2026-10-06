#include <EGL/egl.h>
#include "display_layout.h"
#include "launcher_api.h"

namespace {
DisplayViewport viewport;
int used[4];
constexpr int margin = 8, gap = 4;
decltype(&eglGetCurrentDisplay) getDisplay;
decltype(&eglGetCurrentSurface) getSurface;
decltype(&eglQuerySurface) querySurface;
}

bool display_layout_begin_frame() {
    viewport = {};
    for (int& value : used) value = 0;
    if (!getDisplay || !getSurface || !querySurface) {
        void* egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
        if (!egl) return false;
        getDisplay = reinterpret_cast<decltype(getDisplay)>(mcpelauncher_host_dlsym(egl, "eglGetCurrentDisplay"));
        getSurface = reinterpret_cast<decltype(getSurface)>(mcpelauncher_host_dlsym(egl, "eglGetCurrentSurface"));
        querySurface = reinterpret_cast<decltype(querySurface)>(mcpelauncher_host_dlsym(egl, "eglQuerySurface"));
    }
    if (!getDisplay || !getSurface || !querySurface) return false;
    EGLDisplay display = getDisplay(); EGLSurface surface = getSurface(EGL_DRAW);
    EGLint width = 0, height = 0;
    if (!querySurface(display, surface, EGL_WIDTH, &width)
        || !querySurface(display, surface, EGL_HEIGHT, &height) || width <= 0 || height <= 0) return false;
    viewport = {width, height};
    return true;
}
DisplayViewport display_layout_viewport() { return viewport; }

bool display_layout_place(DisplayAnchor anchor, int width, int height, DisplayPosition& position) {
    if (anchor < displayTopLeft || anchor > displayBottomRight || width <= 0 || height <= 0
        || width > viewport.width - 2 * margin || height > viewport.height - 2 * margin - used[anchor])
        return false;
    bool right = anchor == displayTopRight || anchor == displayBottomRight;
    bool bottom = anchor == displayBottomLeft || anchor == displayBottomRight;
    position = {right ? viewport.width - margin - width : margin,
                bottom ? viewport.height - margin - used[anchor] - height : margin + used[anchor]};
    used[anchor] += height + gap;
    return true;
}
