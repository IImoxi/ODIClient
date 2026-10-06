#include "chat.h"

namespace {
struct Listener { ChatListener before; ChatAfterListener after; };
// ponytail: bounded process-lifetime listeners; raise the limit if real modules need more.
Listener listeners[32]{};
unsigned int listenerCount;
ChatLiveListener liveListeners[32]{};
unsigned int liveListenerCount;
}

bool chat_listen(ChatListener listener, ChatAfterListener after) {
    if (!listener && !after) return false;
    for (unsigned int i = 0; i < listenerCount; ++i)
        if (listeners[i].before == listener && listeners[i].after == after) return true;
    if (listenerCount == sizeof(listeners) / sizeof(listeners[0])) return false;
    listeners[listenerCount++] = {listener, after};
    return true;
}
bool chat_notify(const ChatMessage& message) {
    bool hidden = false;
    for (unsigned int i = 0; i < listenerCount; ++i)
        if (listeners[i].before && listeners[i].before(message)) hidden = true;
    return hidden;
}
void chat_notify_after(const ChatMessage& message) {
    for (unsigned int i = 0; i < listenerCount; ++i)
        if (listeners[i].after) listeners[i].after(message);
}

bool chat_listen_live(ChatLiveListener listener) {
    if (!listener) return false;
    for (unsigned int i = 0; i < liveListenerCount; ++i)
        if (liveListeners[i] == listener) return true;
    if (liveListenerCount == sizeof(liveListeners) / sizeof(liveListeners[0])) return false;
    liveListeners[liveListenerCount++] = listener;
    return true;
}
void chat_notify_live(const ChatLiveContext& context) {
    for (unsigned int i = 0; i < liveListenerCount; ++i) liveListeners[i](context);
}
