#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <EGL/egl.h>
#include "../fps_display.cpp"

FpsDisplaySettings settings;
struct Draw { std::string text; int x, top, size; };
std::vector<Draw> drawn;
int surfaceWidth = 960, surfaceHeight = 720;
bool surfaceAvailable = true;
FpsDisplaySettings client_settings_get_fps_display() { return settings; }
EGLDisplay display() { return reinterpret_cast<EGLDisplay>(1); }
EGLSurface surface(EGLint) { return reinterpret_cast<EGLSurface>(1); }
EGLBoolean dimensions(EGLDisplay, EGLSurface, EGLint key, EGLint* value) {
    *value = key == EGL_WIDTH ? surfaceWidth : surfaceHeight; return surfaceAvailable ? EGL_TRUE : EGL_FALSE;
}
extern "C" void* mcpelauncher_host_dlopen(const char*, int) { return reinterpret_cast<void*>(1); }
extern "C" void* mcpelauncher_host_dlsym(void*, const char* name) {
    if (!std::strcmp(name, "eglGetCurrentDisplay")) return reinterpret_cast<void*>(display);
    if (!std::strcmp(name, "eglGetCurrentSurface")) return reinterpret_cast<void*>(surface);
    if (!std::strcmp(name, "eglQuerySurface")) return reinterpret_cast<void*>(dimensions);
    return nullptr;
}
void custom_font_set_opacity(float) {}
void custom_font_set_mojangles(bool) {}
int custom_font_text_width(const char* text, int size) { return std::strlen(text) * size / 2; }
bool custom_font_draw_left_color(const char*, int, int, int, float, float, float, int, int) { return true; }
bool custom_font_draw_left(const char* text, int x, int top, int size, int, int) {
    drawn.push_back({text, x, top, size}); return true;
}
void render(bool focused, long long time) {
    display_layout_begin_frame(); fps_display_render(focused, time);
}
int main() {
    settings = {true, true, 1000};
    long long t = 1;
    render(true, t);
    assert(drawn.size() == 1 && drawn[0].text == "FPS: --  |  1% low: --");
    assert(drawn[0].x == 8 && drawn[0].top == 8 && drawn[0].size == 16);
    for (int i = 0; i < 100; ++i) render(true, t += 10000000);
    assert(average == 100 && low == 100);
    // 199 fast frames and one 100 ms stall: low averages the two slowest frames.
    settings.intervalMs = 2000;
    render(true, t);
    for (int i = 0; i < 199; ++i) render(true, t += 10000000);
    render(true, t += 100000000);
    assert(average == 96 && low == 18);
    render(false, t);
    assert(!available && previous == 0);
    render(true, t += 10000000000LL);
    assert(!available); // Focus gaps are excluded.
    settings.low = false;
    drawn.clear(); render(true, t += 10000000);
    assert(drawn.size() == 1 && drawn[0].text == "FPS: --");
    // Every font tier and corner; the full horizontal label stays aligned to the edge.
    for (int corner = 0; corner < 4; ++corner) for (int tier = 0; tier < ui_scale::count; ++tier) {
        settings.anchor = corner; settings.fontScale = tier;
        drawn.clear(); render(true, t += 10000000);
        assert(drawn.size() == 1);
        auto& draw = drawn[0];
        int size = 16 * ui_scale::percentages[tier] / 100;
        int width = custom_font_text_width(draw.text.c_str(), size) + 1;
        assert(draw.size == size);
        assert(draw.x == (corner % 2 ? 960 - 8 - width : 8));
        assert(draw.top == (corner / 2 ? 720 - 8 - size - 1 : 8));
    }
    settings.enabled = false;
    drawn.clear(); render(true, t);
    assert(drawn.empty() && !available);
    settings.enabled = settings.low = true; settings.intervalMs = 250; settings.fontScale = 2;
    render(true, t);
    render(true, t - 1);
    assert(previous == 0 && !available);
    // Extreme rates keep average FPS valid without returning a partial 1% low.
    render(true, t);
    for (int i = 0; i < 40000; ++i) render(true, t += 6250);
    assert(available && average == 160000 && !lowAvailable);
    // Multiple displays share the corner in draw order. Failed claims take no space.
    DisplayPosition first, second, third;
    for (int corner = 0; corner < 4; ++corner) {
        display_layout_begin_frame();
        auto anchor = static_cast<DisplayAnchor>(corner);
        assert(display_layout_place(anchor, 100, 20, first));
        assert(!display_layout_place(anchor, 10000, 20, third));
        assert(display_layout_place(anchor, 200, 30, second));
        assert(first.x == (corner % 2 ? 852 : 8));
        assert(second.x == (corner % 2 ? 752 : 8));
        assert(first.top == (corner / 2 ? 692 : 8));
        assert(second.top == (corner / 2 ? 658 : 32));
        // Other corners have independent stacks.
        assert(display_layout_place(static_cast<DisplayAnchor>((corner + 1) % 4), 100, 20, third));
        assert(third.top == ((corner + 1) % 4 / 2 ? 692 : 8));
        display_layout_begin_frame();
        assert(display_layout_place(anchor, 100, 20, third) && third.top == first.top);
    }
    assert(!display_layout_place(static_cast<DisplayAnchor>(4), 10, 10, first));
    assert(!display_layout_place(displayTopLeft, 0, 10, first));
    surfaceWidth = 320; surfaceHeight = 240;
    display_layout_begin_frame();
    assert(display_layout_place(displayBottomRight, 100, 20, first) && first.x == 212 && first.top == 212);
    assert(!display_layout_place(displayBottomRight, 100, 210, second));
    surfaceAvailable = false;
    drawn.clear(); render(true, t += 10000000);
    assert(drawn.empty() && display_layout_viewport().width == 0);
}
