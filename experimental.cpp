#include "experimental.h"
#include "client_modules.h"
#include "custom_menu.h"
#include "popup.h"

namespace {
unsigned char stateLock;
struct Lock {
    Lock() { while (__atomic_test_and_set(&stateLock, __ATOMIC_ACQUIRE)) {} }
    ~Lock() { __atomic_clear(&stateLock, __ATOMIC_RELEASE); }
};
bool enabled, typing;
char typed[256]{};
int used;
bool invalid;
}
void client_set_experimental(bool value) {
    Lock lock; enabled = value;
    if (!value) typing = false;
}
bool client_experimental_enabled() { Lock lock; return enabled; }

void experimental_show_test() {
    popup_show("Experimental", "Would you like to continue?");
}

void experimental_on_keyboard(int key, int action, bool gameplay, bool focused) {
    bool show = false;
    {
        Lock lock;
        if (!enabled || !focused || custom_menu_is_visible()) { typing = false; return; }
        if (action == 2) return;
        // Chat can keep mouse capture: only the initial T starts tracking.
        if (!typing && gameplay && key == 84 && action == 0) {
            typing = true; used = 0; invalid = false; return;
        }
        if (!typing) return;
        if (key == 256 || key == 27) { typing = false; return; }
        if (key == 257 || key == 13) {
            show = !invalid && used == 4 && typed[0] == 't' && typed[1] == 'e'
                && typed[2] == 's' && typed[3] == 't';
            typing = false;
        } else if (key == 259 || key == 8) {
            if (used) --used;
        } else if (key >= 65 && key <= 90) {
            if (used < 255) typed[used++] = static_cast<char>(key + 32);
            else invalid = true;
        } else if (key != 16 && key != 272) invalid = true;
    }
    if (show) experimental_show_test();
}
