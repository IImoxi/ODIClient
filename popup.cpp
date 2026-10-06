#include "ui_animation.h"
#include <EGL/egl.h>
#include "popup.h"
#include "launcher_api.h"
#include "custom_menu.h"
#include "custom_font.h"
#include "menu_style.h"
#include "panel_renderer.h"

namespace {
unsigned char stateLock;
struct Lock {
    Lock() { while (__atomic_test_and_set(&stateLock, __ATOMIC_ACQUIRE)) {} }
    ~Lock() { __atomic_clear(&stateLock, __ATOMIC_RELEASE); }
};
bool prompt, down[2];
char popupTitle[96]{}, popupMessage[256]{};
PopupCallback callback;
void* callbackContext;
unsigned long pressSerial, lastPress;
long long holdStart, transitionStart, resultStart, promptStart;
long long timeoutNs = 10000000000LL;
unsigned long promptSerial, renderedSerial;
int answerTarget = -1;
float answerBlend[2]{}, answerFrom[2]{};
long long answerTransitionStart;
float visibility, transitionFrom;
bool previousTarget;
const char* result;
decltype(&eglGetCurrentDisplay) getDisplay;
decltype(&eglGetCurrentSurface) getSurface;
decltype(&eglQuerySurface) querySurface;
float (*exp2)(float);
bool validLine(const char* text, unsigned long capacity) {
    if (!text || !*text) return false;
    for (unsigned long i = 0; text[i]; ++i)
        if (i + 1 >= capacity || static_cast<unsigned char>(text[i]) < 32) return false;
    return true;
}
void copyText(char* out, const char* text, unsigned long capacity) {
    unsigned long i = 0;
    for (; i + 1 < capacity && text[i]; ++i) out[i] = text[i];
    out[i] = 0;
}
}

bool popup_show(const char* title, const char* message, PopupCallback onAnswer, void* context, int timeoutSeconds) {
    if (timeoutSeconds < 1 || timeoutSeconds > 3600) return false;
    if (!validLine(title, sizeof(popupTitle)) || !validLine(message, sizeof(popupMessage))) return false;
    Lock lock;
    if (prompt) return false;
    copyText(popupTitle, title, sizeof(popupTitle));
    copyText(popupMessage, message, sizeof(popupMessage));
    callback = onAnswer; callbackContext = context;
    prompt = true; result = nullptr; resultStart = promptStart = 0;
    timeoutNs = static_cast<long long>(timeoutSeconds) * 1000000000LL; ++promptSerial;
    down[0] = down[1] = false; holdStart = 0;
    return true;
}

bool popup_cancel_if(PopupCallback onAnswer, void* expectedContext) {
    {
        Lock lock;
        if (!prompt || callback != onAnswer || callbackContext != expectedContext) return false;
        prompt = false; result = nullptr; callback = nullptr;
        down[0] = down[1] = false; holdStart = 0;
    }
    if (onAnswer) onAnswer(PopupAnswer::Cancelled, expectedContext);
    return true;
}

void popup_cancel() {
    PopupCallback cancelled; void* context;
    {
        Lock lock;
        cancelled = prompt ? callback : nullptr; context = callbackContext;
        prompt = false; result = nullptr; callback = nullptr;
        down[0] = down[1] = false; holdStart = 0;
    }
    if (cancelled) cancelled(PopupAnswer::Cancelled, context);
}

void popup_on_mouse_button(int button, int action, bool focused) {
    if (button != 1 && button != 2) return;
    Lock lock;
    int index = button - 1;
    if (action != 0) {
        down[index] = false; holdStart = 0;
        return;
    }
    if (!prompt || !focused) return;
    down[index] = true; ++pressSerial;
}

void popup_render(bool focused, long long frameNs) {
    bool target; float progress = 0, elapsed = 0, resultTime = 0; unsigned long serial; int answer = -1; char title[96], message[256]; bool showingResult; const char* resultLabel;
    PopupCallback completed = nullptr; void* context = nullptr; PopupAnswer response = PopupAnswer::No;
    {
        Lock lock;
        if (prompt) {
            if (!promptStart) promptStart = frameNs;
            elapsed = static_cast<float>(frameNs - promptStart) / timeoutNs;
            if (frameNs - promptStart >= timeoutNs) {
                prompt = false; result = nullptr;
                down[0] = down[1] = false; holdStart = 0;
                completed = callback; context = callbackContext; callback = nullptr;
                response = PopupAnswer::No;
            }
        }
        serial = promptSerial;
        if (!focused) { down[0] = down[1] = false; holdStart = 0; }
        if (pressSerial != lastPress) { lastPress = pressSerial; holdStart = 0; }
        if (prompt && focused && down[0] != down[1]) {
            answer = down[0] ? 0 : 1;
            if (!holdStart) holdStart = frameNs;
            progress = static_cast<float>(frameNs - holdStart) / 1000000000.0f;
            if (frameNs - holdStart >= 1000000000LL) {
                prompt = false; result = answer == 0 ? "Yes" : "No";
                resultStart = frameNs; down[0] = down[1] = false;
                completed = callback; context = callbackContext; callback = nullptr;
                response = answer == 0 ? PopupAnswer::Yes : PopupAnswer::No;
            }
        } else holdStart = 0;
        target = prompt || (result && frameNs - resultStart < 1000000000LL);
        if (result) resultTime = static_cast<float>(frameNs - resultStart) / 250000000.0f;
        showingResult = result != nullptr; resultLabel = result;
        copyText(title, popupTitle, sizeof(title));
        copyText(message, popupMessage, sizeof(message));
    }
    // Callbacks may open another popup; invoke them outside the state lock.
    if (completed) completed(response, context);
    if (!getDisplay) {
        if (!target) return;
        void* egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
        void* math = mcpelauncher_host_dlopen("libm.so.6", 2);
        if (!egl || !math) return;
        getSurface = reinterpret_cast<decltype(getSurface)>(mcpelauncher_host_dlsym(egl, "eglGetCurrentSurface"));
        querySurface = reinterpret_cast<decltype(querySurface)>(mcpelauncher_host_dlsym(egl, "eglQuerySurface"));
        exp2 = reinterpret_cast<decltype(exp2)>(mcpelauncher_host_dlsym(math, "exp2f"));
        getDisplay = reinterpret_cast<decltype(getDisplay)>(mcpelauncher_host_dlsym(egl, "eglGetCurrentDisplay"));
    }
    if (!getDisplay || !getSurface || !querySurface || !exp2) return;
    if (serial != renderedSerial) {
        renderedSerial = serial; answerTarget = -1;
        answerBlend[0] = answerBlend[1] = answerFrom[0] = answerFrom[1] = 0;
    }
    if (answer != answerTarget) {
        answerTarget = answer; answerTransitionStart = frameNs;
        answerFrom[0] = answerBlend[0]; answerFrom[1] = answerBlend[1];
    }
    float answerTime = static_cast<float>(frameNs - answerTransitionStart) / 180000000.0f;
    if (answerTime > 1) answerTime = 1;
    float answerEase = ui_animation::ease_out_quart(answerTime);
    for (int i = 0; i < 2; ++i)
        answerBlend[i] = answerFrom[i] + ((answerTarget == i ? 1.0f : 0.0f) - answerFrom[i]) * answerEase;
    if (target != previousTarget) {
        previousTarget = target; transitionStart = frameNs; transitionFrom = visibility;
    }
    float t = static_cast<float>(frameNs - transitionStart) / 500000000.0f;
    float ease = ui_animation::ease_out_exponential(t, exp2);
    visibility = transitionFrom + ((target ? 1.0f : 0.0f) - transitionFrom) * ease;
    if (visibility <= 0.001f) return;
    EGLDisplay display = getDisplay(); EGLSurface surface = getSurface(EGL_DRAW);
    EGLint w = 0, h = 0;
    if (!querySurface(display, surface, EGL_WIDTH, &w) || !querySurface(display, surface, EGL_HEIGHT, &h)
        || w < 240 || h < 180) return;
    float scale = w / 960.0f;
    if (h / 720.0f < scale) scale = h / 720.0f;
    if (scale < 0.75f) scale = 0.75f;
    int width = static_cast<int>(238 * scale), height = static_cast<int>(78 * scale);
    if (width > w - 16) width = w - 16;
    int margin = static_cast<int>(10 * scale), headerFont = static_cast<int>(12 * scale);
    int font = static_cast<int>(10 * scale);
    char first[256], second[256]{};
    copyText(first, message, sizeof(first));
    if (custom_font_text_width(first, font) > width - margin * 2) {
        int split = 0, lastSpace = 0;
        for (int i = 1; message[i]; ++i) {
            // Only split at UTF-8 character boundaries.
            if ((static_cast<unsigned char>(message[i]) & 0xc0) == 0x80) continue;
            char saved = first[i]; first[i] = 0;
            int textWidth = custom_font_text_width(first, font);
            first[i] = saved;
            if (textWidth > width - margin * 2) break;
            split = i;
            if (message[i] == ' ') lastSpace = i;
        }
        if (lastSpace) split = lastSpace;
        if (split) {
            first[split] = 0;
            while (message[split] == ' ') ++split;
            copyText(second, message + split, sizeof(second));
        }
    }
    int lineStep = font + static_cast<int>(3 * scale);
    if (*second) height += lineStep;
    int bodyEnd = margin + headerFont + static_cast<int>(3 * scale) + font;
    if (*second) bodyEnd += lineStep;
    // Center the answer row between the body and the bottom edge.
    int answerTop = bodyEnd + (height - bodyEnd - font) / 2;
    int x = w - width - margin + static_cast<int>((width + margin) * (1 - visibility));
    int bottom = margin;
    int top = h - bottom - height;
    PanelPaint paint;
    paint.tint = menu_style::tablistBackground; paint.tintAmount = menu_style::tablistBackgroundTint;
    paint.opacity = visibility * menu_style::tablistBackgroundOpacity;
    paint.cornerRadius = custom_menu_main_button_radius(w, h);
    paint.blurScale = menu_style::tablistBlurScale; paint.blurTimestampNs = frameNs;
    draw_gl_panel(x, bottom, width, height, paint);
    paint.blurScale = 0; paint.tint = menu_style::tablistOutline;
    paint.opacity = visibility * menu_style::tablistOutlineOpacity;
    paint.borderWidth = menu_style::tablistOutlineThickness;
    if (paint.borderWidth > 0) draw_gl_panel(x, bottom, width, height, paint);
    custom_font_set_opacity(visibility * menu_style::tablistTextOpacity);
    // Fit caller-provided single lines without drawing outside the panel.
    auto drawLine = [&](const char* text, int y, int textFont) {
        int textWidth = custom_font_text_width(text, textFont);
        float fit = textWidth > width - margin * 2
            ? static_cast<float>(width - margin * 2) / textWidth : 1.0f;
        custom_font_draw_left_scaled(text, x + margin, y, textFont, fit, w, h);
    };
    if (showingResult) {
        float t = resultTime < 1 ? resultTime : 1;
        float ease = ui_animation::ease_out_quart(t);
        float displayHeight = font + (headerFont - font) * ease;
        float rasterScale = displayHeight / headerFont;
        float textWidth = custom_font_text_width(resultLabel, headerFont) * rasterScale;
        float initialTop = top + answerTop;
        float centerTop = top + (height - displayHeight) * 0.5f;
        float y = initialTop + (centerTop - initialTop) * ease;
        // Reuse the title-size atlas throughout the growth animation.
        custom_font_draw_left_scaled(resultLabel, x + (width - textWidth) * 0.5f,
                                      y, headerFont, rasterScale, w, h);
    } else {
        drawLine(title, top + margin, headerFont);
        int bodyTop = top + margin + headerFont + static_cast<int>(3 * scale);
        drawLine(first, bodyTop, font);
        if (*second) drawLine(second, bodyTop + lineStep, font);
    }
    if (!showingResult && target) {
        PanelPaint bar;
        bar.tint = menu_style::tablistText; bar.opacity = visibility * 0.04f;
        int inset = static_cast<int>(paint.cornerRadius + 1);
        int barWidth = width - inset * 2;
        int barHeight = static_cast<int>(2 * scale);
        if (barHeight < 1) barHeight = 1;
        draw_gl_panel(x + inset, bottom + 1, barWidth, barHeight, bar);
        bar.opacity = visibility * 0.16f;
        int filled = static_cast<int>(barWidth * elapsed);
        if (filled > 0) draw_gl_panel(x + inset, bottom + 1, filled, barHeight, bar);
    }
    if (!showingResult) {
        int y = top + answerTop;
        for (int i = 0; i < 2; ++i) {
            const char* label = i == 0 ? "Yes" : "No";
            int textWidth = custom_font_text_width(label, font);
            float idleCenter = x + width * (i == 0 ? 0.25f : 0.75f);
            float center = idleCenter + (x + width * 0.5f - idleCenter) * answerBlend[i];
            int left = static_cast<int>(center - textWidth * 0.5f);
            float opacity = visibility * menu_style::tablistTextOpacity * (1 - answerBlend[1 - i]);
            custom_font_set_opacity(opacity);
            custom_font_draw_left(label, left, y, font, w, h);
            if (answerBlend[i] > 0.001f) {
                custom_font_set_opacity(opacity * answerBlend[i]);
                custom_font_draw_ring(left + textWidth + static_cast<int>(5 * scale) + font / 2,
                                      y + font / 2 + static_cast<int>(scale + 1.0f),
                                      font * 0.45f, progress, w, h);
            }
        }
    }
    custom_font_set_opacity(1);
}
