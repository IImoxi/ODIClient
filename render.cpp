#include "render.h"
#include "client_modules.h"
#include "client_settings.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render;
using ListCallback = void (*)(void*, const void*);
ListCallback original;
bool ready;
const char* error = "Render: initializing";
// One atomic snapshot per callback, including both distances.
constexpr unsigned int belowShift = 8, aboveShift = 17, distanceMask = 511;
unsigned int options = (64u << belowShift) | (128u << aboveShift);
struct ChunkPosition { int x, y, z; };
struct PositionList { ChunkPosition *begin, *end, *capacity; };
static_assert(sizeof(ChunkPosition) == profile::positionSize);
static_assert(sizeof(PositionList) == profile::vectorSize);

template<class T> T field(const void* object, unsigned long offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object) + offset);
}
unsigned int snapshot() { return __atomic_load_n(&options, __ATOMIC_ACQUIRE); }
RenderSettings settings(unsigned int value) {
    return {bool(value & 1), bool(value & 2), bool(value & 4),
            int((value >> belowShift) & distanceMask), int((value >> aboveShift) & distanceMask)};
}
int distance(int value) { return value < 16 ? 16 : value > 256 ? 256 : value; }
void change(unsigned int mask, unsigned int value) {
    unsigned int old = snapshot(), next;
    do { next = (old & ~mask) | value; }
    while (!__atomic_compare_exchange_n(&options, &old, next, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    client_settings_set_render(settings(snapshot()));
}
bool count(const PositionList* list, unsigned long& result) {
    if (!list) return false;
    auto begin = reinterpret_cast<unsigned long>(list->begin);
    auto end = reinterpret_cast<unsigned long>(list->end);
    auto capacity = reinterpret_cast<unsigned long>(list->capacity);
    if (begin > end || end > capacity || (!begin && capacity)) return false;
    auto bytes = end - begin;
    // A malformed or unexpectedly large list retains native behavior.
    if (bytes % sizeof(ChunkPosition) || bytes / sizeof(ChunkPosition) > 1048576) return false;
    result = bytes / sizeof(ChunkPosition);
    return true;
}
PositionList* outputList(void* closure) {
    auto separate = field<const bool*>(closure, profile::closureSeparate);
    if (!separate) return nullptr;
    if (*separate) return field<PositionList*>(closure, profile::closureList);
    auto builder = field<unsigned char*>(closure, profile::closureBuilder);
    return builder ? reinterpret_cast<PositionList*>(builder + profile::builderList) : nullptr;
}
void renderList(void* closure, const void* results) {
    unsigned int value = snapshot();
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE) || !(value & 1) || !(value & 6) || !closure) {
        original(closure, results);
        return;
    }
    auto list = outputList(closure);
    auto position = field<const float*>(closure, profile::closurePosition);
    unsigned long previous = 0;
    bool valid = count(list, previous) && position && position[1] > -30000000.0f
        && position[1] < 30000000.0f; // Also rejects NaN/infinity.
    float cameraY = valid ? position[1] : 0.0f;

    // Preserve frustum checks, dirty-section bookkeeping, and every native side effect.
    original(closure, results);
    unsigned long total;
    if (!valid || !count(list, total) || total < previous || total == previous) return;
    auto input = list->begin + previous;
    auto output = input;
    float lower = cameraY - ((value >> belowShift) & distanceMask);
    float upper = cameraY + ((value >> aboveShift) & distanceMask);
    for (; input != list->end; ++input) {
        float bottom = static_cast<float>(input->y) * profile::sectionSize;
        float top = bottom + profile::sectionSize;
        if (((value & 2) && top <= lower) || ((value & 4) && bottom >= upper)) continue;
        if (output != input) *output = *input;
        ++output;
    }
    // Only the suffix produced by this camera is filtered. Native code merges
    // multiple camera lists and creates matching visibility masks afterward.
    list->end = output;
}
bool install(unsigned long base) {
    if (!hooks::supported(base)
        || !hooks::readable(base, profile::listCallback, profile::callbackSize, true)
        || !hooks::matches_pointer(base, profile::listVtable + profile::listInvokeSlot,
                                   profile::listCallback)
        || !hooks::matches(base, profile::listCallback, profile::listEntry, sizeof(profile::listEntry))
        || !hooks::matches(base, profile::listCaptureSite, profile::listCaptureSignature,
                           sizeof(profile::listCaptureSignature))
        || !hooks::matches(base, profile::listOutputSelect, profile::listOutputSignature,
                           sizeof(profile::listOutputSignature))
        || !hooks::matches(base, profile::listConsumptionSite, profile::listConsumptionSignature,
                           sizeof(profile::listConsumptionSignature))) return false;
    original = reinterpret_cast<ListCallback>(base + profile::listCallback);
    const hooks::Patch patch{profile::listVtable + profile::listInvokeSlot,
                            base + profile::listCallback, reinterpret_cast<unsigned long>(&renderList)};
    // On Retained, original remains usable and ready stays false.
    return hooks::install("Render", base, &patch, 1) == hooks::InstallResult::Installed;
}
}

void render_init() {
    static bool initialized;
    if (initialized) return;
    initialized = true;
    auto saved = client_settings_get_render();
    __atomic_store_n(&options, unsigned(saved.enabled) | (unsigned(saved.below) << 1)
        | (unsigned(saved.above) << 2) | (unsigned(distance(saved.belowDistance)) << belowShift)
        | (unsigned(distance(saved.aboveDistance)) << aboveShift), __ATOMIC_RELEASE);
    const char* failure = "Render unavailable: hook manager";
    if (hooks::initialize()) {
        auto base = hooks::find_game();
        failure = "Render unavailable: unsupported build or chunk-list ABI";
        if (base && install(base)) {
            __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
            failure = nullptr;
        }
    }
    __atomic_store_n(&error, failure, __ATOMIC_RELEASE);
}
const char* render_error() { return __atomic_load_n(&error, __ATOMIC_ACQUIRE); }
void client_set_render(bool value) { change(1, unsigned(value)); }
bool client_render_enabled() { return snapshot() & 1; }
void client_set_render_below(bool value) { change(2, unsigned(value) << 1); }
bool client_render_below() { return snapshot() & 2; }
void client_set_render_above(bool value) { change(4, unsigned(value) << 2); }
bool client_render_above() { return snapshot() & 4; }
void client_set_render_below_distance(int value) { change(distanceMask << belowShift, unsigned(distance(value)) << belowShift); }
int client_render_below_distance() { return (snapshot() >> belowShift) & distanceMask; }
void client_set_render_above_distance(int value) { change(distanceMask << aboveShift, unsigned(distance(value)) << aboveShift); }
int client_render_above_distance() { return (snapshot() >> aboveShift) & distanceMask; }
