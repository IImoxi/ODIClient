#include <pthread.h>
#include "launcher_api.h"
#include "zoom.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace zoomBuild = minecraft_build::current::zoom;
// These read-only camera sites request the FOV option and retain native settings.
constexpr auto& sites = zoomBuild::sites;
constexpr auto floorSite = zoomBuild::floorSite;
unsigned int* cameraFloor;
int level; // Zero disables the camera override.
const char* status = "FOV zoom: initializing";
void* (*allocate)(unsigned long);
decltype(&pthread_key_create) createKey;
decltype(&pthread_getspecific) getSpecific;
decltype(&pthread_setspecific) setSpecific;
void (*release)(void*);
float (*tangent)(float), (*arcTangent)(float);
pthread_key_t proxyKey;
struct Proxy {
    alignas(8) unsigned char option[zoomBuild::proxyOptionSize];
    alignas(8) unsigned char definition[zoomBuild::proxyDefinitionSize];
};
void copy(unsigned char* out, const unsigned char* in, unsigned long size) {
    for (unsigned long i = 0; i < size; ++i) out[i] = in[i];
}
float scaledFov(float degrees, int magnification) {
    constexpr float radians = 0.017453292519943295f;
    return 2.0f * arcTangent(tangent(degrees * radians * 0.5f)
                            / (magnification / 10.0f)) / radians;
}
void* cameraOption(void* reader, int id) {
    auto table = *reinterpret_cast<void***>(reader);
    auto original = reinterpret_cast<void* (*)(void*, int)>(table[zoomBuild::optionGetterSlot / 8]);
    auto option = static_cast<unsigned char*>(original(reader, id));
    int magnification = __atomic_load_n(&level, __ATOMIC_ACQUIRE);
    if (!option || id != zoomBuild::fovOptionId || magnification < 15 || magnification > 300) return option;
    // Same bounded override traversal as native callers, with cycle protection.
    for (int i = 0; i < 32; ++i) {
        auto definition = *reinterpret_cast<unsigned char**>(option + zoomBuild::optionDefinition);
        if (!definition) return option;
        auto next = *reinterpret_cast<unsigned char**>(definition + zoomBuild::definitionOverride);
        if (next) { option = next; continue; }
        auto values = reinterpret_cast<float*>(option + zoomBuild::optionValues);
        if (!(values[0] > 0 && values[0] <= values[2] && values[2] <= values[1] && values[1] < 180)) return option;
        auto proxy = static_cast<Proxy*>(getSpecific(proxyKey));
        if (!proxy) {
            proxy = static_cast<Proxy*>(allocate(sizeof(Proxy)));
            if (!proxy) return option;
            if (setSpecific(proxyKey, proxy) != 0) { release(proxy); return option; }
        }
        copy(proxy->option, option, sizeof(proxy->option));
        copy(proxy->definition, definition, sizeof(proxy->definition));
        *reinterpret_cast<unsigned char**>(proxy->option + zoomBuild::optionDefinition) = proxy->definition;
        *reinterpret_cast<void**>(proxy->definition + zoomBuild::definitionOverride) = nullptr;
        auto output = reinterpret_cast<float*>(proxy->option + zoomBuild::optionValues);
        for (int j = 0; j < 3; ++j) output[j] = scaledFov(values[j], magnification);
        return proxy->option;
    }
    return option;
}
bool verified(unsigned long base) {
    if (!hooks::supported(base)) return false;
    // Include the following mov %rax,%rdx in the gate and atomic patch word.
    for (auto site : sites)
        if (!hooks::readable(base, site, sizeof(zoomBuild::cameraCall), true)
            || !hooks::matches(base, site, zoomBuild::cameraCall, sizeof(zoomBuild::cameraCall))) return false;
    return hooks::matches(base, sites[0] - zoomBuild::firstSetup,
                          zoomBuild::firstRead, sizeof(zoomBuild::firstRead))
        && hooks::matches(base, floorSite, zoomBuild::floorLoad, sizeof(zoomBuild::floorLoad));
}
bool install(unsigned long base) {
    if (!verified(base)) return false;
    unsigned long pageSize = hooks::page_size();
    if (!pageSize) return false;
    // A near relay keeps the replacement E8 calls in signed 32-bit range.
    unsigned char* relay = hooks::allocate_near(base + sites[0], 2);
    if (!relay) return false;
    relay[0] = 0xff; relay[1] = 0x25;
    for (int i = 2; i < 6; ++i) relay[i] = 0;
    *reinterpret_cast<unsigned long*>(relay + 6) = reinterpret_cast<unsigned long>(&cameraOption);
    // The first call straddles a cache line. Replace its preceding eight-byte
    // getter/id setup with a jump; this entry calls the proxy then skips the
    // untouched original call. Every patch stays within one cache line.
    const unsigned char firstRelay[] = {0xbe,static_cast<unsigned char>(zoomBuild::fovOptionId),0,0,0,0x48,0xb8};
    copy(relay + 32, firstRelay, sizeof(firstRelay));
    *reinterpret_cast<unsigned long*>(relay + 39) = reinterpret_cast<unsigned long>(&cameraOption);
    relay[47] = 0xff; relay[48] = 0xd0;
    relay[49] = 0xff; relay[50] = 0x25;
    for (int i = 51; i < 55; ++i) relay[i] = 0;
    *reinterpret_cast<unsigned long*>(relay + 55) = base + sites[0] + 6;
    auto floor = reinterpret_cast<unsigned int*>(relay + pageSize);
    *floor = 0x40a00000; // Second relay page remains writable.
    if (!hooks::make_executable(relay, pageSize)) { hooks::release(relay, pageSize * 2); return false; }
    unsigned long originals[5], patches[5];
    const unsigned long patchSites[] = {sites[0] - 8, sites[1], sites[2], sites[3], floorSite};
    for (unsigned int i = 0; i < 4; ++i) {
        if ((patchSites[i] & 63) > 56) { hooks::release(relay, pageSize * 2); return false; }
        long delta = reinterpret_cast<unsigned long>(relay + (i == 0 ? 32 : 0)) - (base + patchSites[i] + 5);
        if (delta < -2147483648L || delta > 2147483647L) { hooks::release(relay, pageSize * 2); return false; }
        originals[i] = *reinterpret_cast<unsigned long*>(base + patchSites[i]);
        patches[i] = originals[i];
        auto bytes = reinterpret_cast<unsigned char*>(&patches[i]);
        bytes[0] = i == 0 ? 0xe9 : 0xe8;
        unsigned int displacement = static_cast<unsigned int>(delta);
        for (int j = 0; j < 4; ++j) bytes[j+1] = static_cast<unsigned char>(displacement >> (j*8));
        bytes[5] = 0x90;
        if (i == 0) bytes[6] = bytes[7] = 0x90;
    }
    long floorDelta = reinterpret_cast<unsigned long>(floor) - (base + floorSite + 8);
    if (floorDelta < -2147483648L || floorDelta > 2147483647L) { hooks::release(relay, pageSize * 2); return false; }
    originals[4] = *reinterpret_cast<unsigned long*>(base + floorSite);
    patches[4] = originals[4];
    auto floorBytes = reinterpret_cast<unsigned char*>(&patches[4]);
    for (int j = 0; j < 4; ++j) floorBytes[j+4] = static_cast<unsigned char>(static_cast<unsigned int>(floorDelta) >> (j*8));
    hooks::Patch batch[5];
    for (unsigned int i = 0; i < 5; ++i)
        batch[i] = {patchSites[i], originals[i], patches[i]};
    auto result = hooks::install("Zoom", base, batch, 5);
    if (result == hooks::InstallResult::Installed) { cameraFloor = floor; return true; }
    // A failed rollback may leave a branch into this relay; keep it alive.
    if (result != hooks::InstallResult::Retained) hooks::release(relay, pageSize * 2);
    return false;
}
}
void zoom_init() {
    static bool initialized;
    if (initialized) return;
    initialized = true;
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    void* math = mcpelauncher_host_dlopen("libm.so.6", 2);
    status = "Zoom unavailable: host API";
    if (!libc || !math) return;
#define LOAD(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(libc, name)); if (!variable) return;
    LOAD(allocate,"malloc"); LOAD(release,"free"); LOAD(createKey,"pthread_key_create");
    LOAD(getSpecific,"pthread_getspecific"); LOAD(setSpecific,"pthread_setspecific");
#undef LOAD
    tangent = reinterpret_cast<decltype(tangent)>(mcpelauncher_host_dlsym(math,"tanf"));
    arcTangent = reinterpret_cast<decltype(arcTangent)>(mcpelauncher_host_dlsym(math,"atanf"));
    if (!tangent || !arcTangent || createKey(&proxyKey, release) != 0) return;
    if (!hooks::initialize()) { status = "Zoom unavailable: hook manager"; return; }
    unsigned long base = hooks::find_game();
    status = base && install(base) ? nullptr : "Zoom unavailable: unsupported build or camera hook";
}
void zoom_update(bool active, int tenths) {
    bool zooming = active && !status && tenths >= 15 && tenths <= 300;
    // Keep the normal floor when inactive; 0.1 degrees permits the full 30x range.
    if (cameraFloor) __atomic_store_n(cameraFloor, zooming ? 0x3dcccccdU : 0x40a00000U, __ATOMIC_RELEASE);
    __atomic_store_n(&level, zooming ? tenths : 0, __ATOMIC_RELEASE);
}
const char* zoom_status() { return status; }
