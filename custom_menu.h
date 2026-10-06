#pragma once

struct GameWindowHandle;

class MenuPage {
public:
    MenuPage& text(const char* value);
    // Omit the divider before the last item to visually join it to the previous item.
    // Consecutive descriptions or sliders in the same parent group join automatically.
    MenuPage& groupWithPrevious();
    MenuPage& button(const char* label, void (*onClick)() = nullptr);
    // Shared boolean switch: left label, animated glass thumb with X/check icons.
    MenuPage& toggle(const char* label, void (*onChange)(bool), bool (*getValue)());
    // Optional formatter writes a value label to a 16-byte buffer. Step snaps to min + n*step.
    MenuPage& slider(const char* label, int min, int max, void (*onChange)(int), int (*getValue)(),
                     void (*formatValue)(int, char*) = nullptr, int step = 1);
    // Shared four-corner selector: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
    MenuPage& anchor(const char* label, void (*onChange)(int), int (*getValue)());
    MenuPage& keyBind(const char* label, void (*onChange)(int), int (*getValue)());
    MenuPage& choice(const char* label, const char* first, const char* second,
                     void (*onChange)(bool), bool (*getValue)());
    MenuPage& dropdown(const char* label, const char* first, const char* second,
                       void (*onChange)(bool), bool (*getValue)());
    // Group the last control under the preceding root toggle; visible only when enabled.
    // Dependent toggles do not start another group. Visible rows get dividers automatically.
    MenuPage& whenEnabled(bool (*isVisible)() = nullptr);
    MenuPage& textBox(const char* label, const char* initialValue = "",
                      void (*onChange)(const char*) = nullptr, bool multiline = false);

private:
    unsigned int index;
    explicit MenuPage(unsigned int pageIndex) : index(pageIndex) {}
    friend MenuPage newPage(const char* title);
    friend class MenuTile;
};

class MenuTile {
public:
    MenuTile& icon(const char* assetPath);
    MenuTile& opens(MenuPage page);
    MenuTile& onRightClick(void (*callback)());
    MenuTile& onToggle(void (*callback)(bool), bool (*getEnabled)());

private:
    unsigned int index;
    explicit MenuTile(unsigned int tileIndex) : index(tileIndex) {}
    friend MenuTile newTile(const char* text);
};

MenuTile newTile(const char* text);
MenuPage newPage(const char* title);
// Declare once, inside declare_menu_pages(): 36 tiles/pages, 16 controls per page.
// Overflow is reported by custom_menu_build_error(); views paginate automatically.
void declare_menu_pages();
const char* custom_menu_build_error();
void custom_menu_back_to_tiles();
// Visible during either animation; input capture starts halfway through opening (250 ms).
bool custom_menu_is_visible();
// True after the opening delay; false immediately when closing is requested.
bool custom_menu_captures_input();
bool custom_menu_on_keyboard(int key, int action);
void custom_menu_register_mouse_callback(GameWindowHandle* window);
void custom_menu_render();

// Radius used by main menu tiles at the given framebuffer dimensions.
float custom_menu_main_button_radius(int screenWidth, int screenHeight);
