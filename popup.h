#pragma once

// One popup at a time. show returns false when busy or text is empty, multiline, or oversized.
// Title/message are copied (95/255 UTF-8 bytes maximum; body wraps to two display lines).
// Open prompts survive menus/focus loss; focus loss cancels the current hold.
// The timeout (1–3600 seconds, default 10) starts on the first frame and answers No.
// It keeps counting across focus loss and menus. No input is consumed. Callbacks run outside the popup lock on the frame thread
// for Yes/No, or on the caller's thread for cancel. Keep context alive until then.
enum class PopupAnswer { Yes, No, Cancelled };
using PopupCallback = void (*)(PopupAnswer answer, void* context);
bool popup_show(const char* title, const char* message,
                PopupCallback onAnswer = nullptr, void* context = nullptr,
                int timeoutSeconds = 16);
// Cancel only the pending prompt belonging to this callback/context.
bool popup_cancel_if(PopupCallback onAnswer, void* context);
void popup_cancel();

// Client input/render integration; mouse actions: 0 press, 1 release.
void popup_on_mouse_button(int button, int action, bool focused);
void popup_render(bool focused, long long frameNs);
