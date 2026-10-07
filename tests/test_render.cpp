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
static long long mockTime = 1000000000LL;
static int clockReads, logWrites;
static char logLine[1536];
static bool failWrite;
static int prepareMockCalls;
static void mockPrepare(void* builder, const void* position, const void* direction, float value, bool flag) {
    assert(builder == reinterpret_cast<void*>(1) && position == reinterpret_cast<void*>(2)
        && direction == reinterpret_cast<void*>(3) && value == 0.375f && flag);
    ++prepareMockCalls;
}
static int mockClock(clockid_t, timespec* value) {
    ++clockReads;
    value->tv_sec = mockTime / 1000000000LL;
    value->tv_nsec = mockTime % 1000000000LL;
    mockTime += 1000000;
    return 0;
}
static ssize_t mockWrite(int fd, const void* data, size_t size) {
    assert(fd == 123 && size < sizeof(logLine));
    std::memcpy(logLine, data, size);
    logLine[size] = 0;
    ++logWrites;
    return failWrite ? -1 : static_cast<ssize_t>(size);
}
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


    // Horizontal filtering retains intersecting/tangent sections and preserves prefixes.
    ready = true;
    client_set_render_above(false); client_set_render_horizontal(true); client_set_render_radius(16);
    const ChunkPosition radial[] = {{0,4,0},{1,4,0},{2,4,0},{2,4,1},{-2,4,0},{-3,4,0},{1,4,1},{2,4,2}};
    Results radialResults{radial,8};
    Fixture radius;
    *radius.list()->end++ = {999,4,999};
    radialResults.replacement = radius.moved;
    renderList(radius.closure, &radialResults);
    assert(radius.list()->end - radius.list()->begin == 4);
    assert(radius.moved[0].x == 999 && radius.moved[3].x == -2 && radius.moved[3].z == 0);
    Fixture diagonal;
    diagonal.camera[0] = diagonal.camera[2] = 1;
    radialResults.replacement = nullptr;
    renderList(diagonal.closure, &radialResults);
    assert(diagonal.list()->end - diagonal.list()->begin == 2);
    // Camera movement changes the cutoff immediately; vertical limits compose.
    Fixture movedCamera;
    movedCamera.camera[0] = 32;
    renderList(movedCamera.closure, &radialResults);
    assert(movedCamera.list()->end - movedCamera.list()->begin == 5);
    client_set_render_above(true); client_set_render_above_distance(16);
    Fixture combined;
    combined.camera[1] = 48;
    renderList(combined.closure, &radialResults);
    assert(combined.list()->end == combined.list()->begin);
    client_set_render_above(false); client_set_render_above_distance(256);
    Fixture badX;
    badX.camera[0] = std::numeric_limits<float>::infinity();
    renderList(badX.closure, &radialResults);
    assert(badX.list()->end - badX.list()->begin == 8);
    client_set_render_radius(256);
    assert(client_render_radius() == 256 && client_render_below_distance() == 16
           && client_render_above_distance() == 256 && persisted.horizontal && persisted.radius == 256);
    client_set_render_radius(-10);
    assert(client_render_radius() == 16);
    client_set_render_horizontal(false);
    Fixture cutoffDisabled;
    renderList(cutoffDisabled.closure, &radialResults);
    assert(cutoffDisabled.list()->end - cutoffDisabled.list()->begin == 8);
    ready = false;

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
    std::memcpy(image+profile::prepareFunction,profile::prepareFunctionSignature,sizeof(profile::prepareFunctionSignature));
    std::memcpy(image+profile::prepareArguments,profile::prepareArgumentsSignature,sizeof(profile::prepareArgumentsSignature));
    std::memcpy(image+profile::prepareCall,profile::prepareCallSignature,sizeof(profile::prepareCallSignature));
    // Execute the real call-site patch through a fixture caller with a valid native
    // stack layout. Keep the gated following mov intact and restore callee-saved registers.
    const unsigned char prepareCaller[] = {0x55,0x41,0x56,0x48,0x81,0xec,0x08,0x03,0,0,
        0x48,0x89,0xe5,0x48,0x81,0xc5,0xe8,0x02,0,0,0xe9};
    auto caller = image+profile::prepareCall-64;
    std::memcpy(caller,prepareCaller,sizeof(prepareCaller));
    int jump = 64-sizeof(prepareCaller)-4;
    std::memcpy(caller+sizeof(prepareCaller),&jump,4);
    const unsigned char prepareCallerReturn[] = {0x48,0x81,0xc4,0x08,0x03,0,0,0x41,0x5e,0x5d,0xc3};
    std::memcpy(image+profile::prepareCall+sizeof(profile::prepareCallSignature),prepareCallerReturn,sizeof(prepareCallerReturn));
    const unsigned char prepareUnwind[] = {0x48,0x81,0xc4,0x38,0x02,0,0,0x5b,0x41,0x5c,0x41,0x5d,
        0x41,0x5e,0x41,0x5f,0x5d,0x48,0xb8};
    auto prepareTail = image+profile::prepareFunction+sizeof(profile::prepareFunctionSignature);
    std::memcpy(prepareTail,prepareUnwind,sizeof(prepareUnwind));
    auto prepareDestination = reinterpret_cast<unsigned long>(mockPrepare);
    std::memcpy(prepareTail+sizeof(prepareUnwind),&prepareDestination,8);
    prepareTail[sizeof(prepareUnwind)+8]=0xff; prepareTail[sizeof(prepareUnwind)+9]=0xe0;
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
    const unsigned long preparationGates[] = {profile::prepareFunction, profile::prepareArguments, profile::prepareCall};
    for (auto gate : preparationGates) {
        corrupt(gate); assert(!installPreparationTrace(base)); corrupt(gate);
    }
    assert(installPreparationTrace(base));
    assert(std::memcmp(image+profile::prepareCall+5,profile::prepareCallSignature+5,7)==0);
    auto prepareCallback = reinterpret_cast<PrepareLists>(caller);
    prepareCallback(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),0.375f,true);
    assert(prepareMockCalls==1);
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
    // Tracing preserves native behavior even with the Render module disabled.
    traceClock = mockClock;
    traceWrite = mockWrite;
    traceFormat = std::snprintf;
    traceFd = 123;
    traceStart = mockTime;
    tracing = true;
    prepareCallback(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),0.375f,true);
    assert(prepareMockCalls==2 && preparationTiming.calls==1 && preparationTiming.ns==3000000
        && preparationTiming.maximum==3000000 && preparationCpuNs==1000000);
    Fixture traced;
    int previousCalls = calls;
    callback(traced.closure, &results);
    assert(calls == previousCalls + 1 && traced.list()->end - traced.list()->begin == 6);
    assert(traceCalls == 1 && traceNs == 1000000 && traceMaxNs == 1000000);
    render_trace_frame(16000000);
    assert(logWrites == 0);
    mockTime = traceStart + 1000000000LL;
    render_trace_frame(20000000);
    assert(logWrites == 1 && std::strstr(logLine, ",1,1.000000,1.000000,2,18.000000,20.000000,"));
    assert(traceCalls == 0 && traceNs == 0 && traceMaxNs == 0 && traceFrames == 0);
    assert(traceSectionsProduced==0 && traceSectionsKept==0 && preparationTiming.calls==0);
    assert(std::strstr(logLine, ",6,6,1,3.000000,3.000000,1.000000,"));
    int columns = 1;
    for (const char* p=logLine; *p; ++p) if (*p==',') ++columns;
    assert(columns==112);
    tracing = false;
    int previousReads = clockReads;
    Fixture untraced;
    callback(untraced.closure, &results);
    render_trace_frame(16000000);
    assert(clockReads == previousReads && logWrites == 1);
    tracing = true;
    failWrite = true;
    mockTime = traceStart + 1000000000LL;
    render_trace_frame(16000000);
    assert(!tracing && logWrites==2);
    std::puts("PASS: Render vertical/horizontal section bounds, negative heights, disabled forwarding, native side effects, multi-camera lists, reallocation, saved controls, and executable build/vtable/ABI gates");
}
