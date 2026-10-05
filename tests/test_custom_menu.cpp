// Exercise animation/input with a real software GLES panel render.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cassert>
#include <climits>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include "../custom_menu.cpp"

static long long testFrameNs = 1000000000LL;
static bool testLocked = true;
extern "C" void* mcpelauncher_host_dlopen(const char* path, int flags) { return dlopen(path, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
extern "C" GameWindowHandle* game_window_get_primary_window() { return nullptr; }
extern "C" bool game_window_is_mouse_locked(GameWindowHandle*) { return testLocked; }
extern "C" void game_window_add_mouse_button_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, int, int)) {}
extern "C" void game_window_add_mouse_position_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, bool)) {}
extern "C" void game_window_add_mouse_scroll_callback(GameWindowHandle*, void*, bool (*)(void*, double, double, double, double)) {}
long long fps_limiter_frame_timestamp_ns() { return testFrameNs; }
static bool testModuleEnabled;
static int testSliderValue;
static void setTestModule(bool enabled) {
    testModuleEnabled = enabled;
    custom_menu_back_to_tiles(); // Callbacks may reenter the menu without deadlocking.
}
static bool getTestModule() { assert(!custom_menu_build_error()); return testModuleEnabled; }
static void setTestSlider(int value) { testSliderValue = value; assert(!custom_menu_build_error()); }
static int getTestSlider() { return testSliderValue; }
static bool averagingEnabled;
static int targetHz = 60;
static int targetHzChanges;
static void setAveraging(bool value) { averagingEnabled = value; assert(!custom_menu_build_error()); }
static bool getAveraging() { assert(!custom_menu_build_error()); return averagingEnabled; }
static void setTargetHz(int value) { targetHz = value; ++targetHzChanges; }
static int getTargetHz() { return targetHz; }
void declare_menu_pages() {
    newTile("Test").opens(newPage("Test").textBox("Text"));
    MenuPage extra = newPage("More controls");
    for (int i = 0; i < 5; ++i) extra.text("First view");
    extra.slider("Wide range", INT_MIN, INT_MAX, setTestSlider, getTestSlider)
        .button("Back", custom_menu_back_to_tiles)
        .textBox("More text", "Initial value");
    for (int i = 0; i < 11; ++i) newTile("Extra module").opens(extra).onToggle(setTestModule, getTestModule);
    MenuPage dependent = newPage("Conditional controls");
    dependent.text("Settings")
        .toggle("FPS averaging", setAveraging, getAveraging)
        .slider("Target Hz", 30, 500, setTargetHz, getTargetHz).whenEnabled()
        .textBox("Conditional text", "saved value").whenEnabled()
        .button("Back", custom_menu_back_to_tiles);
    newTile("Conditional").opens(dependent);
}
static char renderedLabel[65];
static char renderedHeader[65];
static int headerX, headerTop, headerHeight;
static float fontOpacity, headerOpacity, suffixOpacity;
static char renderedSuffix[65];
static float rasterScale = 1.0f;
static int descriptionX, descriptionRasterHeight;
static float descriptionHeight;
bool custom_font_draw(const char* text, int x, int top, int height, int, int) {
    if (std::strncmp(text, "ODIClient", 9) == 0) {
        std::strncpy(renderedHeader, text, sizeof(renderedHeader) - 1);
        headerX = x; headerTop = top; headerHeight = height; headerOpacity = fontOpacity;
    }
    std::strncpy(renderedLabel, text, sizeof(renderedLabel) - 1);
    renderedLabel[64] = 0;
    return true;
}
bool custom_font_draw_left(const char* text, int x, int top, int height, int width, int screenHeight) {
    if (std::strcmp(text, "Description") == 0) {
        descriptionX = x;
        descriptionHeight = height;
        descriptionRasterHeight = static_cast<int>(height / rasterScale + 0.5f);
    }
    if (std::strncmp(text, "ODIClient", 9) == 0)
        return custom_font_draw(text, x, top, height, width, screenHeight);
    if (std::strncmp(text, " - ", 3) == 0) {
        std::strncpy(renderedSuffix, text, sizeof(renderedSuffix) - 1);
        suffixOpacity = fontOpacity;
    }
    return true;
}
bool custom_font_draw_left_scaled(const char* text, float x, float top, int height,
                                  float scale, int width, int screenHeight) {
    bool drawn = custom_font_draw_left(text, x, top, height, width, screenHeight);
    if (std::strcmp(text, "Description") == 0) descriptionHeight = height * scale;
    return drawn;
}
int custom_font_text_width(const char* text, int height) {
    int count = 0;
    while (text[count] && count < 64) ++count;
    return count * height / 2;
}
void custom_font_set_opacity(float opacity) { fontOpacity = opacity; }
void custom_font_set_raster_scale(float scale) { rasterScale = scale; }
void custom_font_set_clip(bool) {}
static float iconRed, iconGreen, iconBlue;
bool custom_font_draw_icon(const char*, int, int, int, float red, float green, float blue, int, int) {
    iconRed = red; iconGreen = green; iconBlue = blue;
    return true;
}

static void click(double x, double y, int button = leftMouseButton) {
    assert(onMouseButton(nullptr, x, y, button, mousePress));
    assert(onMouseButton(nullptr, x, y, button, 1));
}


static void renderAt(long long time) {
    testFrameNs = time;
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    custom_menu_render();
}

int main(int argc, char** argv) {
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    assert(eglInitialize(display, nullptr, nullptr) && eglBindAPI(EGL_OPENGL_ES_API));
    EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, configAttributes, &config, 1, &count) && count == 1);
    EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    EGLint surfaceAttributes[] = {EGL_WIDTH, 800, EGL_HEIGHT, 600, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    assert(eglMakeCurrent(display, surface, surface, context));

    // Independent hover/enable animation, including reversal and complete return.
    ButtonMotion motion;
    updateButtonMotion(motion, false, false, 1000000000LL);
    updateButtonMotion(motion, true, true, 1000000000LL);
    updateButtonMotion(motion, true, true, 1000000000LL + menu_style::buttonTransitionNs / 2);
    assert(motion.hover > 0 && motion.hover < 1 && motion.enabled == motion.hover);
    float midway = motion.hover;
    updateButtonMotion(motion, false, false, 1000000000LL + menu_style::buttonTransitionNs / 2);
    assert(motion.hover == midway);
    updateButtonMotion(motion, false, false, 1000000000LL + menu_style::buttonTransitionNs * 3 / 2);
    assert(motion.hover == 0 && motion.enabled == 0);

    // Focused theme checks also run independently of the existing grid layout checks.
    updateLayout(800, 600);
    menuOpacity = 1.0f;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    MenuFrame buttonFrame;
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0);
    unsigned char buttonPixel[4];
    glReadPixels(150, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int baseRed = static_cast<int>(255 * menu_style::mainButton.red * menu_style::buttonOpacity);
    assert(buttonPixel[0] >= baseRed - 1 && buttonPixel[0] <= baseRed + 1);
    glReadPixels(100, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int outlineRed = static_cast<int>(255 * (menu_style::mainButtonOutline.red * menu_style::buttonOutlineOpacity
        + menu_style::mainButton.red * menu_style::buttonOpacity * (1 - menu_style::buttonOutlineOpacity)));
    assert(buttonPixel[0] >= outlineRed - 1 && buttonPixel[0] <= outlineRed + 1);
    glClear(GL_COLOR_BUFFER_BIT);
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0, true);
    glReadPixels(150, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int settingsRed = static_cast<int>(255 * menu_style::settingsButton.red * menu_style::buttonOpacity);
    assert(buttonPixel[0] >= settingsRed - 1 && buttonPixel[0] <= settingsRed + 1);
    glClear(GL_COLOR_BUFFER_BIT);
    buttonMotions[0][0] = {};
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0, false, true);
    glReadPixels(150, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int enabledRed = static_cast<int>(255 * menu_style::mainButtonEnabled.red * menu_style::enabledButtonOpacity);
    assert(buttonPixel[0] >= enabledRed - 1 && buttonPixel[0] <= enabledRed + 1);
    Tile iconTile{"Icon", "dummy.png", nullptr, -1, nullptr, nullptr};
    drawTile(buttonFrame, 0, iconTile, 100, 100, 100, 100, 800, 600);
    assert(iconRed == 1 && iconGreen == 1 && iconBlue == 1);
    glClear(GL_COLOR_BUFFER_BIT);
    buttonFrame.hovered = 0;
    buttonMotions[0][0] = {};
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0);
    testFrameNs += menu_style::buttonTransitionNs;
    glClear(GL_COLOR_BUFFER_BIT);
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0);
    glReadPixels(150, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int hoverRed = static_cast<int>(255 * menu_style::mainButtonHover.red * menu_style::hoverButtonOpacity);
    assert(buttonPixel[0] >= hoverRed - 1 && buttonPixel[0] <= hoverRed + 1);
    glClear(GL_COLOR_BUFFER_BIT);
    buttonMotions[0][0] = {};
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0, false, true);
    testFrameNs += menu_style::buttonTransitionNs;
    glClear(GL_COLOR_BUFFER_BIT);
    drawButton(buttonFrame, 0, 100, 100, 100, 100, 600, 0, false, true);
    glReadPixels(150, 450, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buttonPixel);
    int enabledHoverRed = static_cast<int>(255 * menu_style::mainButtonEnabledHover.red
        * menu_style::enabledHoverButtonOpacity);
    assert(buttonPixel[0] >= enabledHoverRed - 1 && buttonPixel[0] <= enabledHoverRed + 1);
    assert(custom_menu_on_keyboard(76, 0));
    assert(custom_menu_on_keyboard(76, 2));
    renderAt(1000000000LL);
    assert(!custom_menu_captures_input());
    assert(!custom_menu_on_keyboard(87, 0));
    assert(!onMouseButton(nullptr, 400, 300, 1, 0));
    renderAt(1249999999LL);
    assert(!custom_menu_captures_input());
    assert(!custom_menu_on_keyboard(87, 0));
    renderAt(1250000000LL);
    assert(animationProgressPermille == 969); // Main menu fade retains its 500 ms timing.
    assert(custom_menu_captures_input());
    assert(custom_menu_on_keyboard(68, 0));
    assert(custom_menu_on_keyboard(68, 2));
    assert(onMouseButton(nullptr, 400, 300, 1, 0));
    renderAt(1499999999LL);
    assert(custom_menu_captures_input());
    renderAt(1610000000LL);
    assert(custom_menu_captures_input());
    int fixedHeaderX = headerX, fixedHeaderTop = headerTop, fixedHeaderHeight = headerHeight;
    assert(std::strcmp(renderedHeader, "ODIClient") == 0 && headerOpacity == 1.0f);
    activePage = 1;
    settingsTransition = {true, 1610000000LL, 0};
    renderAt(1610000000LL);
    assert(std::strcmp(renderedHeader, "ODIClient") == 0);
    assert(headerX == fixedHeaderX && headerTop == fixedHeaderTop && headerHeight == fixedHeaderHeight);
    assert(suffixOpacity == 0.0f);
    renderAt(1610000000LL + menu_style::titleMotionDurationNs / 2);
    assert(std::strcmp(renderedHeader, "ODIClient") == 0 && headerX < fixedHeaderX);
    assert(suffixOpacity == 0.5f && headerOpacity == 1.0f);
    int halfwayHeaderX = headerX;
    renderAt(1610000000LL + menu_style::titleMotionDurationNs);
    assert(std::strcmp(renderedHeader, "ODIClient") == 0);
    assert(std::strcmp(renderedSuffix, " - More controls") == 0 && suffixOpacity == 1.0f);
    assert(headerX < halfwayHeaderX && headerTop == fixedHeaderTop && headerHeight == fixedHeaderHeight);
    assert(headerX + custom_font_text_width("ODIClient - More controls", headerHeight) / 2 == panelX + panelWidth / 2);
    assert(headerOpacity == 1.0f);
    int settingsHeaderX = headerX;
    backToTiles();
    renderAt(testFrameNs);
    assert(std::strcmp(renderedHeader, "ODIClient") == 0 && headerX == settingsHeaderX);
    renderAt(testFrameNs + menu_style::titleMotionDurationNs / 2);
    assert(headerX > settingsHeaderX && headerX < fixedHeaderX);
    renderAt(titleMotion.startNs + menu_style::titleMotionDurationNs);
    assert(headerX == fixedHeaderX && headerOpacity == 1.0f);
    // Reversing an unfinished title move keeps its current position.
    activePage = 1;
    renderAt(testFrameNs);
    renderAt(testFrameNs + menu_style::titleMotionDurationNs / 2);
    int reversingX = headerX;
    backToTiles();
    renderAt(testFrameNs);
    assert(headerX == reversingX);
    renderAt(testFrameNs + menu_style::titleMotionDurationNs);
    assert(headerX == fixedHeaderX);
    settingsTransition = {};
    titleMotion = {};
    renderAt(1610000000LL);
    if (argc > 1 && std::strcmp(argv[1], "--theme") == 0) {
        std::puts("PASS: separate main/settings colors, enabled/hover alpha, outline alpha, white icons and title slide/suffix/reversal");
        return 0;
    }
    assert(custom_menu_on_keyboard(65, 0));
    assert(!custom_menu_on_keyboard(87, 2)); // Previously held movement must release.
    unsigned char pixel[4];
    glReadPixels(panelX, panelY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0); // Rounded outer corner.
    glReadPixels(panelX + panelWidth / 2, panelY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == static_cast<int>(255 * menu_style::panelOutline.red)
        && pixel[1] == static_cast<int>(255 * menu_style::panelOutline.green)
        && pixel[2] == static_cast<int>(255 * menu_style::panelOutline.blue)); // Border.
    bool partialCoverage = false;
    for (int dx = 1; dx < 12; ++dx) for (int dy = 1; dy < 12; ++dy) {
        glReadPixels(panelX + dx, panelY + dy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (pixel[0] > 30 && pixel[0] < 255 * menu_style::panelOutline.red) partialCoverage = true;
    }
    assert(partialCoverage); // Partial pixel coverage smooths the curved border.
    glReadPixels(panelX + panelWidth / 2, panelY + 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] >= 10 && pixel[0] <= 12 && pixel[1] == pixel[0] && pixel[2] == pixel[0]); // Tinted black game frame.
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    testFrameNs += 34000000LL;
    custom_menu_render();
    glReadPixels(panelX + panelWidth / 2, panelY + 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] >= 87 && pixel[0] <= 89 && pixel[1] == pixel[0] && pixel[2] == pixel[0]);
    // 70% dark tint leaves 30% of the white game frame visible.
    glReadPixels(panelX + panelWidth / 2, panelY + panelHeight - 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const float titleChannels[] = {menu_style::background.red, menu_style::background.green,
                                   menu_style::background.blue};
    for (int channel = 0; channel < 3; ++channel) {
        int expected = static_cast<int>(255 * titleChannels[channel] * menu_style::titleTint
            + 88 * (1 - menu_style::titleTint));
        assert(pixel[channel] >= expected - 1 && pixel[channel] <= expected + 1);
    } // Title tint overlays the blurred panel without itself being blurred.
    renderAt(testFrameNs);
    assert(glGetError() == GL_NO_ERROR);
    assert(!custom_menu_build_error());
    assert(onMouseScroll(nullptr, 400, 100, 0, -1));
    assert(tileOffset == 0); // Header/outside-panel scrolling leaves the grid alone.
    assert(onMouseScroll(nullptr, 400, 300, 0, -1));
    assert(tileOffset == 3);
    for (int step = 0; step < 10; ++step) onMouseScroll(nullptr, 400, 300, 0, -1);
    assert(tileOffset == 6 && activePage == -1);
    renderAt(1550000000LL);
    renderAt(1730000000LL);
    int x, top, width, height;
    tileRect(9, lastScreenHeight, x, top, width, height);
    bool wasEnabled = testModuleEnabled;
    click(x + width / 2, top + height / 2);
    assert(testModuleEnabled != wasEnabled && activePage == -1);
    click(x + width / 2, top + height / 2, rightMouseButton);
    assert(activePage == 1 && controlView == 0);
    assert(settingsTransition.active && settingsTransition.progress == 0);
    renderAt(1550000000LL);
    click(500, 450);
    assert(controlView == 0); // Settings no longer use footer pagination.
    for (int step = 0; step < 10; ++step)
        assert(onMouseScroll(nullptr, 400, 300, 0, -1));
    assert(controlView == 3);
    renderAt(1730000000LL);
    renderAt(1910000000LL);
    sliderTrackRect(5, lastScreenHeight, x, top, width, height);
    assert(onMouseButton(nullptr, x, top + height / 2, leftMouseButton, mousePress));
    assert(testSliderValue == INT_MIN && draggingSlider == 5);
    onMousePosition(nullptr, x + width + 100, top, false);
    assert(testSliderValue == INT_MAX);
    assert(onMouseButton(nullptr, x, top, leftMouseButton, 1) && draggingSlider == -1);
    pageItemRect(6, lastScreenHeight, x, top, width, height);
    click(x + width / 2, top + height / 2);
    assert(activePage == -1 && controlView == 0);
    renderAt(settingsTransition.startNs + menu_style::settingsTransitionNs);
    for (int step = 0; step < 10; ++step) onMouseScroll(nullptr, 400, 300, 0, 1);
    assert(tileOffset == 0);
    renderAt(2130000000LL);
    renderAt(2310000000LL);
    tileRect(0, lastScreenHeight, x, top, width, height);
    click(x + width / 2, top + height / 2, rightMouseButton);
    assert(activePage == 0);
    renderAt(1550000000LL);
    textFieldRect(0, lastScreenHeight, x, top, width, height);
    click(x + width / 2, top + height / 2);
    assert(focusedTextBox == 0);
    assert(custom_menu_on_keyboard(76, 0));
    assert(pages[0].items[0].value[0] == 'l' && custom_menu_captures_input());
    assert(custom_menu_on_keyboard(76, 2));
    assert(custom_menu_on_keyboard(16, 0));
    assert(custom_menu_on_keyboard(90, 0));
    assert(custom_menu_on_keyboard(90, 2));
    assert(custom_menu_on_keyboard(49, 0));
    assert(custom_menu_on_keyboard(49, 2));
    assert(custom_menu_on_keyboard(191, 0));
    assert(custom_menu_on_keyboard(191, 2));
    assert(custom_menu_on_keyboard(16, 2));
    assert(custom_menu_on_keyboard(188, 0));
    assert(custom_menu_on_keyboard(188, 2));
    assert(std::strcmp(pages[0].items[0].value, "lZ!?,") == 0);
    assert(custom_menu_on_keyboard(37, 0)); // Arrow keys must not insert punctuation.
    assert(custom_menu_on_keyboard(37, 2));
    pages[0].items[0].multiline = true;
    assert(custom_menu_on_keyboard(272, 0));
    assert(custom_menu_on_keyboard(13, 0));
    assert(custom_menu_on_keyboard(13, 2));
    assert(custom_menu_on_keyboard(272, 2));
    assert(custom_menu_on_keyboard(66, 0));
    assert(custom_menu_on_keyboard(66, 1));
    assert(custom_menu_on_keyboard(66, 2));
    assert(std::strcmp(pages[0].items[0].value, "lZ!?,\nbb") == 0 && focusedTextBox == 0);
    assert(layoutControls(pages[0], 0, 0).slots == 2);
    renderAt(1800000000LL);
    assert(drawnControls.multiline[0]);
    int multilineHeight;
    textFieldRect(0, lastScreenHeight, x, top, width, multilineHeight);
    assert(multilineHeight == panelHeight * 15 / 100 - panelHeight * 3 / 100);
    // Inspect the actual caret pixels using the same frame drawing path.
    MenuFrame caretFrame;
    caretFrame.page = pages[0]; caretFrame.pageIndex = 0; caretFrame.focused = 0;
    caretFrame.controls = drawnControls; caretFrame.caretVisible = true;
    menuOpacity = 1.0f;
    drawPageControls(caretFrame, lastScreenWidth, lastScreenHeight);
    int fontHeight = panelHeight * 3 / 100;
    int caretX = x + width * 3 / 100 + custom_font_text_width("bb", fontHeight) + 1;
    int lineHeight = panelHeight * menu_style::textLineHeightPercent / 100;
    int caretTop = top + (multilineHeight - fontHeight - lineHeight) / 2 + lineHeight;
    glReadPixels(caretX, lastScreenHeight - caretTop - fontHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == static_cast<int>(255 * menu_style::caret.red)
        && pixel[1] == static_cast<int>(255 * menu_style::caret.green)
        && pixel[2] == static_cast<int>(255 * menu_style::caret.blue));
    caretFrame.caretVisible = false;
    glClear(GL_COLOR_BUFFER_BIT);
    drawPageControls(caretFrame, lastScreenWidth, lastScreenHeight);
    glReadPixels(caretX, lastScreenHeight - caretTop - fontHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] < 180);
    pages[0].items[0].multiline = false;
    assert(custom_menu_on_keyboard(13, 0)); // Enter clears focus without indexing control -1.
    assert(focusedTextBox == -1 && activePage == 0);
    assert(custom_menu_on_keyboard(13, 2));
    assert(custom_menu_on_keyboard(27, 0));
    assert(activePage == -1 && custom_menu_captures_input());
    assert(custom_menu_on_keyboard(27, 2));
    assert(onMouseButton(nullptr, 400, 300, 1, 0));

    assert(custom_menu_on_keyboard(76, 0));
    assert(!custom_menu_captures_input() && custom_menu_is_visible());
    assert(!custom_menu_on_keyboard(87, 0));
    assert(!onMouseButton(nullptr, 400, 300, 1, 0));
    assert(onMouseButton(nullptr, 400, 300, 1, 1)); // Swallowed press still owns its release.
    assert(!onMouseButton(nullptr, 400, 300, 1, 1));
    assert(custom_menu_on_keyboard(65, 2)); // Captured release is consumed even after closing.
    assert(custom_menu_on_keyboard(76, 2));
    renderAt(1710000000LL);
    renderAt(1960000000LL);
    assert(animationProgressPermille == 31 && !custom_menu_captures_input());
    assert(custom_menu_on_keyboard(76, 0)); // Reverse a closing animation.
    assert(!custom_menu_captures_input());
    renderAt(1960000000LL);
    renderAt(2209999999LL);
    assert(!custom_menu_captures_input());
    renderAt(2210000000LL);
    assert(custom_menu_captures_input());
    renderAt(2460000000LL);
    assert(custom_menu_captures_input());
    assert(custom_menu_on_keyboard(76, 2));
    assert(custom_menu_on_keyboard(27, 0));
    assert(!custom_menu_captures_input());
    renderAt(2460000000LL);
    renderAt(2960000000LL);
    assert(!custom_menu_is_visible());
    assert(custom_menu_on_keyboard(27, 2)); // Escape release must not open Minecraft's menu.
    assert(!custom_menu_on_keyboard(27, 0));
    testLocked = false;
    assert(!custom_menu_on_keyboard(76, 0));
    testLocked = true;
    assert(custom_menu_on_keyboard(76, 0) && custom_menu_on_keyboard(76, 2));
    renderAt(3000000000LL);
    renderAt(3500000000LL);
    activePage = 2;
    controlView = 0;
    renderAt(3500000000LL);
    assert(drawnControls.count == 3 && drawnControls.viewCount == 1);
    assert(drawnControls.rows[1] == 1 && drawnControls.rows[2] == -1 && drawnControls.rows[3] == -1);
    assert(drawnControls.rows[4] == 2); // The following control moves up when collapsed.
    pageItemRect(1, lastScreenHeight, x, top, width, height);
    int gapX = x + 5, gapY = lastScreenHeight - (top + height + 2);
    glReadPixels(gapX, gapY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] < 30);
    click(x + width / 2, top + height / 2);
    assert(averagingEnabled);
    renderAt(3500000000LL);
    assert(drawnControls.count == 5 && drawnControls.rows[2] == 2 && drawnControls.rows[3] == 3);
    assert(drawnControls.rows[4] == 4 && drawnControls.viewCount == 1);
    glReadPixels(gapX, gapY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] > 30); // Expanded children share the parent's bubble, including the gap.
    sliderTrackRect(2, lastScreenHeight, x, top, width, height);
    click(x + width / 2, top + height / 2);
    assert(averagingEnabled && targetHzChanges == 1 && targetHz == 265); // Child clicks do not toggle the parent.
    textFieldRect(3, lastScreenHeight, x, top, width, height);
    click(x + width / 2, top + height / 2);
    assert(focusedTextBox == 3);
    averagingEnabled = false;
    renderAt(3500000000LL);
    assert(focusedTextBox == -1 && drawnControls.rows[3] == -1);
    click(x + width / 2, top + height / 2); // Now-hidden field cannot regain focus.
    assert(focusedTextBox == -1 && targetHzChanges == 1 && activePage == 2);
    assert(std::strcmp(pages[2].items[3].value, "saved value") == 0);
    averagingEnabled = true;
    renderAt(3500000000LL);
    sliderTrackRect(2, lastScreenHeight, x, top, width, height);
    assert(onMouseButton(nullptr, x, top + height / 2, leftMouseButton, mousePress));
    assert(draggingSlider == 2);
    averagingEnabled = false;
    renderAt(3500000000LL);
    assert(draggingSlider == -1);
    assert(onMouseButton(nullptr, x, top, leftMouseButton, 1));

    // A mode choice stays inside its master and selects the clicked option.
    Page& modePage = pages[2];
    modePage = {};
    modePage.itemCount = 4;
    modePage.items[0].type = pageToggle;
    modePage.items[0].parentToggle = -1;
    modePage.items[0].label = "Enable";
    modePage.items[0].getToggle = getAveraging;
    for (int i = 1; i < 4; ++i) modePage.items[i].parentToggle = 0;
    modePage.items[1].type = pageChoice;
    modePage.items[1].label = "Mode";
    modePage.items[1].choices[0] = "Trail";
    modePage.items[1].choices[1] = "FPS average";
    modePage.items[1].getToggle = getTestModule;
    modePage.items[1].onToggle = [](bool value) { testModuleEnabled = value; };
    modePage.items[2].isVisible = []() { return !testModuleEnabled; };
    modePage.items[3].isVisible = getTestModule;
    modePage.items[2].label = "Strength";
    modePage.items[3].label = "Target Hz";
    averagingEnabled = true;
    testModuleEnabled = false;
    renderAt(3500000000LL);
    assert(drawnControls.count == 3 && drawnControls.rows[2] >= 0 && drawnControls.rows[3] == -1);
    pageItemRect(1, lastScreenHeight, x, top, width, height);
    click(x + width * 80 / 100, top + height / 2);
    renderAt(3500000000LL);
    assert(testModuleEnabled && drawnControls.rows[2] == -1 && drawnControls.rows[3] >= 0);
    click(x + width * 80 / 100, top + height / 2);
    assert(testModuleEnabled); // Clicking the selected mode does not toggle it off.
    click(x + width * 45 / 100, top + height / 2);
    renderAt(3500000000LL);
    assert(!testModuleEnabled && drawnControls.rows[2] >= 0 && drawnControls.rows[3] == -1);
    averagingEnabled = false;
    renderAt(3500000000LL);
    assert(drawnControls.count == 1 && drawnControls.rows[1] == -1);

    Page oversized{};
    oversized.itemCount = pageItemCapacity;
    oversized.items[0].parentToggle = -1;
    oversized.items[0].type = pageToggle;
    oversized.items[0].getToggle = getAveraging;
    for (int child = 1; child < pageItemCapacity; ++child) oversized.items[child].parentToggle = 0;
    averagingEnabled = true;
    for (int offset = 0; offset <= 11; ++offset) {
        ControlLayout continued = layoutControls(oversized, 2, offset);
        assert(continued.viewCount == 12 && continued.count == pageItemCapacity);
        assert(continued.view == offset && continued.indices[0] == 0 && continued.rows[15] == 15);
    }
    averagingEnabled = false;
    ControlLayout collapsed = layoutControls(oversized, 2, 3);
    assert(collapsed.view == 0 && collapsed.viewCount == 1 && collapsed.count == 1);
    custom_menu_back_to_tiles();

    drawLabel("A label that does not fit", 0, 0, 40, 20, 10, 800, 600);
    assert(std::strlen(renderedLabel) <= 8 && std::strstr(renderedLabel, "..."));
    // Description glyphs keep their settled atlas throughout both zoom directions.
    MenuFrame descriptionFrame{};
    descriptionFrame.page.itemCount = 1;
    descriptionFrame.page.items[0].type = pageText;
    descriptionFrame.page.items[0].label = "Description";
    descriptionFrame.controls.count = 1;
    descriptionFrame.controls.indices[0] = 0;
    int savedDescriptionHeight = panelHeight;
    int savedDescriptionWidth = panelWidth;
    for (int zoomHeight = 150; zoomHeight <= 200; ++zoomHeight) {
        panelHeight = zoomHeight;
        panelWidth = zoomHeight * 4 / 3;
        drawPageControls(descriptionFrame, 800, 600, 200);
        pageItemRect(0, 600, x, top, width, height);
        assert(descriptionX == x + width * 3 / 100);
        float expectedDescriptionHeight = 6 * zoomHeight / 200.0f;
        assert(descriptionHeight > expectedDescriptionHeight - 0.0001f
            && descriptionHeight < expectedDescriptionHeight + 0.0001f);
        assert(descriptionRasterHeight == 6);
    }
    panelHeight = savedDescriptionHeight;
    panelWidth = savedDescriptionWidth;
    custom_font_set_raster_scale(1.0f);
    // Binding capture consumes the chosen key and release, rejects L, and cancels on Escape.
    MenuPage bindingPage = newPage("Binding");
    bindingPage.keyBind("Zoom key", setTestSlider, getTestSlider);
    __atomic_store_n(&open, true, __ATOMIC_RELAXED);
    __atomic_store_n(&openingCaptureReady, true, __ATOMIC_RELAXED);
    activePage = 3; controlView = 0; bindingItem = 0;
    drawnControls.pageIndex = 3; drawnControls.view = 0; drawnControls.rows[0] = 0;
    assert(custom_menu_on_keyboard(76, press) && bindingItem == 0);
    assert(custom_menu_on_keyboard(76, release));
    assert(custom_menu_on_keyboard(90, press) && testSliderValue == 90 && bindingItem == -1);
    assert(custom_menu_on_keyboard(90, release));
    bindingItem = 0;
    assert(custom_menu_on_keyboard(keyEscape, press) && bindingItem == -1 && activePage == 3);
    assert(custom_menu_on_keyboard(keyEscape, release));
    bindingItem = 0; backToTiles(); assert(bindingItem == -1);
    // Reuse this page slot so the existing capacity assertions stay unchanged.
    --pageCount;
    // Registration overflow and invalid handles must leave existing controls intact.
    MenuPage limit = newPage("Limit");
    limit.slider("Invalid", 10, 10, nullptr, nullptr);
    assert(custom_menu_build_error() && pages[3].itemCount == 0);
    for (int i = 0; i < pageItemCapacity; ++i) limit.text("Valid");
    limit.text("Overflow").whenEnabled();
    assert(pages[3].items[pageItemCapacity - 1].parentToggle == -1);
    assert(pages[3].itemCount == pageItemCapacity);
    while (pageCount < pageCapacity) newPage("Valid");
    newPage("Overflow").button("Invalid");
    assert(pageCount == pageCapacity && pages[0].itemCount == 1);
    while (tileCount < tileCapacity) newTile("Valid");
    newTile("Overflow").icon("Invalid");
    assert(tileCount == tileCapacity && tiles[0].iconPath == nullptr);
    char multiplier[80];
    PageItem formatted{}; formatted.label = "Zoom";
    formatted.formatSlider = [](int value, char* out) {
        out[0] = static_cast<char>('0' + value / 10); out[1] = '.';
        out[2] = static_cast<char>('0' + value % 10); out[3] = 'x'; out[4] = 0;
    };
    sliderLabel(formatted, 15, multiplier); assert(std::strcmp(multiplier, "Zoom: 1.5x") == 0);
    formatted.label = "A very long slider label that must stay within the bounded output buffer safely";
    formatted.formatSlider = [](int, char* out) {
        for (int i = 0; i < 15; ++i) out[i] = 'x';
        out[15] = 0;
    };
    struct { char text[80]; char guard; } bounded{};
    bounded.guard = '!'; sliderLabel(formatted, 0, bounded.text);
    assert(std::strlen(bounded.text) == 79 && bounded.guard == '!');
    char negative[80];
    sliderLabel(pages[1].items[5], INT_MIN, negative);
    assert(std::strstr(negative, "-2147483648"));
    for (int scale = 1; scale <= 4; scale *= 2) {
        updateLayout(320 * scale, 240 * scale);
        assert(panelWidth * referenceHeight == panelHeight * referenceWidth);
        assert(panelWidth <= 320 * scale / 2 && panelHeight <= 240 * scale / 2);
        panelY = centeredPanelY;
        int footerTop = lastScreenHeight - panelY - panelHeight
            + panelHeight * menu_style::footerTopPercent / 100;
        for (int i = 0; i < controlsPerView; ++i) {
            textFieldRect(i, lastScreenHeight, x, top, width, height);
            assert(top + height < footerTop && x >= panelX && x + width <= panelX + panelWidth);
        }
        for (int i = 0; i < tilesPerView; ++i) {
            tileRect(i, lastScreenHeight, x, top, width, height);
            assert(width * 4 == height * 3);
            assert(top + height < footerTop && x >= panelX && x + width <= panelX + panelWidth);
        }
    }
    assert(settingsExitGridOpacity(0.5f) == 0); // Grid waits 110 ms.
    assert(settingsExitGridOpacity(0.75f) == 0.5f);
    assert(settingsExitGridOpacity(1) == 1);
    assert(settingsScale(0) == 0.75f);
    assert(settingsScale(0.5f) == 0.984375f);
    assert(settingsScale(1) == 1);
    settingsTransition = {true, 4000000000LL, 0};
    updateSettingsTransition(4110000000LL);
    assert(settingsTransition.active && settingsTransition.progress == 0.5f);
    updateSettingsTransition(4220000000LL);
    assert(!settingsTransition.active && settingsTransition.progress == 1);
    settingsTransition.active = true;
    activePage = 0;
    backToTiles();
    assert(settingsTransition.active && settingsTransition.progress == 0 && activePage == -1);
    assert(settingsTransition.exitPage == 0);
    assert(settingsScale(1) == 1.0f);
    assert(settingsScale(1.0f - 0.5f) == 0.984375f); // Exit: 1 - 0.25 * t^4.
    assert(settingsExitGridOpacity(0.5f) == 0); // Grid waits 110 ms.
    assert(settingsExitGridOpacity(0.75f) == 0.5f);
    assert(settingsExitGridOpacity(1) == 1);
    assert(settingsScale(0) == 0.75f);
    updateSettingsTransition(settingsTransition.startNs + menu_style::settingsTransitionNs / 2);
    assert(settingsTransition.active && settingsTransition.progress == 0.5f);
    updateSettingsTransition(settingsTransition.startNs + menu_style::settingsTransitionNs);
    assert(!settingsTransition.active); // Returning tiles finish the same contents transition.
    backToTiles();
    assert(!settingsTransition.active); // Already on tiles: no extra animation.
    scrollPage = -2;
    updateScroll(4000000000LL, -1, 0);
    updateScroll(4000000000LL, -1, 1);
    assert(scrollPosition == 0);
    updateScroll(4090000000LL, -1, 1);
    assert(scrollPosition == 0.9375f); // Halfway through ease-out quart.
    updateScroll(4090000000LL, -1, 2);
    assert(scrollPosition == 0.9375f); // Retarget without jumping.
    updateScroll(4270000000LL, -1, 2);
    assert(scrollPosition == 2);
    updateScroll(4270000000LL, 0, 0);
    assert(scrollPosition == 0); // Reset when changing panels.
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    std::puts("PASS: key binding capture/cancel/reserved keys; antialiased panel/border pixels, animation/input timing, tile/settings scrolling, settings zoom/fade transition, callback reentry, expandable conditional bubbles, hidden focus/drag cleanup, slider extremes, safe builder limits, and layout bounds");
}
