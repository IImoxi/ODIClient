#include <cassert>
#include <cstring>
#include "../chat.h"

static int calls[3], sequence;
static bool hide(const ChatMessage& message) {
    assert(!std::strcmp(message.text, "invite") && message.handler == &sequence);
    assert(++sequence == 1); ++calls[0]; return true;
}
static bool observe(const ChatMessage&) { assert(++sequence == 2); ++calls[1]; return false; }
static void after(const ChatMessage&) { assert(++sequence == 4); ++calls[2]; }
int main() {
    assert(!chat_listen(nullptr));
    assert(chat_listen(hide));
    assert(chat_listen(hide)); // Duplicate registration must not deliver twice.
    assert(chat_listen(observe, after));
    ChatMessage message{"invite", &sequence};
    assert(chat_notify(message));
    assert(sequence == 2 && calls[0] == 1 && calls[1] == 1 && !calls[2]);
    ++sequence; // Native display (or suppression) finishes before after-listeners.
    chat_notify_after(message);
    assert(calls[2] == 1);
}
