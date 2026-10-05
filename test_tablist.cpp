#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include "tablist.cpp"

#ifdef TABLIST_PRESENCE_FIXTURE
void test_presence_load(const char*, long long);
#endif
void auto_gg_lobby_player(void*,const char*) {}
void auto_gg_lobby_dispatch(void*) {}
void auto_gg_lobby_reset() {}
bool client_settings_get_tablist_mojangles() { return false; }
void custom_font_set_mojangles(bool) {}
float custom_menu_main_button_radius(int,int) { return 2; }
static int prefixes;
static bool saved = true;
bool client_settings_get_tablist() { return saved; }
void client_settings_set_tablist(bool value) { saved = value; }
static int blurredPanels, scrollbars, scrollbarX, scrollbarWidth;
static int screenWidth = 1280, screenHeight = 720, panels, faces, labels;
static EGLDisplay display() { return reinterpret_cast<EGLDisplay>(1); }
static EGLSurface surface(EGLint) { return reinterpret_cast<EGLSurface>(1); }
static EGLBoolean dimensions(EGLDisplay, EGLSurface, EGLint attribute, EGLint* value) {
    *value = attribute == EGL_WIDTH ? screenWidth : screenHeight; return EGL_TRUE;
}
static GLint clipBox[] = {0,0,1280,720};
static bool clipEnabled;
static void mockGet(GLenum, GLint* out) { for (int i=0;i<4;++i) out[i]=clipBox[i]; }
static GLboolean mockEnabled(GLenum) { return clipEnabled; }
static void mockEnable(GLenum) { clipEnabled=true; }
static void mockDisable(GLenum) { clipEnabled=false; }
static void mockClip(GLint x, GLint y, GLsizei w, GLsizei h) { clipBox[0]=x;clipBox[1]=y;clipBox[2]=w;clipBox[3]=h; }
static __eglMustCastToProperFunctionPointerType mockProc(const char* name) {
    void* fn = !std::strcmp(name,"glGetIntegerv") ? reinterpret_cast<void*>(&mockGet)
        : !std::strcmp(name,"glIsEnabled") ? reinterpret_cast<void*>(&mockEnabled)
        : !std::strcmp(name,"glEnable") ? reinterpret_cast<void*>(&mockEnable)
        : !std::strcmp(name,"glDisable") ? reinterpret_cast<void*>(&mockDisable)
        : reinterpret_cast<void*>(&mockClip);
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(fn);
}
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* lib, const char* name) {
    if (!std::strcmp(name,"eglGetProcAddress")) return reinterpret_cast<void*>(&mockProc);
    if (!std::strcmp(name,"eglGetCurrentDisplay")) return reinterpret_cast<void*>(&display);
    if (!std::strcmp(name,"eglGetCurrentSurface")) return reinterpret_cast<void*>(&surface);
    if (!std::strcmp(name,"eglQuerySurface")) return reinterpret_cast<void*>(&dimensions);
    return dlsym(lib,name);
}
bool draw_gl_panel(int x, int y, int w, int h, const PanelPaint& paint) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x+w <= screenWidth && y+h <= screenHeight);
    assert(paint.opacity >= 0 && paint.opacity <= 1);
    if (paint.blurScale > 0) {
        assert(paint.blurTimestampNs > 0 && paint.tintAmount < 1);
        assert(paint.cornerRadius > 0);
        ++blurredPanels;
    }
    if (h==2 && paint.opacity==menu_style::scrollThumbOpacity) {
        ++scrollbars;scrollbarX=x;scrollbarWidth=w;
    }
    ++panels; return true;
}
int custom_font_text_width(const char* text, int height) { return std::strlen(text) * height / 2; }
bool custom_font_draw_left(const char* text, int x, int y, int height, int, int) {
    assert(x >= 0 && y >= 0 && x+custom_font_text_width(text,height) <= screenWidth && y+height <= screenHeight);
    ++labels; return true;
}
bool custom_font_draw_left_color(const char* text, int x, int y, int height, float r, float g, float b, int w, int h) {
    assert(r > 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1);
    if (!std::strcmp(text, "[FL]")) ++prefixes;
    return custom_font_draw_left(text, x, y, height, w, h);
}
bool custom_font_draw(const char* text, int center, int y, int height, int w, int h) {
    return custom_font_draw_left(text,center-custom_font_text_width(text,height)/2,y,height,w,h);
}
void custom_font_set_opacity(float value) { assert(value >= 0 && value <= 1); }
bool custom_font_draw_head(const unsigned char*, int x, int y, int size, int, int) {
    assert(x-size/2 >= 0 && x+size/2 <= screenWidth && y >= 0 && y+size <= screenHeight); ++faces; return true;
}
template<class T> static void put(void* p, unsigned long offset, T value) {
    std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof(value));
}
static void shortName(unsigned char* entry, const char* name) {
    unsigned long size = std::strlen(name); assert(size <= 22);
    entry[profile::rosterName] = size << 1;
    std::memcpy(entry+profile::rosterName+1,name,size);
}
static int nativeCalls;
static void nativeHandle(void*, const void*, void*) { ++nativeCalls; }
static int previews, rings;
void custom_font_set_clip(bool) {}
bool custom_font_draw_skin(const unsigned char*,int w,int h,unsigned long,int x,int y,int size,int,int) {
    assert((w==64 || w==128 || w==256) && (h==w || h==w/2));
    assert(x-size/4>=0 && x+size/4<=screenWidth && y>=0 && y+size<=screenHeight);
    ++previews;return true;
}
bool custom_font_draw_ring(int,int,float,float progress,int,int) { assert(progress>=0);++rings;return true; }
int main() {
    assert(skin_image_init());
    assert(hooks::initialize());
    // Execute the real four-argument dispatcher ABI and its patched vtable slots.
    unsigned long size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0)); assert(image != MAP_FAILED);
    unsigned long base = reinterpret_cast<unsigned long>(image);
    unsigned int note[] = {4,20,3,0x00554e47};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,minecraft_build::current::buildId,20);
    for (int i=0;i<4;++i) {
        put(image,profile::rosterSlots[i],base+profile::rosterFunctions[i]);
        put(image,chat::handlerVtable+profile::rosterHandlerSlots[i],base+profile::rosterBaseHandlers[i]);
        put(image,chat::legacyHandlerVtable+profile::rosterHandlerSlots[i],base+profile::rosterLegacyHandlers[i]);
        unsigned char signature[] = {0x48,0x89,0xd7,0x48,0x8b,0x11,0x48,0x8b,0x07,0x48,0x8b,0x80,0,0,0,0,0xff,0xe0};
        put(signature,12,static_cast<unsigned int>(profile::rosterHandlerSlots[i]));
        std::memcpy(image+profile::rosterFunctions[i],signature,sizeof(signature));
        for (auto function : {profile::rosterBaseHandlers[i],profile::rosterLegacyHandlers[i]}) {
            auto code=image+function; code[0]=0x48; code[1]=0xb8;
            put(code,2,reinterpret_cast<unsigned long>(&nativeHandle)); code[10]=0xff; code[11]=0xe0;
        }
    }
#define FILL(name) std::memcpy(image+profile::name##Site,profile::name##Signature,sizeof(profile::name##Signature))
    FILL(rosterAddLayout); FILL(rosterRemoveLayout); FILL(rosterVectorLayout); FILL(skinPacketLayout);
    FILL(rosterVariantLayout); FILL(skinImageFormat); FILL(skinImageData); FILL(skinImageWidth); FILL(skinImageHeight); FILL(skinImageInit);
#undef FILL
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    auto corrupt = [&](unsigned long offset) {
        auto p=image+(offset & ~(hooks::page_size()-1));
        assert(mprotect(p,hooks::page_size(),PROT_READ|PROT_WRITE)==0); image[offset]^=1;
        assert(mprotect(p,hooks::page_size(),PROT_READ|PROT_EXEC)==0);
    };
    const unsigned long gates[] = {minecraft_build::current::buildNote+16, profile::rosterSlots[0],
        profile::rosterFunctions[1],chat::legacyHandlerVtable+profile::rosterHandlerSlots[2],
        profile::rosterAddLayoutSite,profile::rosterRemoveLayoutSite,profile::rosterVectorLayoutSite,
        profile::skinPacketLayoutSite,profile::rosterVariantLayoutSite,profile::skinImageFormatSite,profile::skinImageDataSite,profile::skinImageWidthSite,
        profile::skinImageHeightSite,profile::skinImageInitSite};
    for (auto gate:gates) { corrupt(gate); assert(!install(base)); corrupt(gate); }
    assert(install(base));
    auto handlerPage = image + (chat::legacyHandlerVtable & ~(hooks::page_size()-1));
    assert(mprotect(handlerPage,hooks::page_size(),PROT_READ|PROT_WRITE)==0);
    for (int i=0;i<4;++i) put(image,chat::legacyHandlerVtable+profile::rosterHandlerSlots[i],reinterpret_cast<unsigned long>(&nativeHandle));
    assert(mprotect(handlerPage,hooks::page_size(),PROT_READ)==0);
    unsigned char handler[8]; put(handler,0,base+chat::legacyHandlerVtable);
    unsigned char packet[0x48]{};
    unsigned char entries[2][profile::rosterEntrySize]{};
    auto send = [&](int kind) {
        void* shared=packet;
        reinterpret_cast<Dispatch>(read<unsigned long>(image,profile::rosterSlots[kind]))(nullptr,nullptr,handler,&shared);
    };
    // Inactive/retained callbacks must still forward original packets.
    send(0); assert(nativeCalls==1 && playerCount==0);
    installed=true;
    auto pixels = static_cast<unsigned char*>(mmap(nullptr,64*64*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(pixels != MAP_FAILED); unsigned char skin[0xa8]{};
    put(skin,profile::skinFormat,4); put(skin,profile::skinWidth,64u); put(skin,profile::skinHeight,64u);
    put(skin,profile::skinPixels,pixels); put(skin,profile::skinBytes,static_cast<unsigned long>(64*64*4));
    for (int y=8;y<16;++y) for(int x=8;x<16;++x) {
        auto p=pixels+(y*64+x)*4; p[0]=255;p[3]=255;
        p=pixels+(y*64+x+32)*4;p[2]=255;p[3]=128;
    }
    for (int i=0;i<2;++i) {
        put(entries[i],profile::rosterVariant,1u); put(entries[i],profile::rosterUuid,static_cast<unsigned long>(i+1));
        put(entries[i],profile::rosterSkin,skin);
    }
    shortName(entries[0],"Zed");shortName(entries[1],"\xc2\xa7" "aAlex");
    put(packet,profile::rosterVector,entries);put(packet,profile::rosterVector+8,entries+2);
    send(0);assert(nativeCalls==2 && playerCount==2 && !std::strcmp(players[0].name,"Alex"));
    assert(players[0].hasHead && players[0].head[0]==127 && players[0].head[2]==128 && players[0].head[3]==255);
    send(0);assert(playerCount==2); // UUID updates do not duplicate players.
    // Skin update, malformed image, removal, and disconnect/start-world reset.
    put(packet,profile::skinPacketUuid,2ul);put(packet,profile::skinPacketUuid+8,0ul);put(packet,profile::skinPacketSkin,skin);
    put(skin,profile::skinWidth,63u);send(1);assert(!players[0].hasHead);
    put(skin,profile::skinWidth,64u);send(1);assert(players[0].hasHead);
    assert(tablist_on_keyboard(9,0,true));
    tablist_render(true,1000000000);tablist_render(true,1250000000);assert(opacity>0.9f && opacity<1);
    tablist_render(true,1500000000);assert(opacity==1 && panels && blurredPanels && faces && labels);
    assert(tablist_on_keyboard(9,2,false));
    tablist_render(true,1500000001);tablist_render(true,2000000001);assert(opacity==0);
    assert(!tablist_on_keyboard(9,0,false));
    assert(tablist_on_keyboard(9,0,true));tablist_render(false,2100000000);assert(!held);
    assert(tablist_on_keyboard(9,2,false));
#ifdef TABLIST_PRESENCE_FIXTURE
    test_presence_load("[\"+Zed\",\"+\\u4e00\",\"-Other\"]", 2200000000);
    assert(tablist_on_keyboard(9,0,true));
    tablist_render(true,2200000000);
    prefixes = 0;
    tablist_render(true,2700000000);
    assert(prefixes == 1);
    assert(!std::strcmp(framePlayers[0].name, "Zed") && frameFlarial[0]);
    assert(!std::strcmp(framePlayers[1].name, "Alex") && !frameFlarial[1]);
    assert(tablist_on_keyboard(9,2,false));
#endif
    assert(tablist_on_keyboard(9,0,true));
    assert(tablist_on_scroll(-1) && selection==0);
    tablist_render(true,2800000000LL);tablist_render(true,2900000000LL);
    assert(previews && frameSkinWidth==64 && frameSkinHeight==64);
    assert(!std::memcmp(frameSkin,pixels,64*64*4));
    assert(tablist_on_keyboard(9,2,true));
    // Follow UUID across real native roster inserts, departures, name changes,
    // and Flarial sorting. Scroll resolves the current rank even before rendering.
#ifdef TABLIST_PRESENCE_FIXTURE
    test_presence_load("[]",2910000000LL);
#endif
    assert(tablist_on_keyboard(9,0,true));
    assert(tablist_on_scroll(-1));tablist_render(true,2920000000LL);
    assert(selectedPlayer.uuid[0]==2 && selection==0); // Alex
    unsigned char newcomer[profile::rosterEntrySize]{};
    put(newcomer,profile::rosterVariant,1u);put(newcomer,profile::rosterUuid,3ul);
    shortName(newcomer,"Aaron");
    auto sendEntry = [&](unsigned char* entry) {
        put(packet,profile::rosterVector,entry);put(packet,profile::rosterVector+8,entry+profile::rosterEntrySize);send(0);
    };
    sendEntry(newcomer);tablist_render(true,2930000000LL);
    assert(selection==1 && selectedPlayer.uuid[0]==2 && !std::strcmp(selectedPlayer.name,"Alex"));
    assert(tablist_on_mouse_button(2,0,true));tablist_render(true,2940000000LL);
    long long started=holdStart;
    put(newcomer,profile::rosterVariant,0u);sendEntry(newcomer);
    tablist_render(true,2950000000LL);
    assert(selection==0 && selectedPlayer.uuid[0]==2 && rightDown && holdStart==started);
    assert(tablist_on_mouse_button(2,1,true));
    put(newcomer,profile::rosterVariant,1u);sendEntry(newcomer);
    tablist_on_scroll(-1);tablist_render(true,2960000000LL);
    assert(selectedPlayer.uuid[0]==1); // Next after Alex is Zed, even before a new frame.
    shortName(entries[0],"Aardvark");sendEntry(entries[0]);
    tablist_render(true,2970000000LL);assert(selection==0 && selectedPlayer.uuid[0]==1);
#ifdef TABLIST_PRESENCE_FIXTURE
    test_presence_load("[\"+Alex\"]",2970000001LL);
    tablist_render(true,2970000002LL);assert(selection==1 && selectedPlayer.uuid[0]==1);
    test_presence_load("[]",2970000003LL);
#endif
    tablist_render(true,2970000004LL);
    assert(tablist_on_mouse_button(2,0,true));
    put(entries[0],profile::rosterVariant,0u);sendEntry(entries[0]);
    assert(!rightDown);tablist_render(true,2980000000LL);
    assert(selection==0 && selectedPlayer.uuid[0]==3); // Next: Aaron.
    assert(tablist_on_mouse_button(2,1,true));
    put(entries[0],profile::rosterVariant,1u);shortName(entries[0],"Zed");sendEntry(entries[0]);
    tablist_render(true,2981000000LL);assert(selectedPlayer.uuid[0]==3);
    tablist_on_scroll(-1);tablist_on_scroll(-1);tablist_render(true,2982000000LL);
    assert(selectedPlayer.uuid[0]==1); // Last: Zed.
    put(entries[0],profile::rosterVariant,0u);sendEntry(entries[0]);
    tablist_render(true,2983000000LL);assert(selectedPlayer.uuid[0]==2); // Previous: Alex.
    put(entries[1],profile::rosterVariant,0u);sendEntry(entries[1]);
    tablist_render(true,2984000000LL);assert(selectedPlayer.uuid[0]==3);
    put(newcomer,profile::rosterVariant,0u);sendEntry(newcomer);
    assert(selection==-1 && !rightDown); // Empty roster has no replacement.
    put(entries[0],profile::rosterVariant,1u);put(entries[1],profile::rosterVariant,1u);
    sendEntry(entries[0]);sendEntry(entries[1]);
    assert(tablist_on_keyboard(9,2,true));
    client_set_tablist(false);assert(!saved && !tablist_on_keyboard(9,0,true));client_set_tablist(true);
    // Dense rosters paginate at narrow, ordinary and large framebuffer sizes.
    { Lock lock; for(int i=0;i<playerCount;++i) releaseSkin(players[i]); playerCount=200; for(int i=2;i<playerCount;++i) players[i]=players[0]; for(int i=0;i<playerCount;++i) { players[i].uuid[0]=i+1000;players[i].uuid[1]=0; } }
    const int widths[]={320,1280,2560}, heights[]={240,720,1440};
    for (int i=0;i<3;++i) {
        screenWidth=widths[i];screenHeight=heights[i];assert(tablist_on_keyboard(9,0,true));
        tablist_render(true,3000000000+i*1000000000LL);tablist_render(true,3500000000+i*1000000000LL);
        assert(tablist_on_scroll(-1));tablist_render(true,3600000000+i*1000000000LL);assert(selection==0 && columnOffset==0);
        for (int step=0;step<40;++step) tablist_on_scroll(-1);
        tablist_render(true,3700000000+i*1000000000LL); assert(selection==40 && columnOffset>0);
        tablist_on_scroll(1); assert(selection==39);
        assert(tablist_on_keyboard(9,2,true));
    }
    // Overlapping column windows keep the last two name columns together,
    // including a partial final column. The horizontal thumb reaches both ends.
    screenWidth=1280;screenHeight=720;
    { Lock lock; playerCount=25; }
    assert(tablist_on_keyboard(9,0,true));
    tablist_render(true,6100000000LL);tablist_render(true,6600000000LL);
    int indicators=scrollbars;assert(indicators>0);
    tablist_on_scroll(-1);tablist_render(true,6610000000LL);
    for(int step=0;step<24;++step) tablist_on_scroll(-1);
    tablist_render(true,6620000000LL);
    assert(selection==24 && columnOffset==2);
    assert(framePlayers[0].uuid[0]==1016 && framePlayers[8].uuid[0]==1024);
    int finalWidth=widthTarget;
    tablist_render(true,6820000000LL);
    assert(scrollbarX+scrollbarWidth==(screenWidth-static_cast<int>(panelWidth+0.5f))/2+static_cast<int>(panelWidth+0.5f)-14);
    tablist_on_scroll(1);tablist_render(true,6830000000LL);
    assert(selection==23 && columnOffset==2 && widthTarget==finalWidth);
    for(int step=0;step<8;++step) tablist_on_scroll(1);
    tablist_render(true,6840000000LL);assert(selection==15 && columnOffset==1);
    for(int step=0;step<8;++step) tablist_on_scroll(1);
    tablist_render(true,6850000000LL);assert(selection==7 && columnOffset==0);
    { Lock lock; playerCount=9; }
    tablist_render(true,6860000000LL);tablist_render(true,6870000000LL);
    int noOverflowIndicators=scrollbars;
    tablist_render(true,6880000000LL);assert(scrollbars==noOverflowIndicators);
    assert(tablist_on_keyboard(9,2,true));
    { Lock lock; playerCount=200; }
    // First scroll in either direction selects the top-left name. Right-click
    // releases stay consumed after Tab closes; focus loss cancels the hold.
    screenWidth=1280;screenHeight=720;
    assert(tablist_on_keyboard(9,0,true));
    assert(!tablist_on_mouse_button(2,0,true));
    assert(tablist_on_scroll(1) && selection==0);
    tablist_render(true,7000000000LL);
    assert(tablist_on_mouse_button(2,0,true));
    tablist_render(true,7010000000LL);tablist_render(true,7510000000LL);
    assert(holdStart && !holdCompleted && rings);
    tablist_on_scroll(-1); tablist_render(true,7610000000LL);
    assert(!rightDown && !holdStart);
    assert(tablist_on_mouse_button(2,1,true));
    assert(tablist_on_mouse_button(2,0,true));
    tablist_render(true,7620000000LL);tablist_render(true,8620000000LL);
    assert(holdCompleted && resultStart==8620000000LL && !saveAccepted);
    tablist_render(true,8720000000LL);assert(resultStart==8620000000LL);
    assert(tablist_on_mouse_button(2,1,true));
    assert(tablist_on_mouse_button(2,0,true));
    tablist_render(true,8730000000LL);assert(!holdCompleted && holdStart==8730000000LL);
    assert(tablist_on_mouse_button(2,1,true));
    assert(tablist_on_mouse_button(2,0,true));
    tablist_render(true,8820000000LL);
    assert(tablist_on_keyboard(9,2,true) && selection==-1 && !rightDown);
    assert(tablist_on_mouse_button(2,1,false));
    assert(!tablist_on_mouse_button(2,1,false));
    assert(tablist_on_keyboard(9,0,true));tablist_on_scroll(-1);
    tablist_render(true,8900000000LL);tablist_render(true,9400000000LL);
    float expanded=panelWidth;
    { Lock lock; playerCount=1; }
    tablist_render(true,9400000001LL);assert(panelWidth==expanded);
    tablist_render(true,9650000001LL);assert(panelWidth<expanded && panelWidth>widthTarget);
    tablist_render(true,9900000001LL);assert(panelWidth==widthTarget);
    { Lock lock; clear(); }
    // Restore roster payload after the skin-packet fixture reused the same bytes.
    put(packet,profile::rosterVector,entries);put(packet,profile::rosterVector+8,entries+2);send(0);
    put(entries[1],profile::rosterVariant,0u);send(0);assert(playerCount==1);
    send(2);assert(playerCount==0);send(0);assert(playerCount==1);
    send(3);assert(playerCount==0);
    put(packet,profile::rosterVector+8,1ul);send(0);assert(playerCount==0);
    std::puts("PASS: Tablist native gates/forwarding, full skin capture, UUID selection across joins/departures/renames/presence, adjacent fallback, overlapping column windows/horizontal scrollbar, right-click completion/cancellation/releases, and animated column/row layout");
}
