#include <cassert>
#include <initializer_list>
#include "popup.cpp"
#include "experimental.h"
#include "client_modules.h"

bool menuVisible;
bool custom_menu_is_visible() { return menuVisible; }
float custom_menu_main_button_radius(int, int) { return 4; }
extern "C" void* mcpelauncher_host_dlopen(const char*, int) { return nullptr; }
extern "C" void* mcpelauncher_host_dlsym(void*, const char*) { return nullptr; }
bool draw_gl_panel(int, int, int, int, const PanelPaint&) { return true; }
void custom_font_set_opacity(float) {}
int custom_font_text_width(const char*, int) { return 100; }
bool custom_font_draw_left_scaled(const char*, float, float, int, float, int, int) { return true; }
bool custom_font_draw_left(const char*, int, int, int, int, int) { return true; }
bool custom_font_draw_ring(int, int, float, float, int, int) { return true; }

void trigger() {
    experimental_on_keyboard(84, 0, true, true);
    for (int key : {84, 69, 83, 84}) experimental_on_keyboard(key, 0, true, true);
    experimental_on_keyboard(13, 0, false, true);
    assert(prompt);
}
void ownedCancellation(PopupAnswer response, void* context) {
    assert(response == PopupAnswer::Cancelled); ++*static_cast<int*>(context);
}
int timeoutAnswers;
void timedOut(PopupAnswer response, void*) {
    assert(response == PopupAnswer::No); ++timeoutAnswers;
}
int answers, cancellations;
void received(PopupAnswer response, void* context) {
    assert(context == &answers);
    if (response == PopupAnswer::Cancelled) ++cancellations;
    else { ++answers; assert(response == PopupAnswer::Yes); }
    // Exercises callback reentry after releasing the popup lock.
    assert(popup_show("Follow-up", "Another question?"));
}
int main() {
    assert(!client_experimental_enabled());
    client_set_experimental(true);
    trigger();
    popup_on_mouse_button(1, 0, true);
    popup_render(true, 1000000000LL);
    popup_render(true, 1999999999LL);
    assert(prompt);
    popup_on_mouse_button(1, 1, true);
    popup_render(true, 2100000000LL);
    assert(prompt && !holdStart);
    popup_on_mouse_button(1, 0, true);
    popup_render(true, 2200000000LL);
    popup_render(true, 3200000000LL);
    assert(!prompt && result && result[0] == 'Y');
    popup_on_mouse_button(1, 1, true);
    trigger();
    popup_on_mouse_button(2, 0, true);
    popup_render(true, 4000000000LL);
    popup_render(true, 5000000000LL);
    assert(!prompt && result[0] == 'N');
    popup_on_mouse_button(2, 1, true);
    trigger();
    popup_on_mouse_button(1, 0, true);
    popup_on_mouse_button(2, 0, true);
    popup_render(true, 6000000000LL);
    popup_render(true, 8000000000LL);
    assert(prompt && !holdStart);
    popup_render(false, 9000000000LL);
    assert(prompt && !down[0] && !down[1]);
    popup_cancel();
    trigger(); menuVisible = true;
    popup_render(true, 10000000000LL);
    assert(prompt); // Also visible while a UI is open.
    popup_cancel(); menuVisible = false;
    trigger(); client_set_experimental(false);
    assert(prompt); // Experimental only controls the test trigger.
    popup_cancel();
    char title[] = "Custom title";
    assert(popup_show(title, "Custom question?", received, &answers));
    title[0] = 'X'; assert(popupTitle[0] == 'C');
    assert(!popup_show("Busy", "Must not replace the active callback"));
    popup_on_mouse_button(1, 0, true);
    popup_render(true, 11000000000LL);
    popup_render(true, 12000000000LL);
    assert(answers == 1 && prompt); // Follow-up opened by the callback.
    popup_cancel();
    assert(popup_show("Cancel", "Question?", received, &answers));
    popup_cancel(); assert(cancellations == 1 && prompt);
    popup_cancel();
    assert(!popup_show(nullptr, "Question?"));
    assert(!popup_show("Title", ""));
    assert(!popup_show("Title", "Multiline\nquestion"));
    char oversized[257];
    for (int i = 0; i < 256; ++i) oversized[i] = 'x';
    oversized[256] = 0;
    assert(!popup_show("Title", oversized));
    assert(!popup_show("Title", "Question?", nullptr, nullptr, 0));
    assert(popup_show("Timeout", "Question?", timedOut, nullptr, 2));
    popup_render(true, 20000000000LL);
    popup_render(false, 21999999999LL);
    assert(prompt && timeoutAnswers == 0);
    popup_render(false, 22000000000LL);
    assert(!prompt && !result && timeoutAnswers == 1);
    popup_render(true, 23000000000LL);
    assert(timeoutAnswers == 1); // Exactly one No callback, even without focus.
    int ownCancellations = 0;
    assert(popup_show("Owner", "Question?", ownedCancellation, &ownCancellations));
    assert(!popup_cancel_if(ownedCancellation, nullptr) && prompt);
    assert(popup_cancel_if(ownedCancellation, &ownCancellations) && !prompt && ownCancellations == 1);
    menuVisible = true;
    experimental_show_test(); // Preview works even with test trigger disabled.
    assert(prompt);
    popup_cancel();
}
