#pragma once

void tablist_init();
void tablist_render(bool gameplay, long long frameNs);
bool tablist_on_keyboard(int key, int action, bool gameplay);
bool tablist_on_scroll(double dy);
bool tablist_on_mouse_button(int button, int action, bool gameplay);
const char* tablist_error();
