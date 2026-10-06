#include "ui_animation.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "launcher_api.h"
#include "tablist.h"
#include "auto_gg.h"
#include "flarial_presence.h"
#include "client_modules.h"
#include "client_settings.h"
#include "custom_font.h"
#include "custom_menu.h"
#include "menu_style.h"
#include "minecraft_build.h"
#include "hook_manager.h"
#include "skin_image.h"

namespace {
namespace profile = minecraft_build::current::tablist;
namespace chat = minecraft_build::current::chat;
using Dispatch = void (*)(void*, const void*, void*, void*);
Dispatch originals[4];
unsigned long gameBase, handlerIdentity;
bool installed, enabled = true, held, consumed;
const char* error = "Tablist: initializing";
unsigned char rosterLock;
// ponytail: bounded roster (4096) and linear UUID lookup; allocate/index entries
// if a real server exceeds this ceiling. Only copied names and owned pixels survive dispatch.
constexpr int capacity = 4096, visibleCapacity = 24;
struct Player {
    unsigned long uuid[2]{};
    char name[64]{};
    bool hasHead = false;
    unsigned char head[16 * 16 * 4]{};
    unsigned char* skin = nullptr;
    unsigned int skinWidth = 0, skinHeight = 0;
    unsigned long revision = 0, used = 0;
};
Player players[capacity], framePlayers[visibleCapacity];
bool frameFlarial[visibleCapacity];
int playerCount, columnOffset, selection = -1;
unsigned long selectionUuid[2]; // Protected by rosterLock; selection is its displayed rank.
long long rosterFrameNs;
bool rightDown, rightConsumed;
unsigned long skinBytes, skinRevision, skinUse;
// ponytail: full textures capped at 64 MiB; evict least recently used images
// if enormous high-resolution rosters exceed it. Names and heads remain available.
constexpr unsigned long skinBudget = 64 * 1024 * 1024;
unsigned char frameSkin[skin_image_max_bytes];
unsigned int frameSkinWidth, frameSkinHeight;
unsigned long frameSkinRevision;
Player selectedPlayer;
long long holdStart, resultStart;
bool holdCompleted, saveAccepted;
unsigned long holdUuid[2], holdRevision, rightPressSerial, holdPressSerial;
void cancelSelectionLocked() {
    __atomic_store_n(&selection, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&rightDown, false, __ATOMIC_RELAXED);
}
struct Lock {
    Lock() { while (__atomic_test_and_set(&rosterLock, __ATOMIC_ACQUIRE)) {} }
    ~Lock() { __atomic_clear(&rosterLock, __ATOMIC_RELEASE); }
};
void cancelSelection() { Lock lock; cancelSelectionLocked(); }

template<class T> T read(const void* p, unsigned long offset) {
    T result{};
    const auto* bytes = static_cast<const unsigned char*>(p) + offset;
    auto* out = reinterpret_cast<unsigned char*>(&result);
    for (unsigned long i = 0; i < sizeof(T); ++i) out[i] = bytes[i];
    return result;
}
bool readable(const void* p, unsigned long offset, unsigned long size) {
    return p && hooks::readable(reinterpret_cast<unsigned long>(p), offset, size);
}
int compare(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
int find(const unsigned long* uuid) {
    for (int i = 0; i < playerCount; ++i)
        if (players[i].uuid[0] == uuid[0] && players[i].uuid[1] == uuid[1]) return i;
    return -1;
}
void orderRoster(bool* flarial, int* order, long long now) {
    flarial_presence_match_many(reinterpret_cast<const char*>(players), sizeof(Player),
                                 __builtin_offsetof(Player, name), playerCount, flarial, now);
    int rank = 0;
    for (int group = 1; group >= 0; --group) for (int i = 0; i < playerCount; ++i)
        if (flarial[i] == static_cast<bool>(group)) order[rank++] = i;
}
int selectionRank(const int* order) {
    if (__atomic_load_n(&selection,__ATOMIC_RELAXED) < 0) return -1;
    for (int rank = 0; rank < playerCount; ++rank) {
        const Player& player = players[order[rank]];
        if (player.uuid[0] == selectionUuid[0] && player.uuid[1] == selectionUuid[1]) return rank;
    }
    cancelSelectionLocked(); return -1;
}
void releaseSkin(Player& player) {
    if (player.skin) {
        skinBytes -= player.skinWidth * player.skinHeight * 4ul;
        skin_image_release(player.skin); player.skin = nullptr;
    }
}
void retainSkin(Player& player) {
    if (!player.skin) return;
    unsigned long bytes = player.skinWidth * player.skinHeight * 4ul;
    while (skinBytes + bytes > skinBudget) {
        int oldest = -1;
        for (int i = 0; i < playerCount; ++i)
            if (players[i].skin && (oldest < 0 || players[i].used < players[oldest].used)) oldest = i;
        if (oldest < 0) break;
        releaseSkin(players[oldest]);
    }
    skinBytes += bytes; player.revision = ++skinRevision; player.used = ++skinUse;
}
void clear() {
    for (int i = 0; i < playerCount; ++i) releaseSkin(players[i]);
    cancelSelectionLocked();
    playerCount = 0;
    __atomic_store_n(&columnOffset, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&held, false, __ATOMIC_RELAXED);
}
void nameFromString(const void* nativeString, char* out) {
    if (!readable(nativeString, 0, 24)) return;
    unsigned char tag = read<unsigned char>(nativeString, 0);
    unsigned long size = tag & 1 ? read<unsigned long>(nativeString, 8) : tag >> 1;
    const auto* text = tag & 1 ? read<const unsigned char*>(nativeString, 16)
                             : static_cast<const unsigned char*>(nativeString) + 1;
    if (!(tag & 1) && size > 22) return;
    if (size > 256) size = 256;
    if (!readable(text, 0, size)) return;
    int n = 0;
    for (unsigned long i = 0; i < size && n < 60; ++i) {
        unsigned char c = text[i];
        if (c == 0xc2 && i + 2 < size && text[i + 1] == 0xa7) { i += 2; continue; }
        if (c == 0xa7 && i + 1 < size) { ++i; continue; }
        if (c < 32 || c == 127) continue;
        if (c >= 128) {
            out[n++] = '?';
            while (i + 1 < size && (text[i + 1] & 0xc0) == 0x80) ++i;
        } else out[n++] = static_cast<char>(c);
    }
    out[n] = 0;
}
bool skinPixels(const void* skin, const unsigned char*& pixels, unsigned int& width, unsigned int& height) {
    if (!readable(skin, 0, profile::skinBytes + 8)) return false;
    width = read<unsigned int>(skin, profile::skinWidth);
    height = read<unsigned int>(skin, profile::skinHeight);
    if (read<int>(skin, profile::skinFormat) != profile::skinRgbaFormat
        || (width != 64 && width != 128 && width != 256)
        || (height != width && height != width / 2)) return false;
    pixels = read<const unsigned char*>(skin, profile::skinPixels);
    unsigned long bytes = width * height * 4;
    return read<unsigned long>(skin, profile::skinBytes) == bytes && readable(pixels, 0, bytes);
}
bool skinHead(const void* skin, unsigned char* face) {
    const unsigned char* pixels; unsigned int width, height;
    if (!skinPixels(skin,pixels,width,height)) return false;
    bool visible = false;
    for (unsigned int y = 0; y < 16; ++y) for (unsigned int x = 0; x < 16; ++x) {
        unsigned int sy = width / 8 + y * width / 128;
        unsigned int sx = width / 8 + x * width / 128;
        const auto* base = pixels + (sy * width + sx) * 4;
        const auto* hat = pixels + (sy * width + sx + width / 2) * 4;
        auto* out = face + (y * 16 + x) * 4;
        unsigned int alpha = hat[3] * 255 + base[3] * (255 - hat[3]);
        for (int c = 0; c < 3; ++c)
            out[c] = alpha ? (hat[c] * hat[3] * 255 + base[c] * base[3] * (255 - hat[3])) / alpha : 0;
        out[3] = (alpha + 127) / 255;
        visible = visible || out[3] != 0;
    }
    return visible;
}
void copySkin(const void* native, Player& player) {
    const unsigned char* pixels;
    if (!skinPixels(native,pixels,player.skinWidth,player.skinHeight)) return;
    unsigned long bytes = player.skinWidth * player.skinHeight * 4ul;
    player.skin = skin_image_allocate(bytes);
    if (player.skin) {
        for (unsigned long i = 0; i < bytes; ++i) player.skin[i] = pixels[i];
    }
}
void rosterPacket(const void* packet, void* handler) {
    if (!readable(packet, profile::rosterVector, 24)) return;
    unsigned long begin = read<unsigned long>(packet, profile::rosterVector);
    unsigned long end = read<unsigned long>(packet, profile::rosterVector + 8);
    if (end < begin || (end - begin) % profile::rosterEntrySize) return;
    bool tooMany = end - begin > capacity * profile::rosterEntrySize;
    if (tooMany) end = begin + capacity * profile::rosterEntrySize;
    if (end != begin && !hooks::readable(begin, 0, end - begin)) return;
    for (unsigned long pos = begin; pos < end; pos += profile::rosterEntrySize) {
        const auto* entry = reinterpret_cast<const unsigned char*>(pos);
        unsigned int kind = read<unsigned int>(entry, profile::rosterVariant);
        if (kind > 1) continue;
        Player value;
        value.uuid[0] = read<unsigned long>(entry, profile::rosterUuid);
        value.uuid[1] = read<unsigned long>(entry, profile::rosterUuid + 8);
        if (kind == 1) {
            nameFromString(entry + profile::rosterName, value.name);
            if (!value.name[0]) continue;
            const void* skin = read<const void*>(entry, profile::rosterSkin);
            value.hasHead = skinHead(skin, value.head); copySkin(skin, value);
        }
        bool joined = false;
        {
            Lock lock;
            int index = find(value.uuid);
            bool alreadyPresent = index >= 0;
            if (kind == 0 && value.uuid[0] == selectionUuid[0] && value.uuid[1] == selectionUuid[1]) {
                bool flarial[capacity]; int order[capacity];
                orderRoster(flarial,order,rosterFrameNs);
                int rank = selectionRank(order);
                if (rank >= 0 && playerCount > 1) {
                    int adjacent = rank+1 < playerCount ? rank+1 : rank-1;
                    selectionUuid[0] = players[order[adjacent]].uuid[0];
                    selectionUuid[1] = players[order[adjacent]].uuid[1];
                    __atomic_store_n(&selection,adjacent,__ATOMIC_RELAXED);
                    __atomic_store_n(&rightDown,false,__ATOMIC_RELAXED);
                } else cancelSelectionLocked();
            }
            if (index >= 0) {
                releaseSkin(players[index]);
                for (int i = index; i + 1 < playerCount; ++i) players[i] = players[i + 1];
                --playerCount;
            }
            if (kind == 1 && playerCount < capacity) {
                retainSkin(value);
                int insert = playerCount;
                while (insert > 0 && compare(players[insert - 1].name, value.name) > 0) {
                    players[insert] = players[insert - 1]; --insert;
                }
                players[insert] = value; ++playerCount;
                joined = !alreadyPresent;
            } else skin_image_release(value.skin);
        }
        if (joined) auto_gg_lobby_player(handler, value.name);
    }
}
void skinPacket(const void* packet) {
    if (!readable(packet, profile::skinPacketUuid, 24)) return;
    unsigned long uuid[] = {read<unsigned long>(packet, profile::skinPacketUuid),
                            read<unsigned long>(packet, profile::skinPacketUuid + 8)};
    Player value;
    const void* skin = read<const void*>(packet, profile::skinPacketSkin);
    value.hasHead = skinHead(skin, value.head); copySkin(skin, value);
    Lock lock;
    int index = find(uuid);
    if (index >= 0) {
        releaseSkin(players[index]); retainSkin(value);
        players[index].hasHead = value.hasHead;
        for (unsigned long i = 0; i < sizeof(value.head); ++i) players[index].head[i] = value.head[i];
        players[index].skin = value.skin; players[index].skinWidth = value.skinWidth;
        players[index].skinHeight = value.skinHeight; players[index].revision = value.revision;
        players[index].used = value.used;
    } else skin_image_release(value.skin);
}
void observe(int kind, void* handler, void* sharedPacket) {
    if (!__atomic_load_n(&installed, __ATOMIC_ACQUIRE) || !readable(handler, 0, 8)) return;
    unsigned long vtable = read<unsigned long>(handler, 0);
    if (vtable != gameBase + chat::handlerVtable && vtable != gameBase + chat::legacyHandlerVtable) return;
    bool reset;
    {
        Lock lock;
        auto identity = reinterpret_cast<unsigned long>(handler);
        reset = handlerIdentity != identity || kind >= 2;
        if (reset) clear();
        handlerIdentity = identity;
    }
    if (reset) auto_gg_world_reset();
    if (kind >= 2 || !readable(sharedPacket, 0, 8)) return;
    auto_gg_lobby_dispatch(handler);
    const void* packet = read<const void*>(sharedPacket, 0);
    if (kind == 0) rosterPacket(packet, handler); else skinPacket(packet);
}
template<int Kind> void dispatch(void* dispatcher, const void* network, void* handler, void* packet) {
    observe(Kind, handler, packet);
    originals[Kind](dispatcher, network, handler, packet);
}
bool install(unsigned long base) {
    if (!base || !hooks::supported(base)) return false;
    for (int i = 0; i < 4; ++i) {
        unsigned char signature[] = {0x48,0x89,0xd7,0x48,0x8b,0x11,0x48,0x8b,0x07,
                                     0x48,0x8b,0x80,0,0,0,0,0xff,0xe0};
        unsigned long slot = profile::rosterHandlerSlots[i];
        for (int j = 0; j < 4; ++j) signature[12 + j] = (slot >> (j * 8)) & 255;
        if (!hooks::readable(base, profile::rosterFunctions[i], sizeof(signature), true)
            || !hooks::matches(base, profile::rosterFunctions[i], signature, sizeof(signature))
            || !hooks::matches_pointer(base, profile::rosterSlots[i], profile::rosterFunctions[i])
            || !hooks::matches_pointer(base, chat::handlerVtable + slot, profile::rosterBaseHandlers[i])
            || !hooks::matches_pointer(base, chat::legacyHandlerVtable + slot, profile::rosterLegacyHandlers[i])) return false;
    }
#define CHECK(name) if (!hooks::matches(base, profile::name##Site, profile::name##Signature, sizeof(profile::name##Signature))) return false
    CHECK(rosterAddLayout); CHECK(rosterRemoveLayout); CHECK(rosterVectorLayout); CHECK(skinPacketLayout);
    CHECK(rosterVariantLayout); CHECK(skinImageFormat); CHECK(skinImageData); CHECK(skinImageWidth); CHECK(skinImageHeight); CHECK(skinImageInit);
#undef CHECK
    Dispatch callbacks[] = {dispatch<0>,dispatch<1>,dispatch<2>,dispatch<3>};
    hooks::Patch patches[4];
    gameBase = base;
    for (int i = 0; i < 4; ++i) {
        originals[i] = reinterpret_cast<Dispatch>(base + profile::rosterFunctions[i]);
        patches[i] = {profile::rosterSlots[i],base + profile::rosterFunctions[i],reinterpret_cast<unsigned long>(callbacks[i])};
    }
    return hooks::install("Tablist", base, patches, 4) == hooks::InstallResult::Installed;
}
decltype(&eglGetCurrentDisplay) getDisplay;
decltype(&eglGetCurrentSurface) getSurface;
decltype(&eglQuerySurface) querySurface;
float (*exp2)(float);
bool graphicsLoaded;
decltype(&glGetIntegerv) glGet;
decltype(&glIsEnabled) glEnabled;
decltype(&glEnable) glEnableCap;
decltype(&glDisable) glDisableCap;
decltype(&glScissor) glClip;
float panelWidth, panelHeight, widthFrom, heightFrom;
int widthTarget, heightTarget, layoutScreenW, layoutScreenH;
long long sizeStart;
float scrollbarPosition, scrollbarFrom;
int scrollbarTarget;
long long scrollbarStart;
float animateScrollbar(int target, int maximum, long long now) {
    float t = static_cast<float>(now-scrollbarStart)/menu_style::scrollDurationNs;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float ease = ui_animation::ease_out_quart(t);
    scrollbarPosition = scrollbarFrom+(scrollbarTarget-scrollbarFrom)*ease;
    if (scrollbarTarget != target) {
        scrollbarFrom = scrollbarPosition; scrollbarTarget = target; scrollbarStart = now;
    }
    if (scrollbarPosition < 0) scrollbarPosition = 0;
    if (scrollbarPosition > maximum) scrollbarPosition = maximum;
    return scrollbarPosition;
}
void resizePanel(int width, int height, long long now) {
    if (!widthTarget) {
        panelWidth = widthFrom = widthTarget = width;
        panelHeight = heightFrom = heightTarget = height;
    }
    float t = static_cast<float>(now - sizeStart) / menu_style::animationDurationNs;
    float ease = ui_animation::ease_out_exponential(t, exp2);
    panelWidth = widthFrom + (widthTarget-widthFrom)*ease;
    panelHeight = heightFrom + (heightTarget-heightFrom)*ease;
    if (widthTarget != width || heightTarget != height) {
        widthFrom = panelWidth; heightFrom = panelHeight; sizeStart = now;
        widthTarget = width; heightTarget = height;
    }
}
float opacity, animationFrom;
bool animationTarget;
long long animationStart;
float animate(bool target, long long now) {
    if (target != animationTarget) {
        animationFrom = opacity; animationStart = now; animationTarget = target;
    }
    long long elapsed = now >= animationStart ? now - animationStart : 0;
    float t = static_cast<float>(elapsed) / menu_style::animationDurationNs;
    float ease = ui_animation::ease_out_exponential(t, exp2);
    opacity = animationFrom + ((target ? 1.0f : 0.0f) - animationFrom) * ease;
    return opacity;
}
void number(char* out, int n) {
    char reverse[12]; int count = 0;
    do { reverse[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n);
    for (int i = 0; i < count; ++i) out[i] = reverse[count - i - 1];
    out[count] = 0;
}
void append(char* out, const char* text) {
    while (*out) ++out;
    while (*text) *out++ = *text++;
    *out = 0;
}
void panel(int x, int top, int w, int h, int screenH, MenuColor color, float alpha, float radius) {
    PanelPaint paint; paint.inheritScissor = true; paint.tint = color; paint.opacity = alpha; paint.cornerRadius = radius;
    draw_gl_panel(x, screenH - top - h, w, h, paint);
}
}

void tablist_init() {
    skin_image_init();
    __atomic_store_n(&enabled, client_settings_get_tablist(), __ATOMIC_RELAXED);
    if (!hooks::initialize() || !install(hooks::find_game())) {
        error = "Tablist: unsupported Minecraft build"; return;
    }
    __atomic_store_n(&installed, true, __ATOMIC_RELEASE); error = nullptr;
    flarial_presence_init();
}
const char* tablist_error() { return error; }
void client_set_tablist(bool value) {
    __atomic_store_n(&enabled, value, __ATOMIC_RELAXED);
    if (!value) { __atomic_store_n(&held, false, __ATOMIC_RELAXED); cancelSelection(); }
    client_settings_set_tablist(value);
}
bool client_tablist_enabled() { return __atomic_load_n(&enabled, __ATOMIC_RELAXED); }
bool tablist_on_keyboard(int key, int action, bool gameplay) {
    if (key != 9) return false;
    if (action == 2) {
        __atomic_store_n(&held, false, __ATOMIC_RELAXED); cancelSelection();
        bool result = consumed; consumed = false; return result;
    }
    if (!gameplay || !client_tablist_enabled() || !__atomic_load_n(&installed, __ATOMIC_ACQUIRE)) return consumed;
    if (action == 0) {
        cancelSelection();
        __atomic_store_n(&columnOffset, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&held, true, __ATOMIC_RELAXED); consumed = true;
    }
    return consumed;
}
bool tablist_on_scroll(double dy) {
    if (!__atomic_load_n(&held, __ATOMIC_RELAXED) || !client_tablist_enabled()) return false;
    if (dy > 0 || dy < 0) {
        Lock lock;
        if (!playerCount || !__atomic_load_n(&held,__ATOMIC_RELAXED) || !client_tablist_enabled()) return true;
        bool flarial[capacity]; int order[capacity];
        orderRoster(flarial,order,rosterFrameNs);
        int old = selectionRank(order);
        int next = old < 0 ? 0 : old + (dy > 0 ? -1 : 1);
        if (next < 0) next = 0;
        if (next >= playerCount) next = playerCount-1;
        selectionUuid[0] = players[order[next]].uuid[0];
        selectionUuid[1] = players[order[next]].uuid[1];
        __atomic_store_n(&selection, next, __ATOMIC_RELAXED);
        __atomic_store_n(&rightDown, false, __ATOMIC_RELAXED);
    }
    return true;
}
bool tablist_on_mouse_button(int button, int action, bool gameplay) {
    if (button != 2) return false;
    if (action != 0) {
        __atomic_store_n(&rightDown, false, __ATOMIC_RELAXED);
        return __atomic_exchange_n(&rightConsumed, false, __ATOMIC_RELAXED);
    }
    if (!gameplay || !client_tablist_enabled() || !__atomic_load_n(&held, __ATOMIC_RELAXED)
        || __atomic_load_n(&selection, __ATOMIC_RELAXED) < 0) return false;
    __atomic_store_n(&rightConsumed, true, __ATOMIC_RELAXED);
    __atomic_add_fetch(&rightPressSerial,1ul,__ATOMIC_RELAXED);
    __atomic_store_n(&rightDown, true, __ATOMIC_RELEASE);
    return true;
}
void tablist_render(bool gameplay, long long frameNs) {
    if (!gameplay) { __atomic_store_n(&held, false, __ATOMIC_RELAXED); cancelSelection(); }
    bool target = gameplay && client_tablist_enabled() && __atomic_load_n(&held, __ATOMIC_RELAXED);
    if (!graphicsLoaded) {
        if (!target) return;
        void* egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
        void* math = mcpelauncher_host_dlopen("libm.so.6", 2);
        if (!egl || !math) return;
#define LOAD(dst, lib, name) dst = reinterpret_cast<decltype(dst)>(mcpelauncher_host_dlsym(lib, name)); if (!dst) return
        LOAD(getDisplay, egl, "eglGetCurrentDisplay"); LOAD(getSurface, egl, "eglGetCurrentSurface");
        LOAD(querySurface, egl, "eglQuerySurface"); LOAD(exp2, math, "exp2f");
#undef LOAD
        auto proc = reinterpret_cast<decltype(&eglGetProcAddress)>(mcpelauncher_host_dlsym(egl,"eglGetProcAddress"));
        if (!proc) return;
        glGet = reinterpret_cast<decltype(glGet)>(proc("glGetIntegerv"));
        glEnabled = reinterpret_cast<decltype(glEnabled)>(proc("glIsEnabled"));
        glEnableCap = reinterpret_cast<decltype(glEnableCap)>(proc("glEnable"));
        glDisableCap = reinterpret_cast<decltype(glDisableCap)>(proc("glDisable"));
        glClip = reinterpret_cast<decltype(glClip)>(proc("glScissor"));
        if (!glGet || !glEnabled || !glEnableCap || !glDisableCap || !glClip) return;
        graphicsLoaded = true;
    }
    if (target) flarial_presence_refresh(frameNs);
    float alpha = animate(target, frameNs);
    if (alpha <= 0.001f) { widthTarget = 0; holdStart = resultStart = 0; return; }
    EGLDisplay display = getDisplay(); EGLSurface surface = getSurface(EGL_DRAW);
    EGLint screenW = 0, screenH = 0;
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE
        || !querySurface(display, surface, EGL_WIDTH, &screenW)
        || !querySurface(display, surface, EGL_HEIGHT, &screenH) || screenW < 240 || screenH < 180) return;
    custom_font_set_mojangles(client_settings_get_tablist_mojangles());
    int font = screenH / 50; if (font < 12) font = 12; if (font > 24) font = 24;
    int padding = font, rowH = font * 2, head = rowH - font / 2;
    int maxColumns = screenW >= font * 48 ? 3 : screenW >= font * 32 ? 2 : 1;
    int maxRows = (screenH * 3 / 4 - padding * 4) / rowH;
    if (maxRows < 1) maxRows = 1;
    if (maxRows > 8) maxRows = 8;
    int selected, total, count, firstVisible, totalColumns;
    bool preview;
    bool flarialPlayers[capacity]; int order[capacity];
    frameSkinWidth = frameSkinHeight = 0;
    {
        Lock lock;
        total = playerCount; rosterFrameNs = frameNs;
        orderRoster(flarialPlayers,order,frameNs);
        selected = selectionRank(order);
        __atomic_store_n(&selection,selected,__ATOMIC_RELAXED);
        preview = selected >= 0;
        int nameColumns = preview ? (maxColumns > 2 ? maxColumns-1 : 1) : maxColumns;
        totalColumns = (total+maxRows-1)/maxRows;
        int maxOffset = totalColumns-nameColumns; if (maxOffset < 0) maxOffset = 0;
        int offset = preview ? __atomic_load_n(&columnOffset,__ATOMIC_RELAXED) : 0;
        int selectedColumn = selected >= 0 ? selected/maxRows : 0;
        if (selectedColumn < offset) offset = selectedColumn;
        if (selectedColumn >= offset+nameColumns) offset = selectedColumn-nameColumns+1;
        if (offset > maxOffset) offset = maxOffset;
        if (offset < 0) offset = 0;
        __atomic_store_n(&columnOffset,offset,__ATOMIC_RELAXED);
        firstVisible = offset*maxRows;
        count = total-firstVisible; if (count > maxRows*nameColumns) count = maxRows*nameColumns;
        for (int i = 0; i < count; ++i) {
            framePlayers[i] = players[order[firstVisible+i]];
            frameFlarial[i] = flarialPlayers[order[firstVisible+i]];
        }
        if (preview) {
            Player& player = players[order[selected]];
            selectedPlayer = player;
            if (player.skin) {
                player.used = ++skinUse;
                frameSkinWidth = player.skinWidth; frameSkinHeight = player.skinHeight;
                frameSkinRevision = player.revision;
                unsigned long bytes = frameSkinWidth * frameSkinHeight * 4ul;
                for (unsigned long j = 0; j < bytes; ++j) frameSkin[j] = player.skin[j];
            }
        }
    }
    int rows = count < maxRows ? (count ? count : 1) : maxRows;
    int nameColumns = count ? (count+rows-1)/rows : 1;
    int columns = nameColumns + (preview ? 1 : 0);
    int displayRows = rows;
    int previewRows = maxRows < 4 ? maxRows : 4;
    if (preview && displayRows < previewRows) displayRows = previewRows;
    int targetWidth = columns * font * 14 + padding * 2;
    if (targetWidth > screenW * 9 / 10) targetWidth = screenW * 9 / 10;
    int targetHeight = displayRows * rowH + padding * 3;
    if (layoutScreenW != screenW || layoutScreenH != screenH) {
        widthTarget = 0; layoutScreenW = screenW; layoutScreenH = screenH;
    }
    resizePanel(targetWidth,targetHeight,frameNs);
    int width = static_cast<int>(panelWidth+0.5f), height = static_cast<int>(panelHeight+0.5f);
    int x = (screenW - width) / 2;
    int top = screenH / 12 - static_cast<int>(height * 0.1f * (1 - alpha));
    float cornerRadius = custom_menu_main_button_radius(screenW, screenH);
    PanelPaint background;
    background.tint = menu_style::tablistBackground;
    background.tintAmount = menu_style::tablistBackgroundTint;
    background.opacity = alpha * menu_style::tablistBackgroundOpacity;
    background.cornerRadius = cornerRadius;
    background.blurScale = menu_style::tablistBlurScale;
    background.blurTimestampNs = frameNs;
    draw_gl_panel(x, screenH - top - height, width, height, background);
    PanelPaint border; border.tint = menu_style::tablistOutline;
    border.opacity = alpha * menu_style::tablistOutlineOpacity;
    border.cornerRadius = cornerRadius;
    border.borderWidth = menu_style::tablistOutlineThickness;
    if (border.borderWidth > 0.0f)
        draw_gl_panel(x, screenH - top - height, width, height, border);
    custom_font_set_opacity(alpha * menu_style::tablistTextOpacity);
    char title[64] = "Players - ", digits[12]; number(digits, total); append(title, digits);
    custom_font_draw_left_color(title, (screenW - custom_font_text_width(title, font)) / 2,
                                top + padding, font, menu_style::tablistText.red,
                                menu_style::tablistText.green, menu_style::tablistText.blue, screenW, screenH);
    // Clip newly revealed content to the expanding panel without changing game scissor state.
    GLint oldScissor[4]; glGet(GL_SCISSOR_BOX,oldScissor);
    bool oldScissorEnabled = glEnabled(GL_SCISSOR_TEST);
    glEnableCap(GL_SCISSOR_TEST);
    glClip(x+padding-3,screenH-top-height+padding-1,width-padding*2+6,height-padding*3+3);
    custom_font_set_clip(true);
    int colW = (targetWidth - padding * 2) / columns;
    for (int i = 0; i < count; ++i) {
        const Player& player = framePlayers[i];
        int left = x + padding + (i / rows) * colW, y = top + padding * 2 + (i % rows) * rowH;
        if (preview && i == selected-firstVisible) {
            PanelPaint highlight; highlight.tint = {1,1,1}; highlight.opacity = alpha*0.25f;
            highlight.borderWidth = 1; highlight.cornerRadius = 2; highlight.inheritScissor = true;
            draw_gl_panel(left-2,screenH-y-rowH+1,colW-1,rowH-2,highlight);
        }
        int iconX = left + head / 2, iconY = y + (rowH - head) / 2;
        custom_font_set_opacity(alpha * menu_style::tablistHeadOpacity);
        if (!player.hasHead || !custom_font_draw_head(player.head, iconX, iconY, head, screenW, screenH)) {
            panel(left, iconY, head, head, screenH, menu_style::tablistHeadPlaceholder,
                  alpha * menu_style::tablistHeadPlaceholderOpacity, menu_style::tablistHeadPlaceholderRadius);
            char initial[] = {player.name[0],0};
            custom_font_set_opacity(alpha * menu_style::tablistTextOpacity);
            custom_font_draw_left_color(initial, iconX - custom_font_text_width(initial, font) / 2,
                iconY + (head - font) / 2, font, menu_style::tablistText.red,
                menu_style::tablistText.green, menu_style::tablistText.blue, screenW, screenH);
        }
        int nameX = left + head + 7;
        int nameMax = left + colW - nameX - 3;
        bool flarial = frameFlarial[i];
        int prefixWidth = flarial ? custom_font_text_width("[FL]", font) + 4 : 0;
        nameMax -= prefixWidth;
        char label[64]; int n = 0;
        while (n < 60 && player.name[n]) { label[n] = player.name[n]; ++n; } label[n] = 0;
        if (custom_font_text_width(label, font) > nameMax) {
            while (n > 0) {
                label[--n] = 0;
                char shortened[64]; int k = 0; while (label[k]) { shortened[k] = label[k]; ++k; }
                shortened[k++] = '.'; shortened[k++] = '.'; shortened[k++] = '.'; shortened[k] = 0;
                if (custom_font_text_width(shortened, font) <= nameMax) {
                    for (int j = 0; j <= k; ++j) label[j] = shortened[j];
                    break;
                }
            }
        }
        int textY = y + (rowH - font) / 2 - 1;
        if (flarial) {
            custom_font_set_opacity(alpha * menu_style::tablistBadgeOpacity);
            custom_font_draw_left_color("[FL]", nameX, textY, font,
                                        menu_style::tablistBadge.red, menu_style::tablistBadge.green,
                                        menu_style::tablistBadge.blue, screenW, screenH);
        }
        custom_font_set_opacity(alpha * menu_style::tablistTextOpacity);
        custom_font_draw_left_color(label, nameX + prefixWidth, textY, font,
            menu_style::tablistText.red, menu_style::tablistText.green,
            menu_style::tablistText.blue, screenW, screenH);
    }
    if (preview) {
        int center = x+padding+nameColumns*colW+colW/2;
        int previewTop = top+padding*2;
        int previewHeight = displayRows*rowH-font;
        if (previewHeight > (colW-8)*2) previewHeight = (colW-8)*2;
        custom_font_set_opacity(alpha);
        if (!frameSkinWidth || !custom_font_draw_skin(frameSkin,frameSkinWidth,frameSkinHeight,
                frameSkinRevision,center,previewTop,previewHeight,screenW,screenH)) {
            custom_font_draw(frameSkinWidth ? "Preview unavailable" : "Skin unavailable",
                             center,previewTop+previewHeight/2,font*3/4,screenW,screenH);
        }
        bool down = target && __atomic_load_n(&rightDown,__ATOMIC_ACQUIRE);
        unsigned long press = __atomic_load_n(&rightPressSerial,__ATOMIC_RELAXED);
        bool newPress = press != holdPressSerial; holdPressSerial = press;
        bool changed = holdUuid[0] != selectedPlayer.uuid[0] || holdUuid[1] != selectedPlayer.uuid[1]
            || holdRevision != selectedPlayer.revision;
        if (changed || newPress) resultStart = 0;
        if (!down || changed || newPress) {
            holdStart = 0; holdCompleted = false;
            holdUuid[0] = selectedPlayer.uuid[0]; holdUuid[1] = selectedPlayer.uuid[1];
            holdRevision = selectedPlayer.revision;
            if (down && changed && !newPress) { __atomic_store_n(&rightDown,false,__ATOMIC_RELAXED); down = false; }
        }
        if (down && !holdCompleted) {
            if (!holdStart) holdStart = frameNs;
            float progress = static_cast<float>(frameNs-holdStart)/1000000000.0f;
            custom_font_draw_ring(center,top+padding*2+displayRows*rowH-font/2,font*0.45f,progress,screenW,screenH);
            if (progress >= 1) {
                holdCompleted = true; resultStart = frameNs;
                saveAccepted = frameSkinWidth && skin_image_save(frameSkin,frameSkinWidth,frameSkinHeight,selectedPlayer.name);
            }
        }
        if (resultStart && frameNs-resultStart < 2500000000LL) {
            int status = saveAccepted ? skin_image_save_status() : -1;
            const char* message = !frameSkinWidth ? "Skin unavailable" : status == 1 ? "Saving..." : status == 2 ? "Saved" : "Save failed";
            custom_font_draw(message,center,top+padding*2+displayRows*rowH-font,font*3/4,screenW,screenH);
        }
    } else { holdStart = resultStart = 0; holdCompleted = false; }
    custom_font_set_clip(false);
    glClip(oldScissor[0],oldScissor[1],oldScissor[2],oldScissor[3]);
    if (!oldScissorEnabled) glDisableCap(GL_SCISSOR_TEST);
    int visibleNameColumns = nameColumns;
    int scrollMaximum = totalColumns-visibleNameColumns;
    if (scrollMaximum < 0) scrollMaximum = 0;
    float scrollPosition = animateScrollbar(__atomic_load_n(&columnOffset,__ATOMIC_RELAXED),scrollMaximum,frameNs);
    // Only overflow beyond the three-column roster/preview layout adds an indicator.
    if (totalColumns+(preview ? 1 : 0) > 3 && scrollMaximum > 0) {
        int trackWidth = width-padding*2;
        int thumbWidth = trackWidth*visibleNameColumns/totalColumns;
        if (thumbWidth < 8) thumbWidth = 8;
        if (thumbWidth > trackWidth) thumbWidth = trackWidth;
        int trackX = x+padding, trackY = screenH-top-height+padding/2;
        PanelPaint track; track.tint = menu_style::scrollTrack;
        track.opacity = alpha*menu_style::scrollTrackOpacity; track.cornerRadius = menu_style::scrollRadius;
        draw_gl_panel(trackX,trackY,trackWidth,2,track);
        PanelPaint thumb = track; thumb.tint = menu_style::scrollThumb;
        thumb.opacity = alpha*menu_style::scrollThumbOpacity;
        int thumbX = trackX+static_cast<int>((trackWidth-thumbWidth)*scrollPosition/scrollMaximum);
        draw_gl_panel(thumbX,trackY,thumbWidth,2,thumb);
    }
    custom_font_set_opacity(1);
    custom_font_set_mojangles(false);
}
