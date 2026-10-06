#include <algorithm>
#include "display_layout.h"
#include "ui_scale.h"
#include "fps_display.h"
#include "client_settings.h"
#include "custom_font.h"

namespace {
long long previous, elapsed;
unsigned long frames;
// Bounded storage covers over 16000 FPS at the maximum two-second window.
// If exceeded, average FPS remains exact and the low reads "--" for that window.
long long samples[32768];
int average, low;
bool available, lowAvailable;
FpsDisplaySettings lastSettings;

void reset() {
    previous = elapsed = 0; frames = 0;
    available = lowAvailable = false;
}
void sample(long long timestamp, const FpsDisplaySettings& settings) {
    if (settings.intervalMs != lastSettings.intervalMs || settings.low != lastSettings.low) reset();
    lastSettings = settings;
    if (timestamp <= 0 || (previous && timestamp <= previous)) { reset(); return; }
    if (!previous) { previous = timestamp; return; }
    long long delta = timestamp - previous;
    previous = timestamp;
    elapsed += delta;
    if (settings.low && frames < 32768) samples[frames] = delta;
    ++frames;
    if (elapsed < settings.intervalMs * 1000000LL) return;
    double rate = frames * 1000000000.0 / elapsed;
    average = static_cast<int>(rate > 999999 ? 999999 : rate + 0.5);
    available = true;
    lowAvailable = settings.low && frames <= 32768;
    if (lowAvailable) {
        // Extract only the slowest ceil(1%) without sorting the whole window.
        unsigned long count = (frames + 99) / 100;
        std::make_heap(samples, samples + frames);
        long long slowTime = 0;
        for (unsigned long i = 0; i < count; ++i) {
            slowTime += samples[0];
            std::pop_heap(samples, samples + frames - i);
        }
        rate = count * 1000000000.0 / slowTime;
        low = static_cast<int>(rate > 999999 ? 999999 : rate + 0.5);
    }
    frames = 0; elapsed = 0;
}
void label(char* out, const char* prefix, bool ready, int value) {
    while (*prefix) *out++ = *prefix++;
    if (!ready) { *out++ = '-'; *out++ = '-'; }
    else {
        char digits[6]; int n = 0;
        do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (n) *out++ = digits[--n];
    }
    *out = 0;
}
}

void fps_display_render(bool focused, long long frameNs) {
    auto settings = client_settings_get_fps_display();
    if (!settings.enabled || !focused) { reset(); return; }
    sample(frameNs, settings);
    auto viewport = display_layout_viewport();
    if (viewport.width <= 0 || viewport.height <= 0) return;
    int baseSize = viewport.height / 45;
    if (baseSize < 16) baseSize = 16;
    if (baseSize > 32) baseSize = 32;
    int size = baseSize * ui_scale::percentages[settings.fontScale] / 100;
    char text[64];
    label(text, "FPS: ", available, average);
    if (settings.low) {
        unsigned int end = 0;
        while (text[end]) ++end;
        label(text + end, "  |  1% low: ", lowAvailable, low);
    }
    DisplayPosition position;
    // Include the shadow pixel in the claimed bounds.
    int width = custom_font_text_width(text, size) + 1;
    if (!display_layout_place(static_cast<DisplayAnchor>(settings.anchor), width, size + 1, position)) return;
    custom_font_set_opacity(1);
    custom_font_set_mojangles(false);
    custom_font_draw_left_color(text, position.x + 1, position.top + 1, size, 0, 0, 0,
                                viewport.width, viewport.height);
    custom_font_draw_left(text, position.x, position.top, size, viewport.width, viewport.height);
}
