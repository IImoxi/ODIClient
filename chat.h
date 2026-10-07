#pragma once

struct ChatMessage {
    // Formatting codes removed. Text and handler are borrowed only for this callback.
    const char* text;
    void* handler;
};
using ChatListener = bool (*)(const ChatMessage&); // Return true to hide native display.
using ChatAfterListener = void (*)(const ChatMessage&);

// Register once at mod_init, before gameplay. Callbacks live until process exit.
// All listeners receive hidden messages too. Optional after runs after native display
// (or its suppression), so sending a response cannot reenter before forwarding.
bool chat_listen(ChatListener listener, ChatAfterListener after = nullptr);
// Native backend status: null when its verified hook is available.
const char* chat_error();

// Call only from a live native dispatcher with its borrowed handler, outside UI
// locks. Accepts command text with or without one leading slash (max 255 bytes
// including slash). Returns whether native execution was invoked, not server success.
// Fails when the backend is unsupported or gameplay is unfocused. Deferred UI
// actions must store copied text and handler identity, never the handler pointer.
bool chat_send_command(void* handler, const char* command);

// Native hook owner only: emit a successfully decoded message before/after forwarding.
bool chat_notify(const ChatMessage& message);
void chat_notify_after(const ChatMessage& message);

// Borrowed only during a native client callback. No pointers may be retained.
struct ChatLiveContext { void* client; };
// Called by the verified LocalPlayer getter during native camera/gameplay work.
// Runs even when no incoming packet arrives; nested notifications are suppressed.
using ChatLiveListener = void (*)(const ChatLiveContext&);
bool chat_listen_live(ChatLiveListener listener); // Register at mod_init.
// Send only inside a live callback, outside UI locks. Queued actions keep a
// copied numeric client identity and text; world reset/disable cancels them.
bool chat_send_command(const ChatLiveContext& context, const char* command);
bool chat_send_text(const ChatLiveContext& context, const char* text);
// Queue one copied local diagnostic line for Minecraft's native chat UI helper.
// It is processed on the next live-client callback and never constructs or
// sends a network packet. A newer pending line replaces an older one. Input
// must be valid single-line UTF-8; true means queued, not displayed.
bool chat_print_local(const char* text);
void chat_notify_live(const ChatLiveContext& context); // Native owner only.
