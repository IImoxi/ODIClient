#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../render.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
static RenderSettings persisted;
RenderSettings client_settings_get_render() { return persisted; }
void client_settings_set_render(const RenderSettings& value) { persisted = value; }

template<class T> static void put(void* object, unsigned long offset, T value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}
struct Fixture {
    alignas(8) unsigned char closure[0x70]{};
    alignas(8) unsigned char builder[0x498]{};
    bool separate = false;
    float camera[3]{0,64,0};
    ChunkPosition storage[32]{}, moved[32]{};
    PositionList alternative{storage,storage,storage+32};
    Fixture() {
        put(builder, profile::builderList, PositionList{storage,storage,storage+32});
        put(closure, profile::closureSeparate, &separate);
        put(closure, profile::closureList, &alternative);
        put(closure, profile::closureBuilder, builder);
        put(closure, profile::closurePosition, camera);
    }
    PositionList* list() { return outputList(closure); }
};
struct Results { const ChunkPosition* positions; unsigned long size; ChunkPosition* replacement = nullptr; };
static int calls, sideEffects;
static void nativeList(void* closure, const void* input) {
    ++calls;
    ++sideEffects; // Stand-in for dirty-section bookkeeping, which must always run.
    auto list = outputList(closure);
    auto results = static_cast<const Results*>(input);
    if (!list || !results) return;
    unsigned long previous;
    assert(count(list, previous));
    if (results->replacement) {
        if (previous) std::memcpy(results->replacement, list->begin, previous * sizeof(ChunkPosition));
        list->begin = results->replacement;
        list->end = list->begin + previous;
        list->capacity = list->begin + 32;
    }
    assert(list->capacity - list->end >= static_cast<long>(results->size));
    for (unsigned long i = 0; i < results->size; ++i) *list->end++ = results->positions[i];
}
int main() {
    original = nativeList;
    assert(!client_render_enabled() && !client_render_below() && !client_render_above());
    assert(client_render_below_distance() == 64 && client_render_above_distance() == 128);
    const ChunkPosition positions[] = {{0,1,0},{1,2,0},{2,3,0},{3,4,0},{4,5,0},{5,6,0}};
    Results results{positions,6};
    Fixture disabled;
    ready = true;
    renderList(disabled.closure, &results);
    assert(disabled.list()->end - disabled.list()->begin == 6 && calls == 1 && sideEffects == 1);
    client_set_render(true); client_set_render_below(true); client_set_render_above(true);
    client_set_render_below_distance(32); client_set_render_above_distance(32);
    Fixture both;
    renderList(both.closure, &results);
    assert(both.list()->end - both.list()->begin == 4);
    for (int i=0;i<4;++i) assert(both.storage[i].x == i+1 && both.storage[i].y == i+2);
    assert(calls == 2 && sideEffects == 2);

    // Preserve existing camera contributions, including when native append reallocates.
    Fixture prefix;
    *prefix.list()->end++ = {99,-500,99};
    results.replacement = prefix.moved;
    renderList(prefix.closure, &results);
    assert(prefix.list()->begin == prefix.moved && prefix.list()->end - prefix.list()->begin == 5);
    assert(prefix.moved[0].x == 99 && prefix.moved[0].y == -500);
    assert(prefix.moved[1].y == 2 && prefix.moved[4].y == 5);
    results.replacement = nullptr;

    Fixture separate;
    separate.separate = true;
    renderList(separate.closure, &results);
    assert(separate.alternative.end - separate.alternative.begin == 4);
    auto shared = reinterpret_cast<PositionList*>(separate.builder + profile::builderList);
    assert(shared->end == shared->begin);
    // Full section bounds preserve sections that partly intersect the range.
    Fixture partial;
    partial.camera[1] = 65;
    renderList(partial.closure, &results);
    assert(partial.list()->end - partial.list()->begin == 5 && partial.storage[4].y == 6);

    client_set_render_above(false); client_set_render_below_distance(16);
    Fixture negative;
    negative.camera[1] = 0;
    const ChunkPosition underground[] = {{1,-2,0},{2,-1,0},{3,0,0},{4,100,0}};
    Results caves{underground,4};
    renderList(negative.closure, &caves);
    assert(negative.list()->end - negative.list()->begin == 3 && negative.storage[0].y == -1);
    client_set_render_below(false); client_set_render_above(true);
    Fixture above;
    renderList(above.closure, &results);
    assert(above.list()->end - above.list()->begin == 5 && above.storage[0].y == 1);

    Fixture invalid;
    invalid.camera[1] = std::numeric_limits<float>::quiet_NaN();
    renderList(invalid.closure, &results);
    assert(invalid.list()->end - invalid.list()->begin == 6);
    ready = false;
    Fixture unavailable;
    renderList(unavailable.closure, &results);
    assert(unavailable.list()->end - unavailable.list()->begin == 6);
    client_set_render_below_distance(-1); client_set_render_above_distance(1000);
    assert(client_render_below_distance() == 16 && client_render_above_distance() == 256);
    assert(persisted.enabled && !persisted.below && persisted.above
           && persisted.belowDistance == 16 && persisted.aboveDistance == 256);


    // Execute the actual vtable patch and reject every native ABI gate independently.
    assert(hooks::initialize());
    unsigned long size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image != MAP_FAILED);
    unsigned long base = reinterpret_cast<unsigned long>(image);
    unsigned int note[] = {4,20,3,0x00554e47};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    std::memcpy(image + minecraft_build::current::buildNote + 16,
                minecraft_build::current::buildId, sizeof(minecraft_build::current::buildId));
    auto slot = reinterpret_cast<unsigned long*>(image + profile::listVtable + profile::listInvokeSlot);
    *slot = base + profile::listCallback;
    std::memcpy(image+profile::listCallback,profile::listEntry,sizeof(profile::listEntry));
    std::memcpy(image+profile::listCaptureSite,profile::listCaptureSignature,sizeof(profile::listCaptureSignature));
    std::memcpy(image+profile::listOutputSelect,profile::listOutputSignature,sizeof(profile::listOutputSignature));
    std::memcpy(image+profile::listConsumptionSite,profile::listConsumptionSignature,sizeof(profile::listConsumptionSignature));
    // Unwind the verified native prologue and tail-call the mock through its real ABI.
    const unsigned char unwind[] = {0x48,0x81,0xc4,0xe8,0,0,0,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0x48,0xb8};
    auto tail = image+profile::listCallback+sizeof(profile::listEntry);
    std::memcpy(tail,unwind,sizeof(unwind));
    auto destination = reinterpret_cast<unsigned long>(&nativeList);
    std::memcpy(tail+sizeof(unwind),&destination,8);
    tail[sizeof(unwind)+8]=0xff; tail[sizeof(unwind)+9]=0xe0;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    auto corrupt = [&](unsigned long offset) {
        auto page = image+(offset & ~(hooks::page_size()-1));
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_WRITE)==0);
        image[offset] ^= 1;
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_EXEC)==0);
    };
    const unsigned long gates[] = {minecraft_build::current::buildNote+16, profile::listCallback,
        profile::listCaptureSite,profile::listOutputSelect,profile::listConsumptionSite,
        profile::listVtable+profile::listInvokeSlot};
    for (auto gate:gates) { corrupt(gate); assert(!install(base)); corrupt(gate); }
    auto slotPage = image+((profile::listVtable+profile::listInvokeSlot)&~(hooks::page_size()-1));
    assert(mprotect(slotPage,hooks::page_size(),PROT_READ)==0);
    assert(install(base));
    client_set_render_below(true); client_set_render_above(false); client_set_render_below_distance(32);
    auto callback = reinterpret_cast<ListCallback>(*slot);
    Fixture inactive;
    callback(inactive.closure,&results); // Retained/inactive forwarding remains valid.
    assert(inactive.list()->end - inactive.list()->begin == 6);
    ready=true;
    Fixture active;
    callback(active.closure,&results);
    assert(active.list()->end - active.list()->begin == 5 && active.storage[0].y == 2);
    client_set_render(false);
    Fixture restored;
    callback(restored.closure,&results);
    assert(restored.list()->end - restored.list()->begin == 6);
    std::puts("PASS: Render vertical section bounds, negative heights, disabled forwarding, native side effects, multi-camera lists, reallocation, saved controls, and executable build/vtable/ABI gates");
}
