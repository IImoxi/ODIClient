#include "environment.h"
#include "client_modules.h"
#include "client_settings.h"
#include "minecraft_build.h"
#include "hook_manager.h"
#include "sky_renderer.h"

namespace {
namespace profile = minecraft_build::current::environment;
struct Color { float r, g, b, a; };
using Fog = Color (*)(void*, const Color&, float);
using Angle = float (*)(void*, int, float);
Fog originals[3];
Angle originalAngle;
bool ready;
float lastSkyAngle;
const char* error = "Environment: initializing";
unsigned long long options = (6000ull << 3) | (100ull << 34) | (1ull << 42);
unsigned long long snapshot() { return __atomic_load_n(&options, __ATOMIC_ACQUIRE); }
EnvironmentSettings settings(unsigned long long v) {
    return {bool(v & 1), bool(v & 2), bool(v & 4), int((v >> 3) & 32767),
            int((v >> 18) & 511), int((v >> 27) & 127), int((v >> 34) & 127),
            bool(v & (1ull << 41)), bool(v & (1ull << 42)), bool(v & (1ull << 43)), bool(v & (1ull << 44)),
            bool(v & (1ull << 45)), bool(v & (1ull << 46))};
}
unsigned long long pack(EnvironmentSettings s) {
    return static_cast<unsigned long long>(s.enabled) | (static_cast<unsigned long long>(s.time) << 1)
        | (static_cast<unsigned long long>(s.fog) << 2) | (static_cast<unsigned long long>(s.ticks) << 3)
        | (static_cast<unsigned long long>(s.hue) << 18) | (static_cast<unsigned long long>(s.saturation) << 27)
        | (static_cast<unsigned long long>(s.value) << 34)
        | (static_cast<unsigned long long>(s.sky) << 41)
        | (static_cast<unsigned long long>(s.clouds) << 42)
        | (static_cast<unsigned long long>(s.vanillaCelestials) << 43)
        | (static_cast<unsigned long long>(s.skyLookup) << 44)
        | (static_cast<unsigned long long>(s.skyHalfResolution) << 45)
        | (static_cast<unsigned long long>(s.skyReducedSamples) << 46);
}
int clamp(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }
void change(unsigned long long mask, unsigned long long bits) {
    auto old = snapshot(); unsigned long long next;
    do { next = (old & ~mask) | bits; }
    while (!__atomic_compare_exchange_n(&options, &old, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    client_settings_set_environment(settings(snapshot()));
}
Color hsv(EnvironmentSettings s, float alpha) {
    float h = (s.hue % 360) / 60.0f, v = s.value / 100.0f, c = v * s.saturation / 100.0f;
    int sector = static_cast<int>(h);
    float fraction = h - sector, x = c * ((sector & 1) ? 1.0f - fraction : fraction), m = v - c;
    switch (sector) {
        case 0: return {c+m, x+m, m, alpha};
        case 1: return {x+m, c+m, m, alpha};
        case 2: return {m, c+m, x+m, alpha};
        case 3: return {m, x+m, c+m, alpha};
        case 4: return {x+m, m, c+m, alpha};
        default: return {c+m, m, x+m, alpha};
    }
}
template<int Index> Color fog(void* dimension, const Color& color, float brightness) {
    Color result = originals[Index](dimension, color, brightness);
    auto s = settings(snapshot());
    return __atomic_load_n(&ready, __ATOMIC_ACQUIRE) && s.enabled && s.fog ? hsv(s, result.a) : result;
}
float angle(void* dimension, int ticks, float partialTick) {
    auto s = settings(snapshot());
    if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) && s.enabled && s.time) {
        ticks = s.ticks; partialTick = 0.0f;
    }
    float result = originalAngle(dimension, ticks, partialTick);
    __atomic_store(&lastSkyAngle, &result, __ATOMIC_RELAXED);
    return result;
}
bool install(unsigned long base) {
    if (!hooks::supported(base)) return false;
    const unsigned long sites[] = {profile::fogOverworldSite, profile::fogPassthroughSite,
                                  profile::angleEntrySite, profile::fogCallerSite, profile::angleCallerSite};
    const unsigned char* signatures[] = {profile::fogOverworldSignature, profile::fogPassthroughSignature,
                                        profile::angleEntrySignature, profile::fogCallerSignature, profile::angleCallerSignature};
    const unsigned long sizes[] = {sizeof(profile::fogOverworldSignature), sizeof(profile::fogPassthroughSignature),
                                  sizeof(profile::angleEntrySignature), sizeof(profile::fogCallerSignature), sizeof(profile::angleCallerSignature)};
    for (unsigned int i = 0; i < 5; ++i)
        if (!hooks::readable(base, sites[i], sizes[i], true) || !hooks::matches(base, sites[i], signatures[i], sizes[i])) return false;
    if (!hooks::matches_pointer(base, profile::fogTables[0] + profile::angleSlot, profile::angleFunction)) return false;
    Fog replacements[] = {fog<0>, fog<1>, fog<2>};
    hooks::Patch patches[4];
    for (unsigned int i = 0; i < 3; ++i) {
        if (!hooks::matches_pointer(base, profile::fogTables[i] + profile::fogSlot, profile::fogFunctions[i])) return false;
        originals[i] = reinterpret_cast<Fog>(base + profile::fogFunctions[i]);
        patches[i] = {profile::fogTables[i] + profile::fogSlot, base + profile::fogFunctions[i],
                      reinterpret_cast<unsigned long>(replacements[i])};
    }
    originalAngle = reinterpret_cast<Angle>(base + profile::angleFunction);
    patches[3] = {profile::fogTables[0] + profile::angleSlot, base + profile::angleFunction,
                  reinterpret_cast<unsigned long>(&angle)};
    return hooks::install("Environment", base, patches, 4) == hooks::InstallResult::Installed;
}
}
void environment_init() {
    static bool initialized;
    if (initialized) return;
    initialized = true;
    __atomic_store_n(&options, pack(client_settings_get_environment()), __ATOMIC_RELEASE);
    sky_renderer_init();
    if (!hooks::initialize() || !install(hooks::find_game())) {
        error = "Environment: unsupported Minecraft build"; return;
    }
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE); error = nullptr;
}
const char* environment_error() { return error; }
float environment_sky_angle() {
    float value;
    __atomic_load(&lastSkyAngle, &value, __ATOMIC_RELAXED);
    return value;
}
void client_set_environment(bool v) { change(1, v); }
bool client_environment_enabled() { return snapshot() & 1; }
void client_set_environment_time(bool v) { change(2, static_cast<unsigned long long>(v) << 1); }
bool client_environment_time() { return snapshot() & 2; }
void client_set_environment_fog(bool v) { change(4, static_cast<unsigned long long>(v) << 2); }
bool client_environment_fog() { return snapshot() & 4; }
void client_set_environment_ticks(int v) { change(32767ull << 3, static_cast<unsigned long long>(clamp(v, 23999)) << 3); }
int client_environment_ticks() { return settings(snapshot()).ticks; }
void client_set_environment_hue(int v) { change(511ull << 18, static_cast<unsigned long long>(clamp(v, 360)) << 18); }
int client_environment_hue() { return settings(snapshot()).hue; }
void client_set_environment_saturation(int v) { change(127ull << 27, static_cast<unsigned long long>(clamp(v, 100)) << 27); }
int client_environment_saturation() { return settings(snapshot()).saturation; }
void client_set_environment_value(int v) { change(127ull << 34, static_cast<unsigned long long>(clamp(v, 100)) << 34); }
int client_environment_value() { return settings(snapshot()).value; }
void client_set_environment_sky(bool v) {
    change(1ull << 41, static_cast<unsigned long long>(v) << 41);
}
bool client_environment_sky() { return snapshot() & (1ull << 41); }
void client_set_environment_vanilla_celestials(bool v) { change(1ull << 43, static_cast<unsigned long long>(v) << 43); }
bool client_environment_vanilla_celestials() { return snapshot() & (1ull << 43); }
