#include "ui_animation.h"
#include "ui_scale.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "launcher_api.h"
#include "custom_menu.h"
#include "menu_style.h"
#include "custom_font.h"
#include "fps_limiter.h"

namespace {
constexpr int keyL = 76;
constexpr int keyEscape = 27;
constexpr int keyLeftShift = 16;
constexpr int keyRightShift = 272;
constexpr int press = 0;
constexpr int release = 2;
constexpr int layoutScalePercent = 50;
constexpr int referenceWidth = 480;
constexpr int referenceHeight = 360;
constexpr int resizeThresholdPercent = 5;
// Launcher mouse buttons are GLFW button IDs shifted by one; press is enum value 0.
constexpr int rightMouseButton = 2;
constexpr int leftMouseButton = 1;
constexpr int mousePress = 0;
// ponytail: bounded registration; raise these limits when real modules need more.
constexpr int tileCapacity = 36;
constexpr int pageCapacity = 36;
constexpr int pageItemCapacity = 16;
constexpr int tilesPerView = 9;
constexpr int controlsPerView = 5;
constexpr int gridColumns = 3;
constexpr int gridGapPercent = 3;
struct Tile {
    const char* text;
    const char* iconPath;
    void (*rightClick)();
    int pageIndex;
    void (*onToggle)(bool);
    bool (*getEnabled)();
};
enum PageItemType { pageText, pageButton, pageToggle, pageSlider, pageTextBox, pageChoice, pageKeyBind, pageDropdown, pageAnchor };
struct PageItem {
    PageItemType type;
    bool groupWithPrevious;
    int parentToggle;
    const char* label;
    const char* choices[2];
    bool (*isVisible)();
    void (*onClick)();
    void (*onChange)(const char*);
    bool (*getToggle)();
    int (*getSlider)();
    void (*formatSlider)(int, char*); // Writes at most 15 characters plus terminator.
    int sliderMin;
    int sliderMax;
    int sliderStep;
    void (*onToggle)(bool);
    void (*onSlider)(int);
    bool multiline;
    bool expanded;
    char value[256];
};
struct Page {
    const char* title;
    PageItem items[pageItemCapacity];
    unsigned int itemCount;
    int lastAppended;
};
struct ControlLayout {
    int pageIndex = -1;
    int view = 0;
    int viewCount = 1;
    int count = 0;
    int slots = 0;
    int indices[pageItemCapacity]{};
    int rows[pageItemCapacity]{};
    int offsetsPercent[pageItemCapacity]{};
    int parents[pageItemCapacity]{};
    bool enabled[pageItemCapacity]{};
    bool descriptions[pageItemCapacity]{};
    bool dividers[pageItemCapacity]{};
    bool multiline[pageItemCapacity]{};
    bool expanded[pageItemCapacity]{};
    bool anchors[pageItemCapacity]{};
    int lineSlots[pageItemCapacity]{};
};
int bindingItem = -1;
ControlLayout drawnControls;
double cursorX, cursorY;
bool cursorPositionValid;

// Evaluate getters outside UiLock, then show a scrolling window of visible controls.
ControlLayout layoutControls(const Page& page, int pageIndex, int requestedView) {
    ControlLayout layout;
    layout.pageIndex = pageIndex;
    int visible[pageItemCapacity]{};
    int count = 0;
    for (unsigned int i = 0; i < page.itemCount; ++i) {
        layout.rows[i] = -1;
        layout.parents[i] = page.items[i].parentToggle;
        layout.descriptions[i] = page.items[i].type == pageText;
        layout.anchors[i] = page.items[i].type == pageAnchor;
        if (page.items[i].type == pageToggle || page.items[i].type == pageChoice || page.items[i].type == pageDropdown)
            layout.enabled[i] = page.items[i].getToggle && page.items[i].getToggle();
    }
    for (unsigned int i = 0; i < page.itemCount; ++i) {
        int parent = page.items[i].parentToggle;
        if (parent >= 0 && (!layout.enabled[parent]
            || (page.items[i].isVisible && !page.items[i].isVisible()))) continue;
        visible[count++] = i;
    }
    int totalPercent = 0;
    layout.count = count;
    for (int row = 0; row < count; ++row) {
        int index = visible[row];
        const PageItem& item = page.items[index];
        int lines = 1;
        if (item.multiline) for (const char* c = item.value; *c; ++c) if (*c == '\n') ++lines;
        layout.indices[row] = index;
        layout.rows[index] = row;
        layout.offsetsPercent[index] = totalPercent;
        if (row > 0) {
            int previous = visible[row - 1];
            bool descriptions = layout.descriptions[index] && layout.descriptions[previous]
                && layout.parents[index] == layout.parents[previous];
            bool sliders = item.type == pageSlider && page.items[previous].type == pageSlider
                && layout.parents[index] == layout.parents[previous];
            bool grouped = item.groupWithPrevious && previous == index - 1;
            layout.dividers[index] = !descriptions && !sliders && !grouped;
        }
        layout.multiline[index] = item.multiline;
        layout.lineSlots[index] = lines;
        layout.expanded[index] = item.type == pageDropdown && item.expanded;
        if (layout.expanded[index]) totalPercent += 2 * menu_style::rowHeightPercent;
        totalPercent += (layout.descriptions[index] ? menu_style::descriptionRowStridePercent
                                                  : menu_style::rowStridePercent)
            + (lines - 1) * menu_style::textLineHeightPercent
            + (layout.anchors[index] ? menu_style::rowHeightPercent : 0);
    }
    layout.slots = (totalPercent + menu_style::rowStridePercent - 1) / menu_style::rowStridePercent;
    int maximum = layout.slots > controlsPerView ? layout.slots - controlsPerView : 0;
    layout.viewCount = maximum + 1;
    layout.view = requestedView < 0 ? 0 : requestedView > maximum ? maximum : requestedView;
    return layout;
}

Tile tiles[tileCapacity];
Page pages[pageCapacity];
unsigned int tileCount;
unsigned int pageCount;
bool menuBuilt;
const char* builderError;
int tileOffset;
int controlView;
float scrollPosition, scrollFrom;
int scrollTarget, scrollPage = -2;
long long scrollStartNs;
bool clippingContent;
decltype(&glGetIntegerv) menuGetIntegerv;
decltype(&glIsEnabled) menuIsEnabled;
decltype(&glEnable) menuEnable;
decltype(&glDisable) menuDisable;
decltype(&glScissor) menuScissor;

void updateScroll(long long now, int page, int target) {
    if (scrollPage != page) {
        scrollPage = page;
        scrollPosition = scrollFrom = static_cast<float>(target);
        scrollTarget = target;
        scrollStartNs = now;
        return;
    }
    float t = now > scrollStartNs ? static_cast<float>(now - scrollStartNs) / menu_style::scrollDurationNs : 0.0f;
    if (t > 1.0f) t = 1.0f;
    float eased = ui_animation::ease_out_quart(t);
    scrollPosition = scrollFrom + (scrollTarget - scrollFrom) * eased;
    if (target != scrollTarget) {
        scrollFrom = scrollPosition;
        scrollTarget = target;
        scrollStartNs = now;
    }
}

bool open = false;
bool openingCaptureReady = false;
bool consumedKeys[512];
bool consumedMouseButtons[8];
int activePage = -1;
struct SettingsTransition {
    bool active = false;
    long long startNs = 0;
    float progress = 1.0f;
    int exitPage = -1;
    int exitView = 0;
};
SettingsTransition settingsTransition;

float settingsExitGridOpacity(float progress) {
    float delay = static_cast<float>(menu_style::settingsExitGridFadeDelayNs)
        / menu_style::settingsTransitionNs;
    float fade = (progress - delay) / (1.0f - delay);
    if (fade <= 0.0f) return 0.0f;
    return fade >= 1.0f ? 1.0f : fade;
}

float settingsScale(float progress) {
    float remaining = 1.0f - progress;
    return 1.0f - 0.25f * remaining * remaining * remaining * remaining;
}

float settingsExitScale(float progress) {
    float remaining = 1.0f - progress;
    return 0.75f + 0.25f * remaining * remaining * remaining * remaining;
}

void updateSettingsTransition(long long now) {
    if (!settingsTransition.active) return;
    long long elapsed = now - settingsTransition.startNs;
    settingsTransition.progress = elapsed <= 0 ? 0.0f
        : static_cast<float>(elapsed) / menu_style::settingsTransitionNs;
    if (settingsTransition.progress >= 1.0f) {
        settingsTransition.progress = 1.0f;
        settingsTransition.active = false;
    }
}
int focusedTextBox = -1;
int draggingSlider = -1;
bool leftShiftHeld;
bool rightShiftHeld;
bool capsLock;
long long caretStartNs;
bool layoutDirty = true;
int animationProgressPermille;
int animationFromPermille;
bool animationTargetOpen;
long long animationStartNs;
int panelX, panelY, panelWidth, panelHeight;
int centeredPanelY;
int lastScreenWidth, lastScreenHeight;
float menuOpacity = 1.0f;
bool rendererLoaded;
float (*hostExp2f)(float);
decltype(&eglGetCurrentContext) getContext;
decltype(&eglGetCurrentDisplay) getDisplay;
decltype(&eglGetCurrentSurface) getSurface;
decltype(&eglQuerySurface) querySurface;

// Only model copies and input edits hold this lock. GL work and callbacks run outside it.
bool uiLocked;
struct UiLock {
    UiLock() {
        while (__atomic_test_and_set(&uiLocked, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
    }
    ~UiLock() { __atomic_clear(&uiLocked, __ATOMIC_RELEASE); }
};

struct MenuAction {
    void (*click)() = nullptr;
    void (*toggle)(bool) = nullptr;
    void (*slider)(int) = nullptr;
    void (*text)(const char*) = nullptr;
    bool (*getToggle)() = nullptr;
    int value = 0;
    char textValue[256]{};
    void run() const {
        if (click) click();
        if (toggle) toggle(getToggle ? !getToggle() : value != 0);
        if (slider) slider(value);
        if (text) text(textValue);
    }
};

void resetPageInput() {
    if (activePage >= 0)
        for (unsigned int i = 0; i < pages[activePage].itemCount; ++i) pages[activePage].items[i].expanded = false;
    focusedTextBox = -1;
    draggingSlider = -1;
    bindingItem = -1;
}

void backToTiles() {
    bool leavingSettings = activePage >= 0 && __atomic_load_n(&open, __ATOMIC_RELAXED);
    settingsTransition = leavingSettings
        ? SettingsTransition{true, fps_limiter_frame_timestamp_ns(), 0.0f, activePage, controlView}
        : SettingsTransition{};
    bindingItem = -1;
    resetPageInput();
    activePage = -1;
    controlView = 0;
    scrollPage = -2;
    resetPageInput();
}

void buildMenu();

void tileRect(unsigned int index, int screenHeight,
              int& x, int& top, int& width, int& height) {
    int slot = static_cast<int>(index);
    int column = slot % gridColumns;
    int row = slot / gridColumns;
    int contentWidth = panelWidth * (100 - 2 * menu_style::panelPaddingPercent) / 100;
    int gap = panelWidth * gridGapPercent / 100;
    int count = static_cast<int>(tileCount);
    if (count > tilesPerView) count = tilesPerView;
    int rows = (count + gridColumns - 1) / gridColumns;
    if (rows < 1) rows = 1;
    int cardUnit = panelHeight * (rows < 3 ? 28 : 21) / 400;
    width = cardUnit * 6;
    height = cardUnit * 4;
    int rowGap = panelHeight * 3 / 100;
    int gridHeight = rows * height + (rows - 1) * rowGap;
    int contentHeight = panelHeight * 70 / 100;
    int rowCells = static_cast<int>(tileCount) - row * gridColumns;
    if (rowCells > gridColumns) rowCells = gridColumns;
    x = panelX + panelWidth * menu_style::panelPaddingPercent / 100
        + (contentWidth - rowCells * width - (rowCells - 1) * gap) / 2
        + column * (width + gap);
    top = screenHeight - (panelY + panelHeight)
        + panelHeight * menu_style::contentTopPercent / 100
        + (contentHeight - gridHeight) / 2 + row * (height + rowGap)
        - static_cast<int>(scrollPosition * (height + rowGap));
}

void pageItemRect(unsigned int index, int screenHeight,
                  int& x, int& top, int& width, int& height) {
    int offsetPercent = drawnControls.pageIndex >= 0 && drawnControls.rows[index] >= 0
        ? drawnControls.offsetsPercent[index] : static_cast<int>(index % controlsPerView) * menu_style::rowStridePercent;
    x = panelX + panelWidth * menu_style::panelPaddingPercent / 100;
    top = screenHeight - (panelY + panelHeight)
        + panelHeight * (menu_style::contentTopPercent + offsetPercent) / 100;
    top -= static_cast<int>(scrollPosition * panelHeight * menu_style::rowStridePercent / 100);
    width = panelWidth * (100 - 2 * menu_style::panelPaddingPercent) / 100;
    int fieldRows = drawnControls.multiline[index] ? drawnControls.lineSlots[index] : 1;
    int rowHeight = drawnControls.descriptions[index] ? menu_style::descriptionRowHeightPercent
                                                    : menu_style::rowHeightPercent;
    height = panelHeight * (rowHeight
        + (fieldRows - 1) * menu_style::textLineHeightPercent) / 100;
    if (drawnControls.anchors[index]) height += panelHeight * menu_style::rowHeightPercent / 100;
    if (drawnControls.expanded[index]) height += panelHeight * 2 * menu_style::rowHeightPercent / 100;
    if (drawnControls.pageIndex >= 0 && drawnControls.parents[index] >= 0 && drawnControls.rows[index] >= 0) {
        int inset = panelWidth * 3 / 100;
        x += inset;
        width -= 2 * inset;
    }
}

bool contains(double x, double y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

// Use the same geometry for drawing and clicks, including scrolling and settings zoom.
void anchorRect(unsigned int index, int screenHeight, int& x, int& top, int& width, int& height) {
    int rowX, rowTop, rowWidth, rowHeight;
    pageItemRect(index, screenHeight, rowX, rowTop, rowWidth, rowHeight);
    int unit = (rowHeight - panelHeight * 2 / 100) / 9;
    int widthUnit = rowWidth * 60 / 100 / 16;
    if (widthUnit < unit) unit = widthUnit;
    if (unit < 1) unit = 1;
    width = unit * 16; height = unit * 9;
    x = rowX + rowWidth - width - panelWidth * 2 / 100;
    top = rowTop + (rowHeight - height) / 2;
}
void anchorDotRect(unsigned int index, int screenHeight, int corner,
                   int& x, int& top, int& width, int& height) {
    int boxX, boxTop, boxWidth, boxHeight;
    anchorRect(index, screenHeight, boxX, boxTop, boxWidth, boxHeight);
    int diameter = panelHeight * 3 / 100;
    if (diameter < 4) diameter = 4;
    width = height = diameter * 2; // Larger invisible click target around each dot.
    x = boxX + (corner % 2 ? boxWidth : 0) - diameter;
    top = boxTop + (corner / 2 ? boxHeight : 0) - diameter;
}

void textFieldRect(unsigned int index, int screenHeight,
                   int& x, int& top, int& width, int& height) {
    pageItemRect(index, screenHeight, x, top, width, height);
    top += panelHeight * 3 / 100;
    height -= panelHeight * 3 / 100;
}

int sliderValue(const PageItem& item) {
    int value = item.getSlider ? item.getSlider() : item.sliderMin;
    if (value < item.sliderMin) value = item.sliderMin;
    if (value > item.sliderMax) value = item.sliderMax;
    return value;
}

void sliderLabel(const PageItem& item, int value, char* output) {
    int i = 0;
    while (item.label && item.label[i] && i < (item.formatSlider ? 62 : 63)) { output[i] = item.label[i]; ++i; }
    output[i++] = ':';
    output[i++] = ' ';
    if (item.formatSlider) {
        char formatted[16]{};
        item.formatSlider(value, formatted);
        for (int j = 0; j < 15 && formatted[j]; ++j) output[i++] = formatted[j];
        output[i] = 0;
        return;
    }
    char digits[12];
    int count = 0;
    long long magnitude = value;
    if (magnitude < 0) { output[i++] = '-'; magnitude = -magnitude; }
    do { digits[count++] = static_cast<char>('0' + magnitude % 10); magnitude /= 10; }
    while (magnitude && count < static_cast<int>(sizeof(digits)));
    while (count) output[i++] = digits[--count];
    output[i] = 0;
}

void sliderTrackRect(unsigned int index, int screenHeight,
                     int& x, int& top, int& width, int& height) {
    int rowX, rowTop, rowWidth, rowHeight;
    pageItemRect(index, screenHeight, rowX, rowTop, rowWidth, rowHeight);
    width = panelWidth / 2;
    x = rowX + rowWidth - width;
    height = panelHeight * 2 / 100;
    if (height < 2) height = 2;
    top = rowTop + (rowHeight - height) / 2;
}

void sliderLabelLines(const PageItem& item, int value, int fontHeight, int maxWidth,
                      char* first, char* second) {
    char label[80];
    sliderLabel(item, value, label);
    if (custom_font_text_width(label, fontHeight) <= maxWidth) {
        int i = 0;
        do { first[i] = label[i]; } while (label[i++]);
        second[0] = 0;
        return;
    }

    int bestPosition = -1;
    int bestWidth = 0x7fffffff;
    for (int split = 1; label[split]; ++split) {
        if (label[split] != ' ') continue;
        char left[80], right[80];
        int leftLength = split;
        while (leftLength && label[leftLength - 1] == ' ') --leftLength;
        for (int i = 0; i < leftLength; ++i) left[i] = label[i];
        left[leftLength] = 0;
        int rightStart = split + 1;
        while (label[rightStart] == ' ') ++rightStart;
        int rightLength = 0;
        while (label[rightStart + rightLength]
               && rightLength < static_cast<int>(sizeof(right)) - 1) {
            right[rightLength] = label[rightStart + rightLength];
            ++rightLength;
        }
        right[rightLength] = 0;
        int leftWidthPixels = custom_font_text_width(left, fontHeight);
        int rightWidthPixels = custom_font_text_width(right, fontHeight);
        int wider = leftWidthPixels > rightWidthPixels ? leftWidthPixels : rightWidthPixels;
        if (wider < bestWidth) { bestWidth = wider; bestPosition = split; }
    }
    if (bestPosition < 0) {
        int i = 0;
        do { first[i] = label[i]; } while (label[i++]);
        second[0] = 0;
        return;
    }
    int firstLength = bestPosition;
    while (firstLength && label[firstLength - 1] == ' ') --firstLength;
    for (int i = 0; i < firstLength; ++i) first[i] = label[i];
    first[firstLength] = 0;
    int secondStart = bestPosition + 1;
    while (label[secondStart] == ' ') ++secondStart;
    int i = 0;
    while (label[secondStart + i] && i < 78) {
        second[i] = label[secondStart + i];
        ++i;
    }
    second[i] = 0;
}

int sliderFontHeight(const PageItem& item, unsigned int index, int screenHeight) {
    int x, top, width, height, trackX, trackTop, trackWidth, trackHeight;
    pageItemRect(index, screenHeight, x, top, width, height);
    sliderTrackRect(index, screenHeight, trackX, trackTop, trackWidth, trackHeight);
    (void)top;
    (void)width;
    (void)height;
    (void)trackTop;
    (void)trackWidth;
    (void)trackHeight;
    int available = trackX - x - panelHeight / 100;
    int fontHeight = panelHeight * 3 / 100;
    char first[80], second[80], minFirst[80], minSecond[80];
    sliderLabelLines(item, item.sliderMax, fontHeight, available, first, second);
    sliderLabelLines(item, item.sliderMin, fontHeight, available, minFirst, minSecond);
    int widest = custom_font_text_width(first, fontHeight);
    int secondWidth = custom_font_text_width(second, fontHeight);
    if (widest < secondWidth) widest = secondWidth;
    int minWidth = custom_font_text_width(minFirst, fontHeight);
    int minSecondWidth = custom_font_text_width(minSecond, fontHeight);
    if (minWidth < minSecondWidth) minWidth = minSecondWidth;
    if (widest < minWidth) widest = minWidth;
    if (widest > available && available > 0) fontHeight = fontHeight * available / widest;
    return fontHeight > 0 ? fontHeight : 1;
}

bool pageItemContains(int screenHeight, double x, double y) {
    for (int row = 0; row < drawnControls.count; ++row) {
        int i = drawnControls.indices[row];
        int left, top, width, height;
        pageItemRect(i, screenHeight, left, top, width, height);
        if (x >= left && x < left + width && y >= top && y < top + height) return true;
    }
    return false;
}

bool loadRendererApi() {
    void* library = mcpelauncher_host_dlopen("libEGL.so.1", 2);
    if (!library) return false;
    void* mathLibrary = mcpelauncher_host_dlopen("libm.so.6", 2);
    if (!mathLibrary) return false;
    hostExp2f = reinterpret_cast<decltype(hostExp2f)>(mcpelauncher_host_dlsym(mathLibrary, "exp2f"));
    if (!hostExp2f) return false;
    getContext = reinterpret_cast<decltype(getContext)>(mcpelauncher_host_dlsym(library, "eglGetCurrentContext"));
    getDisplay = reinterpret_cast<decltype(getDisplay)>(mcpelauncher_host_dlsym(library, "eglGetCurrentDisplay"));
    getSurface = reinterpret_cast<decltype(getSurface)>(mcpelauncher_host_dlsym(library, "eglGetCurrentSurface"));
    querySurface = reinterpret_cast<decltype(querySurface)>(mcpelauncher_host_dlsym(library, "eglQuerySurface"));
    auto getProc = reinterpret_cast<decltype(&eglGetProcAddress)>(mcpelauncher_host_dlsym(library, "eglGetProcAddress"));
    if (!getProc) return false;
    menuGetIntegerv = reinterpret_cast<decltype(menuGetIntegerv)>(getProc("glGetIntegerv"));
    menuIsEnabled = reinterpret_cast<decltype(menuIsEnabled)>(getProc("glIsEnabled"));
    menuEnable = reinterpret_cast<decltype(menuEnable)>(getProc("glEnable"));
    menuDisable = reinterpret_cast<decltype(menuDisable)>(getProc("glDisable"));
    menuScissor = reinterpret_cast<decltype(menuScissor)>(getProc("glScissor"));
    if (!menuGetIntegerv || !menuIsEnabled || !menuEnable || !menuDisable || !menuScissor) return false;
    if (!getContext || !getDisplay || !getSurface || !querySurface) return false;
    return true;
}

bool changedEnough(int current, int previous) {
    if (previous <= 0) return true;
    int difference = current > previous ? current - previous : previous - current;
    return difference * 100 >= previous * resizeThresholdPercent;
}

void drawSurface(int x, int top, int width, int height, int screenHeight,
                 MenuColor color, float cornerRadius = 0.0f, float borderWidth = 0.0f,
                 float tintAmount = 1.0f, float blurScale = 0.0f, float opacity = 1.0f) {
    PanelPaint paint;
    paint.inheritScissor = clippingContent;
    paint.tint = color;
    paint.cornerRadius = cornerRadius;
    paint.borderWidth = borderWidth;
    paint.tintAmount = tintAmount;
    paint.blurScale = blurScale;
    if (blurScale > 0.0f) paint.blurTimestampNs = fps_limiter_frame_timestamp_ns();
    paint.opacity = menuOpacity * opacity;
    draw_gl_panel(x, screenHeight - top - height, width, height, paint);
}

void updateSlider(unsigned int index, double x, MenuAction& action) {
    PageItem& item = pages[activePage].items[index];
    int trackX, trackTop, trackWidth, trackHeight;
    sliderTrackRect(index, lastScreenHeight, trackX, trackTop, trackWidth, trackHeight);
    if (trackWidth <= 0 || item.sliderMax <= item.sliderMin) return;
    double fraction = (x - trackX) / trackWidth;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    // Wide intermediates also support ranges that span negative and positive values.
    long long range = static_cast<long long>(item.sliderMax) - item.sliderMin;
    action.value = static_cast<int>(item.sliderMin + static_cast<long long>(fraction * range + 0.5));
    if (item.sliderStep > 1) {
        long long offset = static_cast<long long>(action.value) - item.sliderMin;
        action.value = static_cast<int>(item.sliderMin + (offset + item.sliderStep / 2) / item.sliderStep * item.sliderStep);
        if (action.value > item.sliderMax) action.value = item.sliderMax;
    }
    action.slider = item.onSlider;
}

bool handleMouseButton(double x, double y, int button, int action, MenuAction& pending) {
    if (!custom_menu_captures_input()) return false;
    if (action != mousePress) {
        if (button == leftMouseButton) draggingSlider = -1;
        return true;
    }
    draggingSlider = -1;
    if (settingsTransition.active && settingsTransition.exitPage >= 0) return true;
    int contentTop = lastScreenHeight - panelY - panelHeight + panelHeight * menu_style::contentTopPercent / 100;
    bool inContent = contains(x, y, panelX, contentTop, panelWidth, panelHeight * 70 / 100);
    if (button == leftMouseButton && !inContent) { resetPageInput(); return true; }
    if (activePage >= 0 && (drawnControls.pageIndex != activePage || drawnControls.view != controlView)) return true;
    if (activePage >= 0 && activePage < static_cast<int>(pageCount)) {
        Page& page = pages[activePage];
        if (button == rightMouseButton) {
            int panelTop = lastScreenHeight - (panelY + panelHeight);
            if (contains(x, y, panelX, panelTop, panelWidth, panelHeight)
                && (!inContent || !pageItemContains(lastScreenHeight, x, y))) backToTiles();
            return true;
        }
        if (button != leftMouseButton) return true;
        focusedTextBox = -1;
        bindingItem = -1;
        for (int row = 0; row < drawnControls.count; ++row) {
            int i = drawnControls.indices[row];
            int left, top, width, height;
            pageItemRect(i, lastScreenHeight, left, top, width, height);
            PageItem& item = page.items[i];
            if (item.type == pageAnchor) {
                for (int corner = 0; corner < 4; ++corner) {
                    int dotX, dotTop, dotWidth, dotHeight;
                    anchorDotRect(i, lastScreenHeight, corner, dotX, dotTop, dotWidth, dotHeight);
                    if (contains(x, y, dotX, dotTop, dotWidth, dotHeight)) {
                        pending.slider = item.onSlider; pending.value = corner;
                        return true;
                    }
                }
            }
            if (item.type == pageTextBox) textFieldRect(i, lastScreenHeight, left, top, width, height);
            if (!contains(x, y, left, top, width, height)) continue;
            if (item.type == pageTextBox) {
                focusedTextBox = static_cast<int>(i);
                caretStartNs = fps_limiter_frame_timestamp_ns();
            }
            else if (item.type == pageButton) pending.click = item.onClick;
            else if (item.type == pageDropdown) {
                int rowHeight = panelHeight * menu_style::rowHeightPercent / 100;
                if (x < left + width * 32 / 100) break;
                if (drawnControls.expanded[i] && y >= top + rowHeight) {
                    pending.toggle = item.onToggle;
                    pending.value = y >= top + 2 * rowHeight;
                    item.expanded = false;
                } else {
                    bool expand = !item.expanded;
                    for (unsigned int j = 0; j < page.itemCount; ++j) page.items[j].expanded = false;
                    item.expanded = expand;
                }
            }
            else if (item.type == pageKeyBind) bindingItem = i;
            else if ((item.type == pageToggle || item.type == pageChoice) && item.onToggle && item.getToggle) {
                pending.toggle = item.onToggle;
                pending.getToggle = item.getToggle;
                if (item.type == pageChoice) {
                    if (x < left + width * 32 / 100) break;
                    pending.getToggle = nullptr;
                    pending.value = x >= left + width * 66 / 100;
                }
            } else if (item.type == pageSlider) {
                int trackX, trackTop, trackWidth, trackHeight;
                sliderTrackRect(i, lastScreenHeight, trackX, trackTop, trackWidth, trackHeight);
                if (x < trackX || x >= trackX + trackWidth) continue;
                draggingSlider = static_cast<int>(i);
                updateSlider(i, x, pending);
            }
            break;
        }
        return true;
    }
    if (!inContent) return true;
    for (unsigned int i = 0; i < tileCount; ++i) {
        int left, top, width, height;
        tileRect(i, lastScreenHeight, left, top, width, height);
        if (!contains(x, y, left, top, width, height)) continue;
        Tile& tile = tiles[i];
        if (button == leftMouseButton && tile.onToggle && tile.getEnabled) {
            pending.toggle = tile.onToggle;
            pending.getToggle = tile.getEnabled;
        } else if (button == rightMouseButton) {
            if (tile.pageIndex >= 0 && tile.pageIndex < static_cast<int>(pageCount)) {
                settingsTransition = {true, fps_limiter_frame_timestamp_ns(), 0.0f};
                activePage = tile.pageIndex;
                controlView = 0;
                resetPageInput();
            }
            pending.click = tile.rightClick;
        }
        break;
    }
    return true;
}

bool onMouseButton(void*, double x, double y, int button, int action) {
    MenuAction pending;
    bool consumed;
    {
        UiLock lock;
        if (button > 0 && button < 8 && action != mousePress) {
            if (button == leftMouseButton) draggingSlider = -1;
            consumed = consumedMouseButtons[button];
            consumedMouseButtons[button] = false;
        } else {
            consumed = handleMouseButton(x, y, button, action, pending);
            if (button > 0 && button < 8 && consumed) consumedMouseButtons[button] = true;
        }
    }
    pending.run();
    return consumed;
}

bool onMouseScroll(void*, double x, double y, double, double dy) {
    UiLock lock;
    if (!custom_menu_captures_input()) return false;
    if (settingsTransition.active && settingsTransition.exitPage >= 0) return true;
    int top = lastScreenHeight - panelY - panelHeight;
    if (contains(x, y, panelX, top + panelHeight * menu_style::contentTopPercent / 100,
                 panelWidth, panelHeight * 70 / 100) && (dy > 0 || dy < 0)) {
        if (activePage < 0) {
            int rows = (static_cast<int>(tileCount) + gridColumns - 1) / gridColumns;
            int maximum = rows > 3 ? (rows - 3) * gridColumns : 0;
            tileOffset += dy > 0 ? -gridColumns : gridColumns;
            if (tileOffset < 0) tileOffset = 0;
            if (tileOffset > maximum) tileOffset = maximum;
        } else if (drawnControls.pageIndex == activePage) {
            controlView += dy > 0 ? -1 : 1;
            if (controlView < 0) controlView = 0;
            if (controlView >= drawnControls.viewCount) controlView = drawnControls.viewCount - 1;
            resetPageInput();
        }
    }
    return true;
}

bool onMousePosition(void*, double x, double y, bool relative) {
    MenuAction pending;
    {
        UiLock lock;
        if (relative) return false;
        cursorX = x;
        cursorY = y;
        cursorPositionValid = true;
        if (!custom_menu_captures_input() || activePage < 0 || draggingSlider < 0
            || draggingSlider >= static_cast<int>(pages[activePage].itemCount)
            || drawnControls.pageIndex != activePage || drawnControls.view != controlView
            || drawnControls.rows[draggingSlider] < 0) return false;
        updateSlider(static_cast<unsigned int>(draggingSlider), x, pending);
    }
    pending.run();
    return false;
}
}

MenuTile newTile(const char* text) {
    if (tileCount >= tileCapacity) {
        builderError = "Menu tile capacity reached";
        return MenuTile(tileCapacity);
    }
    unsigned int index = tileCount++;
    tiles[index] = {text, nullptr, nullptr, -1, nullptr, nullptr};
    return MenuTile(index);
}

MenuPage newPage(const char* title) {
    if (pageCount >= pageCapacity) {
        builderError = "Menu page capacity reached";
        return MenuPage(pageCapacity);
    }
    unsigned int index = pageCount++;
    pages[index].title = title;
    pages[index].itemCount = 0;
    pages[index].lastAppended = -1;
    return MenuPage(index);
}

namespace {
PageItem* appendPageItem(unsigned int pageIndex, PageItemType type, const char* label) {
    if (pageIndex >= pageCount) return nullptr;
    Page& page = pages[pageIndex];
    page.lastAppended = -1;
    if (page.itemCount >= pageItemCapacity) {
        builderError = "Menu control capacity reached";
        return nullptr;
    }
    page.lastAppended = static_cast<int>(page.itemCount);
    PageItem& item = page.items[page.itemCount++];
    item = {};
    item.parentToggle = -1;
    item.type = type;
    item.label = label;
    return &item;
}
}

MenuPage& MenuPage::text(const char* value) {
    appendPageItem(index, pageText, value);
    return *this;
}

MenuPage& MenuPage::groupWithPrevious() {
    if (index < pageCount && pages[index].lastAppended > 0)
        pages[index].items[pages[index].lastAppended].groupWithPrevious = true;
    return *this;
}

MenuPage& MenuPage::button(const char* label, void (*onClick)()) {
    if (PageItem* control = appendPageItem(index, pageButton, label)) {
        PageItem& item = *control;
        item.onClick = onClick;
    }
    return *this;
}

MenuPage& MenuPage::keyBind(const char* label, void (*onChange)(int), int (*getValue)()) {
    if (PageItem* item = appendPageItem(index, pageKeyBind, label)) {
        item->onSlider = onChange; item->getSlider = getValue;
    }
    return *this;
}

MenuPage& MenuPage::toggle(const char* label, void (*onChange)(bool), bool (*getValue)()) {
    if (PageItem* control = appendPageItem(index, pageToggle, label)) {
        PageItem& item = *control;
        item.onToggle = onChange;
        item.getToggle = getValue;
    }
    return *this;
}

MenuPage& MenuPage::slider(const char* label, int min, int max,
                             void (*onChange)(int), int (*getValue)(),
                             void (*formatValue)(int, char*), int step) {
    if (min >= max || step < 1 || (static_cast<long long>(max) - min) % step != 0) {
        if (index < pageCount) pages[index].lastAppended = -1;
        builderError = "Slider range/step invalid";
        return *this;
    }
    if (PageItem* control = appendPageItem(index, pageSlider, label)) {
        PageItem& item = *control;
        item.sliderMin = min;
        item.sliderMax = max;
        item.sliderStep = step;
        item.onSlider = onChange;
        item.getSlider = getValue;
        item.formatSlider = formatValue;
    }
    return *this;
}

MenuPage& MenuPage::anchor(const char* label, void (*onChange)(int), int (*getValue)()) {
    if (PageItem* item = appendPageItem(index, pageAnchor, label)) {
        item->onSlider = onChange; item->getSlider = getValue;
    }
    return *this;
}

MenuPage& MenuPage::choice(const char* label, const char* first, const char* second,
                           void (*onChange)(bool), bool (*getValue)()) {
    if (PageItem* item = appendPageItem(index, pageChoice, label)) {
        item->choices[0] = first;
        item->choices[1] = second;
        item->onToggle = onChange;
        item->getToggle = getValue;
    }
    return *this;
}

MenuPage& MenuPage::dropdown(const char* label, const char* first, const char* second,
                             void (*onChange)(bool), bool (*getValue)()) {
    choice(label, first, second, onChange, getValue);
    if (index < pageCount && pages[index].lastAppended >= 0)
        pages[index].items[pages[index].lastAppended].type = pageDropdown;
    return *this;
}

MenuPage& MenuPage::whenEnabled(bool (*isVisible)()) {
    if (index >= pageCount || pages[index].itemCount < 2) {
        builderError = "Dependent control needs a preceding toggle";
        return *this;
    }
    Page& page = pages[index];
    if (page.lastAppended < 0) return *this;
    PageItem& child = page.items[page.itemCount - 1];
    for (int parent = static_cast<int>(page.itemCount) - 2; parent >= 0; --parent) {
        if (page.items[parent].parentToggle >= 0) continue;
        if (page.items[parent].type == pageToggle) {
            child.parentToggle = parent;
            child.isVisible = isVisible;
            return *this;
        }
        break; // Dependent controls must be contiguous with their parent.
    }
    builderError = "Dependent control needs a preceding toggle";
    return *this;
}

MenuPage& MenuPage::textBox(const char* label, const char* initialValue,
                              void (*onChange)(const char*), bool multiline) {
    if (PageItem* control = appendPageItem(index, pageTextBox, label)) {
        PageItem& item = *control;
        item.onChange = onChange;
        item.multiline = multiline;
        if (initialValue) {
            int i = 0;
            while (initialValue[i] && i < static_cast<int>(sizeof(item.value)) - 1) {
                item.value[i] = initialValue[i];
                ++i;
            }
        }
    }
    return *this;
}

MenuTile& MenuTile::icon(const char* assetPath) {
    if (index < tileCount) tiles[index].iconPath = assetPath;
    return *this;
}

MenuTile& MenuTile::opens(MenuPage page) {
    if (index < tileCount) tiles[index].pageIndex = static_cast<int>(page.index);
    return *this;
}

MenuTile& MenuTile::onRightClick(void (*callback)()) {
    if (index < tileCount) tiles[index].rightClick = callback;
    return *this;
}

MenuTile& MenuTile::onToggle(void (*callback)(bool), bool (*getEnabled)()) {
    if (index < tileCount) {
        tiles[index].onToggle = callback;
        tiles[index].getEnabled = getEnabled;
    }
    return *this;
}

namespace {
void buildMenu() {
    if (menuBuilt) return;
    menuBuilt = true;
    declare_menu_pages();
}
}

void custom_menu_back_to_tiles() {
    UiLock lock;
    backToTiles();
}

const char* custom_menu_build_error() {
    UiLock lock;
    return builderError;
}

bool custom_menu_is_visible() {
    return __atomic_load_n(&open, __ATOMIC_RELAXED)
        || __atomic_load_n(&animationProgressPermille, __ATOMIC_RELAXED) > 0;
}

bool custom_menu_captures_input() {
    return __atomic_load_n(&open, __ATOMIC_RELAXED)
        && __atomic_load_n(&openingCaptureReady, __ATOMIC_RELAXED);
}

namespace {
bool handleTextBoxKeyboard(int key, int action, MenuAction& pending) {
    if (activePage < 0 || focusedTextBox < 0) return false;
    if (key == keyEscape) return false;
    if (action != press && action != 1) return true;
    PageItem& item = pages[activePage].items[focusedTextBox];
    char* value = item.value;
    const volatile char* valueChars = value;
    int length = 0;
    while (valueChars[length]) ++length;
    bool changed = false;
    if (key == 8 || key == 259) {
        if (length) {
            do { --length; } while (length && (static_cast<unsigned char>(value[length]) & 0xc0) == 0x80);
            value[length] = 0; changed = true;
        }
    } else if (key == 13 || key == 257) {
        if (item.multiline && (leftShiftHeld || rightShiftHeld)) {
            if (length < static_cast<int>(sizeof(item.value)) - 1) {
                value[length] = '\n'; value[length + 1] = 0; changed = true;
            }
        } else focusedTextBox = -1;
    } else if (length < static_cast<int>(sizeof(item.value)) - 1) {
        char character = 0;
        bool shifted = leftShiftHeld || rightShiftHeld;
        if (key >= 'A' && key <= 'Z') character = static_cast<char>((shifted != capsLock) ? key : key + 32);
        else if (key >= '0' && key <= '9') character = shifted ? ")!@#$%^&*("[key - '0'] : key;
        else if (key == 32) character = ' ';
        else {
            switch (key) {
                case 186: character = shifted ? ':' : ';'; break;
                case 187: character = shifted ? '+' : '='; break;
                case 188: character = shifted ? '<' : ','; break;
                case 189: character = shifted ? '_' : '-'; break;
                case 190: character = shifted ? '>' : '.'; break;
                case 191: character = shifted ? '?' : '/'; break;
                case 192: character = shifted ? '~' : '`'; break;
                case 219: character = shifted ? '{' : '['; break;
                case 220: character = shifted ? '|' : '\\'; break;
                case 221: character = shifted ? '}' : ']'; break;
                case 222: character = shifted ? '"' : '\''; break;
                case 106: character = '*'; break;
                case 107: character = '+'; break;
                case 108: character = ','; break;
                case 109: character = '-'; break;
                case 110: character = '.'; break;
                case 111: character = '/'; break;
                default: if (key >= 96 && key <= 105) character = '0' + key - 96;
            }
        }
        if (character) { value[length] = character; value[length + 1] = 0; changed = true; }
    }
    if (changed) caretStartNs = fps_limiter_frame_timestamp_ns();
    if (changed && item.onChange) {
        pending.text = item.onChange;
        for (unsigned int i = 0; i < sizeof(pending.textValue); ++i) pending.textValue[i] = value[i];
    }
    return true;
}
}

static bool handleKeyboard(int key, int action, MenuAction& pending) {
    if (bindingItem >= 0) {
        if (!custom_menu_captures_input() || activePage < 0
            || drawnControls.pageIndex != activePage || drawnControls.rows[bindingItem] < 0) bindingItem = -1;
        else {
            if (action == press) {
                if (key == keyEscape) bindingItem = -1;
                else if (key >= 32 && key < 512 && key != keyL) {
                    pending.slider = pages[activePage].items[bindingItem].onSlider;
                    pending.value = key;
                    bindingItem = -1;
                }
            }
            return true;
        }
    }
    if (key == keyEscape && action == press && activePage >= 0) {
        for (unsigned int i = 0; i < pages[activePage].itemCount; ++i) {
            if (pages[activePage].items[i].expanded) {
                pages[activePage].items[i].expanded = false;
                return true;
            }
        }
    }
    bool wasOpen = custom_menu_is_visible();
    bool targetWasOpen = __atomic_load_n(&open, __ATOMIC_RELAXED);
    if (custom_menu_captures_input() && handleTextBoxKeyboard(key, action, pending)) return true;
    if (key == keyEscape && wasOpen) {
        if (action == press) {
            if (activePage >= 0 && targetWasOpen) {
                backToTiles();
            } else {
                __atomic_store_n(&open, false, __ATOMIC_RELAXED);
                draggingSlider = -1;
            }
        }
        return true;
    }
    if (key != keyL) return custom_menu_captures_input() && action != release;
    if (!wasOpen && !game_window_is_mouse_locked(game_window_get_primary_window())) return false;
    if (action == press) {
        if (!targetWasOpen) {
            __atomic_store_n(&openingCaptureReady, false, __ATOMIC_RELAXED);
            buildMenu();
            if (!wasOpen) {
                layoutDirty = true;
                backToTiles();
            }
        } else {
            draggingSlider = -1;
        }
        __atomic_store_n(&open, !targetWasOpen, __ATOMIC_RELAXED);
    }
    return true; // Consume L, including repeats/releases, without toggling on repeats.
}

static bool handleKeyboardEvent(int key, int action, MenuAction& pending) {
    if (key == 20 && action == press) capsLock = !capsLock;
    if (key == keyLeftShift) leftShiftHeld = action != release;
    if (key == keyRightShift) rightShiftHeld = action != release;
    if (key >= 0 && key < 512) {
        if (action == release) {
            // Always consume releases for keys swallowed before closing.
            bool consumed = consumedKeys[key];
            consumedKeys[key] = false;
            if (consumed) return true;
            return false; // Release any movement held before the menu captured input.
        }
        if (action != press && consumedKeys[key]
            && !(focusedTextBox >= 0 && custom_menu_captures_input())) return true;
    }
    bool consumed = handleKeyboard(key, action, pending);
    if (consumed && key >= 0 && key < 512)
        consumedKeys[key] = true;
    return consumed;
}

bool custom_menu_on_keyboard(int key, int action) {
    MenuAction pending;
    bool consumed;
    {
        UiLock lock;
        consumed = handleKeyboardEvent(key, action, pending);
    }
    pending.run();
    return consumed;
}

void custom_menu_register_mouse_callback(GameWindowHandle* window) {
    game_window_add_mouse_button_callback(window, nullptr, onMouseButton);
    game_window_add_mouse_position_callback(window, nullptr, onMousePosition);
    game_window_add_mouse_scroll_callback(window, nullptr, onMouseScroll);
}

namespace {
float menuScaleForScreen(int screenWidth, int screenHeight) {
    int maxWidth = screenWidth * layoutScalePercent / 100;
    int maxHeight = screenHeight * layoutScalePercent / 100;
    float fittingScale = static_cast<float>(maxWidth) / referenceWidth;
    float heightScale = static_cast<float>(maxHeight) / referenceHeight;
    if (heightScale < fittingScale) fittingScale = heightScale;
    float scale = 0.5f;
    for (int percentage : ui_scale::percentages) {
        float candidate = percentage / 100.0f;
        if (candidate > fittingScale) break;
        scale = candidate;
    }
    return scale;
}

void updateLayout(int screenWidth, int screenHeight) {
    bool needsLayout = layoutDirty
        || changedEnough(screenWidth, lastScreenWidth)
        || changedEnough(screenHeight, lastScreenHeight);
    layoutDirty = false;
    if (needsLayout) {
        float scale = menuScaleForScreen(screenWidth, screenHeight);
        panelWidth = static_cast<int>(referenceWidth * scale);
        panelHeight = static_cast<int>(referenceHeight * scale);
        panelX = (screenWidth - panelWidth) / 2;
        centeredPanelY = (screenHeight - panelHeight) / 2;
        lastScreenWidth = screenWidth;
        lastScreenHeight = screenHeight;
    }
}

bool updateAnimation(long long frameNs) {
    bool targetOpen = __atomic_load_n(&open, __ATOMIC_RELAXED);
    if (targetOpen != animationTargetOpen) {
        animationTargetOpen = targetOpen;
        animationFromPermille = __atomic_load_n(&animationProgressPermille, __ATOMIC_RELAXED);
        animationStartNs = frameNs;
    }
    int progress = targetOpen ? 1000 : 0;
    bool captureReady = targetOpen;
    if (frameNs > 0 && animationStartNs > 0) {
        long long elapsed = frameNs - animationStartNs;
        captureReady = targetOpen && elapsed >= menu_style::openingCaptureDelayNs;
        float t = elapsed <= 0 ? 0.0f : static_cast<float>(elapsed) / menu_style::animationDurationNs;
        if (t > 1.0f) t = 1.0f;
        float eased = ui_animation::ease_out_exponential(t, hostExp2f);
        int targetPermille = targetOpen ? 1000 : 0;
        float value = animationFromPermille
            + (targetPermille - animationFromPermille) * eased;
        progress = static_cast<int>(value + 0.5f);
        if (targetOpen && elapsed < menu_style::animationDurationNs && progress >= 1000) progress = 999;
    }
    __atomic_store_n(&openingCaptureReady, captureReady, __ATOMIC_RELAXED);
    __atomic_store_n(&animationProgressPermille, progress, __ATOMIC_RELAXED);
    if (!targetOpen && progress == 0) {
        backToTiles();
        return false;
    }
    menuOpacity = static_cast<float>(progress) / 1000.0f;
    panelY = centeredPanelY - static_cast<int>(panelHeight * 0.1f * (1.0f - menuOpacity));
    return true;
}

struct MenuFrame {
    Tile visibleTiles[tileCapacity]{};
    Page page{};
    int pageIndex = -1;
    int tileFirst = 0;
    int tileEnd = 0;
    int binding = -1;
    int requestedControlView = 0;
    ControlLayout controls;
    int focused = -1;
    bool caretVisible = false;
    double cursorX = 0.0, cursorY = 0.0;
    bool cursorValid = false;
    int hovered = -1;
    const char* error = nullptr;
    SettingsTransition transition;
};

// Render-thread state, with independent transitions for every tile/page control.
struct ButtonMotion {
    float hover = 0, enabled = 0, fromHover = 0, fromEnabled = 0;
    bool targetHover = false, targetEnabled = false, initialized = false;
    long long startNs = 0;
};
ButtonMotion buttonMotions[pageCapacity + 1][pageItemCapacity * 3];

void updateButtonMotion(ButtonMotion& motion, bool hover, bool enabled, long long now) {
    if (!motion.initialized) {
        motion.initialized = true;
        motion.enabled = motion.fromEnabled = enabled ? 1.0f : 0.0f;
        motion.targetEnabled = enabled;
    }
    float t = now <= motion.startNs ? 0.0f
        : static_cast<float>(now - motion.startNs) / menu_style::buttonTransitionNs;
    if (t > 1.0f) t = 1.0f;
    float eased = ui_animation::ease_out_quart(t);
    motion.hover = motion.fromHover + (static_cast<float>(motion.targetHover) - motion.fromHover) * eased;
    motion.enabled = motion.fromEnabled + (static_cast<float>(motion.targetEnabled) - motion.fromEnabled) * eased;
    if (motion.targetHover != hover || motion.targetEnabled != enabled) {
        motion.fromHover = motion.hover;
        motion.fromEnabled = motion.enabled;
        motion.targetHover = hover;
        motion.targetEnabled = enabled;
        motion.startNs = now;
    }
}

MenuColor blendButtonColor(MenuColor from, MenuColor to, float amount) {
    return {from.red + (to.red - from.red) * amount,
            from.green + (to.green - from.green) * amount,
            from.blue + (to.blue - from.blue) * amount};
}

void drawButton(const MenuFrame& frame, int id, int x, int top, int width, int height,
                int screenHeight, float radius, bool settings = false, bool enabled = false) {
    int slot = id < 100 ? id : (id < 200 ? id - 100 : pageItemCapacity + id - 200);
    ButtonMotion& motion = buttonMotions[settings ? frame.pageIndex + 1 : 0][slot];
    updateButtonMotion(motion, frame.hovered == id, enabled, fps_limiter_frame_timestamp_ns());
    MenuColor color = blendButtonColor(settings ? menu_style::settingsButton : menu_style::mainButton,
        settings ? menu_style::settingsButtonEnabled : menu_style::mainButtonEnabled, motion.enabled);
    MenuColor hoverColor = blendButtonColor(settings ? menu_style::settingsButtonHover : menu_style::mainButtonHover,
        settings ? menu_style::settingsButtonEnabledHover : menu_style::mainButtonEnabledHover, motion.enabled);
    color = blendButtonColor(color, hoverColor, motion.hover);
    float opacity = menu_style::buttonOpacity
        + (menu_style::enabledButtonOpacity - menu_style::buttonOpacity) * motion.enabled;
    float hoverOpacity = menu_style::hoverButtonOpacity
        + (menu_style::enabledHoverButtonOpacity - menu_style::hoverButtonOpacity) * motion.enabled;
    opacity += (hoverOpacity - opacity) * motion.hover;
    drawSurface(x, top, width, height, screenHeight, color, radius,
                0.0f, 1.0f, 0.0f, opacity);
    float thickness = settings ? menu_style::settingsButtonOutlineThickness : menu_style::mainButtonOutlineThickness;
    if (thickness > 0.0f) {
        MenuColor outline = blendButtonColor(
            settings ? menu_style::settingsButtonOutline : menu_style::mainButtonOutline,
            settings ? menu_style::settingsButtonEnabledOutline : menu_style::mainButtonEnabledOutline,
            motion.enabled);
        drawSurface(x, top, width, height, screenHeight, outline, radius, thickness,
                    1.0f, 0.0f, settings ? menu_style::settingsButtonOutlineOpacity : menu_style::buttonOutlineOpacity);
    }
}

// Render-thread text context: all page controls use settled glyphs during transitions.
int pageTextHeight, pageTextWidth;
float pageTextScale = 1.0f;
int pageFontHeight(int percent) {
    int height = (pageTextHeight ? pageTextHeight : panelHeight) * percent / 100;
    return height > 0 ? height : 1;
}
void drawLabel(const char* text, int x, int top, int width, int height,
               int fontHeight, int screenWidth, int screenHeight, bool centered = true) {
    if (!text || width <= 0 || height <= 0) return;
    float textScale = pageTextScale;
    char label[65]{};
    int length = 0;
    while (text[length] && length < 64) { label[length] = text[length]; ++length; }
    bool truncated = text[length] != 0;
    while (length > 3 && (truncated || custom_font_text_width(label, fontHeight) * textScale > width)) {
        --length;
        label[length] = 0;
        label[length - 1] = label[length - 2] = label[length - 3] = '.';
        truncated = false;
    }
    float textTop = textScale == 1.0f ? top + (height - fontHeight) / 2
        : top + (height - fontHeight * textScale) * 0.5f;
    custom_font_draw_scaled(label, centered ? x + width / 2.0f : x, textTop,
                            fontHeight, textScale, screenWidth, screenHeight, centered);
}

// Every boolean setting uses this label/track/thumb renderer through MenuPage::toggle.
void drawToggle(const MenuFrame& frame, int i, int x, int top, int width, int height,
                int screenWidth, int screenHeight) {
    int inset = frame.page.items[i].parentToggle >= 0 ? 0 : width * 3 / 100;
    drawLabel(frame.page.items[i].label, x + inset, top, width * 73 / 100,
              height, pageFontHeight(3), screenWidth, screenHeight, false);
    int trackHeight = panelHeight * 6 / 100, trackTop = top + (height - trackHeight) / 2;
    int padding = trackHeight / 10;
    if (padding < 1) padding = 1;
    int size = trackHeight - padding * 2;
    int trackWidth = size * 2 + padding * 2;
    int trackX = x + width - inset - trackWidth;
    float radius = trackHeight * menu_style::switchRadiusPercent / 100.0f;
    drawButton(frame, 100 + i, trackX, trackTop, trackWidth, trackHeight,
               screenHeight, radius, true, frame.controls.enabled[i]);
    const ButtonMotion& motion = buttonMotions[frame.pageIndex + 1][i];
    int thumbX = trackX + padding
        + static_cast<int>((trackWidth - padding * 2 - size) * motion.enabled + 0.5f);
    int thumbTop = trackTop + padding;
    drawSurface(thumbX, thumbTop, size, size, screenHeight, menu_style::settingsButton,
                size * menu_style::switchRadiusPercent / 100.0f, 0.0f, 1.0f, 0.0f,
                menu_style::switchThumbOpacity + menu_style::switchThumbHoverOpacity * motion.hover);
    custom_font_set_opacity(menuOpacity * (1.0f - motion.enabled));
    custom_font_draw_icon("assets/icon-switch-cross.png", thumbX + size / 2, thumbTop, size,
                          1.0f, 1.0f, 1.0f, screenWidth, screenHeight);
    custom_font_set_opacity(menuOpacity * motion.enabled);
    custom_font_draw_icon("assets/icon-switch-check.png", thumbX + size / 2, thumbTop, size,
                          1.0f, 1.0f, 1.0f, screenWidth, screenHeight);
    custom_font_set_opacity(menuOpacity);
}

// Render-thread state: the base title slides while the suffix fades in.
struct TitleMotion {
    const char* pageTitle = nullptr;
    float from = 0.0f, target = 0.0f, offset = 0.0f; // Fractions of panel width.
    long long startNs = 0;
};
TitleMotion titleMotion;

void drawHeader(const char* pageTitle, int screenWidth, int screenHeight) {
    int top = screenHeight - panelY - panelHeight;
    int padding = panelWidth * menu_style::panelPaddingPercent / 100;
    char title[65] = "ODIClient";
    if (pageTitle) {
        unsigned int length = 9;
        const char* separator = " - ";
        while (*separator) title[length++] = *separator++;
        while (*pageTitle && length < sizeof(title) - 1) title[length++] = *pageTitle++;
        title[length] = 0;
    }
    int fontHeight = panelHeight * 4 / 100;
    unsigned int length = 0;
    while (title[length]) ++length;
    while (length > 3 && custom_font_text_width(title, fontHeight) > panelWidth - 2 * padding) {
        title[--length] = 0;
        title[length - 1] = title[length - 2] = title[length - 3] = '.';
    }
    int baseWidth = custom_font_text_width("ODIClient", fontHeight);
    float target = static_cast<float>(baseWidth - custom_font_text_width(title, fontHeight))
        / (2.0f * panelWidth);
    long long now = fps_limiter_frame_timestamp_ns();
    float progress = now <= titleMotion.startNs ? 0.0f
        : static_cast<float>(now - titleMotion.startNs) / menu_style::titleMotionDurationNs;
    if (progress > 1.0f) progress = 1.0f;
    titleMotion.offset = titleMotion.from + (titleMotion.target - titleMotion.from)
        * ui_animation::ease_out_quart(progress);
    if (titleMotion.pageTitle != pageTitle) {
        titleMotion.pageTitle = pageTitle;
        titleMotion.from = titleMotion.offset;
        titleMotion.startNs = now;
        progress = 0.0f;
    }
    titleMotion.target = target;
    if (progress >= 1.0f) titleMotion.offset = target;
    int left = panelX + panelWidth / 2 - baseWidth / 2
        + static_cast<int>(titleMotion.offset * panelWidth);
    int textTop = top + panelHeight * 5 / 100 + (panelHeight * 7 / 100 - fontHeight) / 2;
    custom_font_draw_left("ODIClient", left, textTop, fontHeight, screenWidth, screenHeight);
    if (pageTitle) {
        custom_font_set_opacity(menuOpacity * progress);
        custom_font_draw_left(title + 9, left + baseWidth, textTop, fontHeight, screenWidth, screenHeight);
        custom_font_set_opacity(menuOpacity);
    }
}

void drawFooter(const MenuFrame& frame, int screenWidth, int screenHeight) {
    int top = screenHeight - panelY - panelHeight + panelHeight * menu_style::footerTopPercent / 100;
    int fontHeight = panelHeight * 2 / 100;
    const char* hint = frame.pageIndex < 0
        ? (tileCount > tilesPerView ? "Scroll: modules  /  Left: toggle  /  Right: settings" : "Left: toggle  /  Right: settings")
        : (frame.controls.viewCount > 1 ? "Scroll: settings  /  Esc: back  /  L: close" : "Esc: back  /  L: close");
    drawLabel(frame.error ? frame.error : hint, panelX + panelWidth * 8 / 100, top,
              panelWidth * 84 / 100, panelHeight * 6 / 100, fontHeight, screenWidth, screenHeight);
    int creditHeight = panelHeight * 2 / 100;
    custom_font_set_opacity(menuOpacity * menu_style::creditOpacity);
    custom_font_draw("IImoxi", panelX + panelWidth / 2,
                     screenHeight - panelY - panelHeight + panelHeight * 97 / 100,
                     creditHeight, screenWidth, screenHeight);
    custom_font_set_opacity(menuOpacity);
}

void drawTile(const MenuFrame& frame, int index, const Tile& tile, int x, int top, int width, int height,
              int screenWidth, int screenHeight) {
        bool enabled = tile.getEnabled && tile.getEnabled();
        float radius = panelHeight * menu_style::mainButtonOutlineRadiusPercent / 100.0f;
        drawButton(frame, index, x, top, width, height, screenHeight, radius, false, enabled);
        int labelHeight = panelHeight * 3 / 100;
        int labelTop = top + height * (tile.iconPath ? 75 : 24) / 100;
        if (tile.iconPath) {
            int iconSize = height * 50 / 100;
            custom_font_draw_icon(tile.iconPath, x + width / 2, top + height * 10 / 100, iconSize,
                                  1.0f, 1.0f, 1.0f,
                                  screenWidth, screenHeight);
            draw_gl_divider(x, screenHeight - (top + height * 74 / 100) - 1, width,
                            menuOpacity * menu_style::tileDividerOpacity, clippingContent);
        }
        drawLabel(tile.text, x + width * 6 / 100, labelTop, width * 88 / 100,
                  height * 22 / 100, labelHeight, screenWidth, screenHeight);
        if (!tile.iconPath && tile.onToggle)
            drawLabel(enabled ? "ON" : "OFF", x, top + height * 65 / 100, width, height / 5,
                      panelHeight * 2 / 100, screenWidth, screenHeight);
}

void drawTiles(const MenuFrame& frame, int screenWidth, int screenHeight) {
    int rows = (static_cast<int>(tileCount) + gridColumns - 1) / gridColumns;
    if (rows > 3) {
        int x = panelX + panelWidth * 94 / 100;
        int top = screenHeight - panelY - panelHeight + panelHeight * menu_style::contentTopPercent / 100;
        int height = panelHeight * 67 / 100;
        int thumb = height * 3 / rows;
        drawSurface(x, top, 2, height, screenHeight, menu_style::scrollTrack,
                    menu_style::scrollRadius, 0.0f, 1.0f, 0.0f, menu_style::scrollTrackOpacity);
        drawSurface(x, top + static_cast<int>((height - thumb) * scrollPosition / (rows - 3)),
                    2, thumb, screenHeight, menu_style::scrollThumb,
                    menu_style::scrollRadius, 0.0f, 1.0f, 0.0f, menu_style::scrollThumbOpacity);
    }
    for (int i = frame.tileFirst; i < frame.tileEnd; ++i) {
        const Tile& tile = frame.visibleTiles[i - frame.tileFirst];
        int x, top, width, height;
        tileRect(i, screenHeight, x, top, width, height);
        drawTile(frame, i, tile, x, top, width, height, screenWidth, screenHeight);
    }
}

void drawSlider(const PageItem& item, unsigned int index, int screenWidth, int screenHeight) {
    int x, top, width, height, trackX, trackTop, trackWidth, trackHeight;
    pageItemRect(index, screenHeight, x, top, width, height);
    sliderTrackRect(index, screenHeight, trackX, trackTop, trackWidth, trackHeight);
    int value = sliderValue(item);
    // Fit and wrap at the settled dimensions, then scale the resulting lines together.
    int animatedHeight = panelHeight, animatedWidth = panelWidth;
    if (pageTextHeight) { panelHeight = pageTextHeight; panelWidth = pageTextWidth; }
    int fontHeight = sliderFontHeight(item, index, screenHeight);
    char first[80], second[80];
    int settledX, settledTop, settledWidth, settledHeight, labelX, labelTop, labelWidth, labelHeight;
    pageItemRect(index, screenHeight, settledX, settledTop, settledWidth, settledHeight);
    sliderTrackRect(index, screenHeight, labelX, labelTop, labelWidth, labelHeight);
    sliderLabelLines(item, value, fontHeight, labelX - settledX - panelHeight / 100, first, second);
    panelHeight = animatedHeight; panelWidth = animatedWidth;
    int lineCount = second[0] ? 2 : 1;
    float displayHeight = fontHeight * pageTextScale;
    float textTop = pageTextScale == 1.0f ? top + (height - fontHeight * lineCount) / 2
        : top + (height - displayHeight * lineCount) * 0.5f;
    float radius = trackHeight * menu_style::sliderTrackRadiusPercent / 100.0f;
    drawSurface(trackX, trackTop, trackWidth, trackHeight, screenHeight, menu_style::sliderTrack, radius,
                0.0f, 1.0f, 0.0f, menu_style::sliderTrackOpacity);
    long long range = static_cast<long long>(item.sliderMax) - item.sliderMin;
    int fillWidth = range > 0 ? static_cast<int>((static_cast<long long>(value) - item.sliderMin) * trackWidth / range) : 0;
    if (fillWidth > 0)
        drawSurface(trackX, trackTop, fillWidth, trackHeight, screenHeight, menu_style::sliderFill, radius,
                    0.0f, 1.0f, 0.0f, menu_style::sliderFillOpacity);
    int handleWidth = panelHeight * 2 / 100;
    if (handleWidth < 4) handleWidth = 4;
    int handleHeight = panelHeight * 4 / 100;
    if (handleHeight < 4) handleHeight = 4;
    int handleX = trackX + fillWidth - handleWidth / 2;
    if (handleX < trackX) handleX = trackX;
    if (handleX > trackX + trackWidth - handleWidth) handleX = trackX + trackWidth - handleWidth;
    drawSurface(handleX, trackTop + (trackHeight - handleHeight) / 2, handleWidth, handleHeight,
                screenHeight, menu_style::sliderHandle,
                handleHeight * menu_style::sliderHandleRadiusPercent / 100.0f,
                0.0f, 1.0f, 0.0f, menu_style::sliderHandleOpacity);
    custom_font_draw_left_scaled(first, x, textTop, fontHeight, pageTextScale, screenWidth, screenHeight);
    if (second[0]) custom_font_draw_left_scaled(second, x, textTop + displayHeight,
                                               fontHeight, pageTextScale, screenWidth, screenHeight);
}

// Render-thread state, relative to the textbox so scrolling moves it with the field.
struct CaretMotion {
    int page = -1, item = -1, fontHeight = 0, width = 0;
    float fromX = 0, fromY = 0, targetX = 0, targetY = 0;
    long long start = 0;
} caretMotion;
void caretPosition(int page, int item, int fontHeight, int width, int& x, int& y) {
    long long now = fps_limiter_frame_timestamp_ns();
    auto& motion = caretMotion;
    if (motion.page != page || motion.item != item || motion.fontHeight != fontHeight || motion.width != width) {
        motion = {page, item, fontHeight, width, static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(x), static_cast<float>(y), now};
    }
    float t = now <= motion.start ? 0.0f
        : static_cast<float>(now - motion.start) / menu_style::caretMotionDurationNs;
    float eased = ui_animation::ease_out_exponential(t, hostExp2f);
    float currentX = motion.fromX + (motion.targetX - motion.fromX) * eased;
    float currentY = motion.fromY + (motion.targetY - motion.fromY) * eased;
    if (motion.targetX != x || motion.targetY != y) {
        motion.fromX = currentX; motion.fromY = currentY;
        motion.targetX = x; motion.targetY = y; motion.start = now;
    }
    x = static_cast<int>(currentX + 0.5f); y = static_cast<int>(currentY + 0.5f);
}

void drawPageControls(const MenuFrame& frame, int screenWidth, int screenHeight, int settledHeight = 0,
                      int settledWidth = 0, float scale = 0.0f) {
    if (!settledHeight) settledHeight = panelHeight;
    pageTextHeight = settledHeight;
    pageTextWidth = settledWidth ? settledWidth : panelWidth * settledHeight / panelHeight;
    pageTextScale = scale > 0.0f ? scale : static_cast<float>(panelHeight) / settledHeight;
    if (frame.focused < 0) caretMotion.item = -1;
    if (frame.controls.viewCount > 1) {
        int x = panelX + panelWidth * 94 / 100;
        int top = screenHeight - panelY - panelHeight + panelHeight * menu_style::contentTopPercent / 100;
        int height = panelHeight * 67 / 100;
        int thumb = height * controlsPerView / frame.controls.slots;
        drawSurface(x, top, 2, height, screenHeight, menu_style::scrollTrack,
                    menu_style::scrollRadius, 0.0f, 1.0f, 0.0f, menu_style::scrollTrackOpacity);
        drawSurface(x, top + static_cast<int>((height - thumb) * scrollPosition / (frame.controls.viewCount - 1)),
                    2, thumb, screenHeight, menu_style::scrollThumb,
                    menu_style::scrollRadius, 0.0f, 1.0f, 0.0f, menu_style::scrollThumbOpacity);
    }
    for (int row = 0; row < frame.controls.count; ++row) {
        int i = frame.controls.indices[row];
        const PageItem& item = frame.page.items[i];
        int x, top, width, height;
        pageItemRect(i, screenHeight, x, top, width, height);
        if (frame.controls.dividers[i]) {
            int previousX, previousTop, previousWidth, previousHeight;
            pageItemRect(frame.controls.indices[row - 1], screenHeight,
                         previousX, previousTop, previousWidth, previousHeight);
            int dividerTop = (previousTop + previousHeight + top) / 2;
            int dividerX = panelX + panelWidth * menu_style::panelPaddingPercent / 100;
            int dividerWidth = panelWidth * (100 - 2 * menu_style::panelPaddingPercent) / 100;
            draw_gl_divider(dividerX, screenHeight - dividerTop - 1, dividerWidth,
                            menuOpacity * menu_style::settingsDividerOpacity, clippingContent);
        }
        int fontHeight = pageFontHeight(3);
        if (item.type == pageText) {
            int settledFontHeight = settledHeight * menu_style::descriptionFontHeightPercent / 100;
            if (settledFontHeight < 1) settledFontHeight = 1;
            int padding = width * 3 / 100;
            drawLabel(item.label, x + padding, top, width - padding * 2, height,
                      settledFontHeight, screenWidth, screenHeight, false);
        } else if (item.type == pageKeyBind) {
            char label[96];
            int key = item.getSlider ? item.getSlider() : 0;
            unsigned int n = 0;
            while (item.label[n] && n < 70) { label[n] = item.label[n]; ++n; }
            label[n++] = ':'; label[n++] = ' ';
            if (key >= 32 && key <= 126) label[n++] = static_cast<char>(key);
            else {
                label[n++] = '#';
                if (key >= 100) label[n++] = static_cast<char>('0' + key / 100);
                if (key >= 10) label[n++] = static_cast<char>('0' + (key / 10) % 10);
                label[n++] = static_cast<char>('0' + key % 10);
            }
            label[n] = 0;
            drawLabel(frame.binding == i ? "Press a key (Esc cancels)" : label,
                      x, top, width, height, fontHeight, screenWidth, screenHeight);
        } else if (item.type == pageButton) {
            drawButton(frame, 100 + i, x, top, width, height, screenHeight,
                       panelHeight * menu_style::settingsButtonOutlineRadiusPercent / 100.0f, true);
            drawLabel(item.label, x, top, width, height, fontHeight, screenWidth, screenHeight);
        } else if (item.type == pageToggle) {
            drawToggle(frame, i, x, top, width, height, screenWidth, screenHeight);
        } else if (item.type == pageDropdown) {
            int rowHeight = panelHeight * menu_style::rowHeightPercent / 100;
            int selectX = x + width * 32 / 100, selectWidth = width * 68 / 100;
            drawLabel(item.label, x, top, width * 30 / 100, rowHeight,
                      fontHeight, screenWidth, screenHeight, false);
            drawButton(frame, 100 + i, selectX, top, selectWidth, rowHeight, screenHeight,
                       panelHeight * menu_style::settingsChoiceRadiusPercent / 100.0f, true);
            drawLabel(item.choices[frame.controls.enabled[i] ? 1 : 0], selectX, top,
                      selectWidth * 85 / 100, rowHeight, fontHeight, screenWidth, screenHeight);
            drawLabel(item.expanded ? "^" : "v", selectX + selectWidth * 85 / 100, top,
                      selectWidth * 15 / 100, rowHeight, fontHeight, screenWidth, screenHeight);
            if (item.expanded) {
                drawSurface(selectX, top + rowHeight, selectWidth, 2 * rowHeight,
                            screenHeight, menu_style::background,
                            panelHeight * menu_style::settingsChoiceRadiusPercent / 100.0f);
                for (int option = 0; option < 2; ++option) {
                    drawButton(frame, 200 + i * 2 + option, selectX, top + (option + 1) * rowHeight,
                               selectWidth, rowHeight, screenHeight, 0.0f, true,
                               frame.controls.enabled[i] == static_cast<bool>(option));
                    drawLabel(item.choices[option], selectX, top + (option + 1) * rowHeight,
                              selectWidth, rowHeight, fontHeight, screenWidth, screenHeight);
                }
            }
        } else if (item.type == pageChoice) {
            drawLabel(item.label, x, top, width * 30 / 100, height,
                      fontHeight, screenWidth, screenHeight, false);
            for (int option = 0; option < 2; ++option) {
                int optionX = x + width * (32 + option * 34) / 100;
                int optionWidth = width * 32 / 100;
                drawButton(frame, 200 + i * 2 + option, optionX, top + height / 5,
                           optionWidth, height * 3 / 5, screenHeight,
                           panelHeight * menu_style::settingsChoiceRadiusPercent / 100.0f,
                           true, frame.controls.enabled[i] == static_cast<bool>(option));
                drawLabel(item.choices[option], optionX, top, optionWidth, height,
                          fontHeight, screenWidth, screenHeight);
            }
        } else if (item.type == pageAnchor) {
            drawLabel(item.label, x, top, width * 30 / 100, height,
                      fontHeight, screenWidth, screenHeight, false);
            int boxX, boxTop, boxWidth, boxHeight;
            anchorRect(i, screenHeight, boxX, boxTop, boxWidth, boxHeight);
            drawSurface(boxX, boxTop, boxWidth, boxHeight, screenHeight,
                        menu_style::control, 0.0f, 1.0f, 1.0f, 0.0f, 0.7f);
            int selected = item.getSlider ? item.getSlider() : 0;
            for (int corner = 0; corner < 4; ++corner) {
                int dotX, dotTop, dotWidth, dotHeight;
                anchorDotRect(i, screenHeight, corner, dotX, dotTop, dotWidth, dotHeight);
                bool hovered = frame.cursorValid && contains(frame.cursorX, frame.cursorY,
                                                            dotX, dotTop, dotWidth, dotHeight);
                int diameter = dotWidth / 2;
                drawSurface(dotX + diameter / 2, dotTop + diameter / 2, diameter, diameter,
                            screenHeight, {1, 1, 1}, diameter / 2.0f, 0.0f, 1.0f, 0.0f,
                            selected == corner ? 1.0f : hovered ? 0.65f : 0.25f);
            }
        } else if (item.type == pageSlider) {
            drawSlider(item, i, screenWidth, screenHeight);
        } else if (item.type == pageTextBox) {
            custom_font_draw_left_scaled(item.label, x, top, pageFontHeight(2),
                                         pageTextScale, screenWidth, screenHeight);
            textFieldRect(i, screenHeight, x, top, width, height);
            float radius = panelHeight * 1.5f / 100.0f;
            drawSurface(x, top, width, height, screenHeight, menu_style::control, radius, 0.0f, menu_style::textBoxTint);
            if (frame.focused == i)
                drawSurface(x, top, width, height, screenHeight, menu_style::focus, radius, 1.0f);
            int textX = x + width * 3 / 100, textWidth = width * 94 / 100;
            int lineHeight = panelHeight * menu_style::textLineHeightPercent / 100;
            int visibleLines = item.multiline ? frame.controls.lineSlots[i] : 1;
            int lines = 1;
            for (const char* c = item.value; *c; ++c) if (*c == '\n') ++lines;
            int skip = frame.focused == i && lines > visibleLines ? lines - visibleLines : 0;
            const char* text = item.value;
            while (skip--) { while (*text && *text != '\n') ++text; if (*text) ++text; }
            for (int line = 0; line < visibleLines; ++line) {
                char value[256];
                int length = 0;
                while (*text && *text != '\n') value[length++] = *text++;
                value[length] = 0;
                bool last = !*text;
                const char* shown = value;
                if (frame.focused == i) while (*shown && custom_font_text_width(shown, fontHeight) * pageTextScale > textWidth - 3) {
                    ++shown;
                    while ((static_cast<unsigned char>(*shown) & 0xc0) == 0x80) ++shown;
                }
                int displayHeight = static_cast<int>(fontHeight * pageTextScale + 0.5f);
                int textBlockHeight = displayHeight + (visibleLines - 1) * lineHeight;
                int lineTop = top + (height - textBlockHeight) / 2 + line * lineHeight;
                drawLabel(shown, textX, lineTop, textWidth, displayHeight,
                          fontHeight, screenWidth, screenHeight, false);
                if (last && frame.focused == i) {
                    int caretX = static_cast<int>(custom_font_text_width(shown, fontHeight) * pageTextScale + 1);
                    int caretY = lineTop - top;
                    caretPosition(frame.pageIndex, i, displayHeight, textWidth, caretX, caretY);
                    int caretWidth = panelHeight / 400;
                    if (caretWidth < 1) caretWidth = 1;
                    if (frame.caretVisible) drawSurface(textX + caretX, top + caretY, caretWidth,
                                displayHeight, screenHeight, menu_style::caret, 0.0f);
                }
                if (last) break;
                ++text;
            }
        }
    }
    pageTextHeight = pageTextWidth = 0;
    pageTextScale = 1.0f;
}
}

float custom_menu_main_button_radius(int screenWidth, int screenHeight) {
    int fittedHeight = static_cast<int>(referenceHeight * menuScaleForScreen(screenWidth, screenHeight));
    return fittedHeight * menu_style::mainButtonOutlineRadiusPercent / 100.0f;
}

void custom_menu_render() {
    if (!custom_menu_is_visible()) return;
    if (!rendererLoaded) {
        if (!loadRendererApi()) return;
        rendererLoaded = true;
    }
    if (getContext() == EGL_NO_CONTEXT) return;

    EGLint screenWidth, screenHeight;
    EGLDisplay display = getDisplay();
    EGLSurface surface = getSurface(EGL_DRAW);
    if (surface == EGL_NO_SURFACE
        || !querySurface(display, surface, EGL_WIDTH, &screenWidth)
        || !querySurface(display, surface, EGL_HEIGHT, &screenHeight)
        || screenWidth <= 0 || screenHeight <= 0) return;

    MenuFrame frame;
    const char* headerTitle = nullptr;
    {
        UiLock lock;
        updateLayout(screenWidth, screenHeight);
        if (!updateAnimation(fps_limiter_frame_timestamp_ns())) return;
        updateSettingsTransition(fps_limiter_frame_timestamp_ns());
        frame.transition = settingsTransition;
        frame.pageIndex = frame.transition.active && frame.transition.exitPage >= 0
            ? frame.transition.exitPage : activePage;
        frame.focused = focusedTextBox;
        frame.caretVisible = (fps_limiter_frame_timestamp_ns() - caretStartNs) / 500000000LL % 2 == 0;
        frame.cursorX = cursorX;
        frame.cursorY = cursorY;
        frame.cursorValid = cursorPositionValid;
        frame.error = builderError;
        if (activePage >= 0 && activePage < static_cast<int>(pageCount))
            headerTitle = pages[activePage].title;
        frame.tileFirst = 0;
        frame.tileEnd = tileCount;
        for (int i = frame.tileFirst; i < frame.tileEnd; ++i) frame.visibleTiles[i - frame.tileFirst] = tiles[i];
        if (frame.pageIndex >= 0 && frame.pageIndex < static_cast<int>(pageCount)) {
            frame.page = pages[frame.pageIndex];
            frame.binding = bindingItem;
            frame.requestedControlView = frame.transition.exitPage >= 0
                ? frame.transition.exitView : controlView;
        }
    }
    if (frame.pageIndex >= 0) {
        frame.controls = layoutControls(frame.page, frame.pageIndex, frame.requestedControlView);
        UiLock lock;
        drawnControls = frame.controls;
        if (activePage == frame.pageIndex && controlView == frame.requestedControlView) {
            controlView = frame.controls.view;
            if (focusedTextBox >= 0 && frame.controls.rows[focusedTextBox] < 0) focusedTextBox = -1;
            if (bindingItem >= 0 && frame.controls.rows[bindingItem] < 0) bindingItem = -1;
            if (draggingSlider >= 0 && frame.controls.rows[draggingSlider] < 0) draggingSlider = -1;
        }
    }
    {
        UiLock lock;
        if (!(frame.transition.active && frame.transition.exitPage >= 0))
            updateScroll(fps_limiter_frame_timestamp_ns(), frame.pageIndex,
                     frame.pageIndex < 0 ? tileOffset / gridColumns : frame.controls.view);
    }
    if (frame.cursorValid && custom_menu_captures_input()
        && !(frame.transition.active && frame.transition.exitPage >= 0)) {
        int contentTop = screenHeight - panelY - panelHeight
            + panelHeight * menu_style::contentTopPercent / 100;
        if (contains(frame.cursorX, frame.cursorY, panelX, contentTop, panelWidth, panelHeight * 70 / 100)) {
            if (frame.pageIndex < 0) {
                for (int i = frame.tileFirst; i < frame.tileEnd; ++i) {
                    int x, top, width, height;
                    tileRect(i, screenHeight, x, top, width, height);
                    if (contains(frame.cursorX, frame.cursorY, x, top, width, height)) {
                        frame.hovered = i;
                        break;
                    }
                }
            } else {
                for (int row = 0; row < frame.controls.count; ++row) {
                    int i = frame.controls.indices[row];
                    PageItemType type = frame.page.items[i].type;
                    int x, top, width, height;
                    pageItemRect(i, screenHeight, x, top, width, height);
                    if ((type == pageButton || type == pageToggle)
                        && contains(frame.cursorX, frame.cursorY, x, top, width, height)) {
                        frame.hovered = 100 + i;
                        break;
                    }
                    if (type == pageChoice) for (int option = 0; option < 2; ++option) {
                        int optionX = x + width * (32 + option * 34) / 100;
                        int optionTop = top + height / 5;
                        int optionWidth = width * 32 / 100;
                        int optionHeight = height * 3 / 5;
                        if (contains(frame.cursorX, frame.cursorY, optionX, optionTop,
                                     optionWidth, optionHeight)) {
                            frame.hovered = 200 + i * 2 + option;
                            break;
                        }
                    }
                    if (frame.hovered >= 0) break;
                }
            }
        }
    }
    custom_font_set_opacity(menuOpacity);
    int panelTop = screenHeight - panelY - panelHeight;
    const int savedX = panelX, savedY = panelY, savedWidth = panelWidth, savedHeight = panelHeight;
    const float savedOpacity = menuOpacity;
    float radius = panelHeight * menu_style::panelRadiusPercent / 100.0f;
    float borderRadius = panelHeight * menu_style::panelOutlineRadiusPercent / 100.0f;
    GLint oldScissor[4];
    menuGetIntegerv(GL_SCISSOR_BOX, oldScissor);
    bool oldScissorEnabled = menuIsEnabled(GL_SCISSOR_TEST);
    int titleHeight = panelHeight * 14 / 100;
    menuEnable(GL_SCISSOR_TEST);
    clippingContent = true;
    menuScissor(panelX, panelY, panelWidth, panelHeight);
    drawSurface(panelX, panelTop, panelWidth, panelHeight, screenHeight,
                menu_style::contentBackground, radius, 0.0f, menu_style::contentTint, 6.0f);
    menuScissor(panelX, screenHeight - panelTop - titleHeight, panelWidth, titleHeight);
    drawSurface(panelX, panelTop, panelWidth, panelHeight, screenHeight,
                menu_style::background, radius, 0.0f, menu_style::titleTint);
    clippingContent = false;
    menuScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
    if (!oldScissorEnabled) menuDisable(GL_SCISSOR_TEST);
    if (menu_style::panelOutlineThickness > 0.0f)
        drawSurface(panelX, panelTop, panelWidth, panelHeight, screenHeight,
                    menu_style::panelOutline, borderRadius, menu_style::panelOutlineThickness);
    drawHeader(headerTitle, screenWidth, screenHeight);
    float textScale = 1.0f;
    if (frame.transition.active) {
        float scale = frame.transition.exitPage >= 0
            ? settingsExitScale(frame.transition.progress)
            : settingsScale(frame.transition.progress);
        panelWidth = static_cast<int>(savedWidth * scale);
        panelHeight = static_cast<int>(savedHeight * scale);
        panelX = savedX + (savedWidth - panelWidth) / 2;
        panelY = savedY + (savedHeight - panelHeight) / 2;
        panelTop = screenHeight - panelY - panelHeight;
        textScale = scale;
        menuOpacity *= frame.transition.exitPage >= 0
            ? 1.0f - frame.transition.progress : frame.transition.progress;
        custom_font_set_opacity(menuOpacity);
    }
    int contentTop = panelTop + panelHeight * menu_style::contentTopPercent / 100;
    int contentHeight = panelHeight * 70 / 100;
    menuEnable(GL_SCISSOR_TEST);
    menuScissor(panelX, screenHeight - contentTop - contentHeight, panelWidth, contentHeight);
    clippingContent = true;
    custom_font_set_clip(true);
    if (frame.pageIndex < 0) drawTiles(frame, screenWidth, screenHeight);
    else if (menuOpacity > 0.0f) drawPageControls(frame, screenWidth, screenHeight, savedHeight, savedWidth, textScale);
    clippingContent = false;
    custom_font_set_clip(false);
    menuScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
    if (!oldScissorEnabled) menuDisable(GL_SCISSOR_TEST);
    panelX = savedX;
    panelY = savedY;
    panelWidth = savedWidth;
    panelHeight = savedHeight;
    custom_font_set_raster_scale(1.0f);
    drawFooter(frame, screenWidth, screenHeight);
    menuOpacity = savedOpacity;
    custom_font_set_opacity(menuOpacity);
    if (frame.transition.active && frame.transition.exitPage >= 0) {
        menuOpacity = savedOpacity * settingsExitGridOpacity(frame.transition.progress);
        custom_font_set_opacity(menuOpacity);
        int gridTop = screenHeight - savedY - savedHeight
            + savedHeight * menu_style::contentTopPercent / 100;
        int gridHeight = savedHeight * 70 / 100;
        menuEnable(GL_SCISSOR_TEST);
        menuScissor(savedX, screenHeight - gridTop - gridHeight, savedWidth, gridHeight);
        clippingContent = true;
        custom_font_set_clip(true);
        float savedScrollPosition = scrollPosition;
        scrollPosition = static_cast<float>(tileOffset / gridColumns);
        drawTiles(frame, screenWidth, screenHeight);
        scrollPosition = savedScrollPosition;
        clippingContent = false;
        custom_font_set_clip(false);
        menuScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
        if (!oldScissorEnabled) menuDisable(GL_SCISSOR_TEST);
        MenuFrame tilesFrame = frame;
        tilesFrame.pageIndex = -1;
        drawFooter(tilesFrame, screenWidth, screenHeight);
        menuOpacity = savedOpacity;
        custom_font_set_opacity(menuOpacity);
    }
}
