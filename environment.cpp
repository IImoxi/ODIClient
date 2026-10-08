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
unsigned long long options = (6000ull << 3) | (100ull << 34) | (2ull << 55) | (2ull << 57) | (1ull << 60);
unsigned long long snapshot() { return __atomic_load_n(&options, __ATOMIC_ACQUIRE); }
EnvironmentSettings settings(unsigned long long v) {
    return {bool(v & 1), bool(v & 2), bool(v & 4), int((v >> 3) & 32767),
            int((v >> 18) & 511), int((v >> 27) & 127), int((v >> 34) & 127),
            bool(v & (1ull << 41)), bool(v & (1ull << 42)), bool(v & (1ull << 43)), bool(v & (1ull << 44)),
            bool(v & (1ull << 45)), bool(v & (1ull << 46)),
            bool(v & (1ull << 47)), int((v >> 48) & 127),
            int((v >> 55) & 3), (int((v >> 57) & 7) + 1) * 8, int((v >> 60) & 3), bool(v & (1ull << 62))};
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
        | (static_cast<unsigned long long>(s.skyReducedSamples) << 46)
        | (static_cast<unsigned long long>(s.weather) << 47)
        | (static_cast<unsigned long long>(s.weatherAmount) << 48)
        | (static_cast<unsigned long long>(s.cloudDetail) << 55)
        | (static_cast<unsigned long long>(s.cloudSamples / 8 - 1) << 57)
        | (static_cast<unsigned long long>(s.cloudResolution) << 60)
        | (static_cast<unsigned long long>(s.skyQuarterResolution) << 62);
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
using WeatherTick = void (*)(void*);
WeatherTick originalWeatherTick;
unsigned long weatherBase;
// Numeric identities and copied scalars only; dereference exclusively inside
// the live Weather callback. The launcher has one live client Overworld.
struct WeatherHistory {
    unsigned long identity, dimension;
    int tick;
    float native[4], applied[4];
};
WeatherHistory weatherHistory{};
int weatherLock;
struct WeatherLock {
    WeatherLock() { while (__atomic_exchange_n(&weatherLock, 1, __ATOMIC_ACQUIRE)) {} }
    ~WeatherLock() { __atomic_store_n(&weatherLock, 0, __ATOMIC_RELEASE); }
};
template<class T> T& weatherField(void* object, unsigned long offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}
void weatherTick(void* weather) {
    auto dimension = weatherField<void*>(weather, profile::weatherDimension);
    auto level = dimension ? weatherField<void*>(dimension, profile::weatherLevel) : nullptr;
    bool client = level && weatherField<unsigned long>(level, 0) == weatherBase + profile::weatherClientLevelTable
        && weatherField<unsigned long>(dimension, 0) == weatherBase + profile::fogTables[0];
    if (!client) { originalWeatherTick(weather); return; }
    const auto identity = reinterpret_cast<unsigned long>(weather);
    const auto dimensionId = reinterpret_cast<unsigned long>(dimension);
    {
        WeatherLock lock;
        auto& h = weatherHistory;
        if (h.identity == identity && h.dimension == dimensionId) {
            if (h.tick == weatherField<int>(weather, profile::weatherTickCounter))
                for (int i = 0; i < 4; ++i)
                    if (weatherField<float>(weather, profile::weatherFields[i]) == h.applied[i])
                        weatherField<float>(weather, profile::weatherFields[i]) = h.native[i];
            h.identity = 0;
        }
    }
    // Native simulation always sees its own interpolation values, including
    // newly received packet changes; never override targets or LevelData.
    originalWeatherTick(weather);
    auto override = environment_weather();
    if (!override.enabled) return;
    WeatherLock lock;
    auto& h = weatherHistory;
    h.identity = identity; h.dimension = dimensionId;
    h.tick = weatherField<int>(weather, profile::weatherTickCounter);
    for (int i = 0; i < 4; ++i) {
        h.native[i] = weatherField<float>(weather, profile::weatherFields[i]);
        h.applied[i] = i < 2 ? override.rain : override.thunder;
        weatherField<float>(weather, profile::weatherFields[i]) = h.applied[i];
    }
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
    hooks::Patch patches[5];
    for (unsigned int i = 0; i < 3; ++i) {
        if (!hooks::matches_pointer(base, profile::fogTables[i] + profile::fogSlot, profile::fogFunctions[i])) return false;
        originals[i] = reinterpret_cast<Fog>(base + profile::fogFunctions[i]);
        patches[i] = {profile::fogTables[i] + profile::fogSlot, base + profile::fogFunctions[i],
                      reinterpret_cast<unsigned long>(replacements[i])};
    }
    originalAngle = reinterpret_cast<Angle>(base + profile::angleFunction);
    patches[3] = {profile::fogTables[0] + profile::angleSlot, base + profile::angleFunction,
                  reinterpret_cast<unsigned long>(&angle)};
    if (!hooks::matches(base, profile::weatherTickFunction, profile::weatherTickSignature,
                        sizeof(profile::weatherTickSignature))
        || !hooks::matches(base, profile::weatherClientCheck, profile::weatherClientSignature,
                           sizeof(profile::weatherClientSignature))
        || !hooks::matches(base, profile::weatherInterpolation, profile::weatherInterpolationSignature,
                           sizeof(profile::weatherInterpolationSignature))) return false;
    auto relay = hooks::allocate_near(base + profile::weatherTickFunction, 1);
    if (!relay) return false;
    auto jump = [](unsigned char* out, unsigned long target) {
        out[0] = 0xff; out[1] = 0x25;
        for (int i = 2; i < 6; ++i) out[i] = 0;
        __builtin_memcpy(out + 6, &target, 8);
    };
    jump(relay, reinterpret_cast<unsigned long>(&weatherTick));
    __builtin_memcpy(relay + 32, profile::weatherTickSignature, 10);
    jump(relay + 42, base + profile::weatherTickFunction + 10);
    long delta = reinterpret_cast<unsigned long>(relay) - (base + profile::weatherTickFunction + 5);
    if (delta < -2147483648L || delta > 2147483647L
        || !hooks::make_executable(relay, hooks::page_size())) {
        hooks::release(relay, hooks::page_size()); return false;
    }
    unsigned char branch[8] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    int displacement = static_cast<int>(delta);
    __builtin_memcpy(branch + 1, &displacement, 4);
    unsigned long expected, replacement;
    __builtin_memcpy(&expected, profile::weatherTickSignature, 8);
    __builtin_memcpy(&replacement, branch, 8);
    patches[4] = {profile::weatherTickFunction, expected, replacement};
    weatherBase = base;
    originalWeatherTick = reinterpret_cast<WeatherTick>(relay + 32);
    auto result = hooks::install("Environment", base, patches, 5);
    if (result == hooks::InstallResult::Installed) return true;
    if (result != hooks::InstallResult::Retained) hooks::release(relay, hooks::page_size());
    return false;
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
void client_set_environment_clouds(bool v) { change(1ull << 42, static_cast<unsigned long long>(v) << 42); }
bool client_environment_clouds() { return snapshot() & (1ull << 42); }
void client_set_environment_cloud_detail(int v) { change(3ull << 55, static_cast<unsigned long long>(v < 1 ? 1 : clamp(v, 3)) << 55); }
int client_environment_cloud_detail() { return settings(snapshot()).cloudDetail; }
void client_set_environment_cloud_samples(int v) {
    int samples = v < 8 ? 8 : clamp(v, 64);
    change(7ull << 57, static_cast<unsigned long long>((samples + 4) / 8 - 1) << 57);
}
int client_environment_cloud_samples() { return settings(snapshot()).cloudSamples; }
void client_set_environment_sky_quarter_resolution(bool v) { change(1ull << 62, static_cast<unsigned long long>(v) << 62); }
bool client_environment_sky_quarter_resolution() { return snapshot() & (1ull << 62); }
void client_set_environment_cloud_resolution(int v) { change(3ull << 60, static_cast<unsigned long long>(clamp(v, 2)) << 60); }
int client_environment_cloud_resolution() { return settings(snapshot()).cloudResolution; }
void client_set_environment_vanilla_celestials(bool v) { change(1ull << 43, static_cast<unsigned long long>(v) << 43); }
bool client_environment_vanilla_celestials() { return snapshot() & (1ull << 43); }

void client_set_environment_weather(bool v) { change(1ull << 47, static_cast<unsigned long long>(v) << 47); }
bool client_environment_weather() { return snapshot() & (1ull << 47); }
void client_set_environment_weather_amount(int v) { change(127ull << 48, static_cast<unsigned long long>(clamp(v, 100)) << 48); }
int client_environment_weather_amount() { return settings(snapshot()).weatherAmount; }
EnvironmentWeather environment_weather() {
    auto s = settings(snapshot());
    return {__atomic_load_n(&ready, __ATOMIC_ACQUIRE) && s.enabled && s.weather,
            (s.weatherAmount < 50 ? s.weatherAmount * 2 : 100) / 100.0f,
            (s.weatherAmount > 50 ? (s.weatherAmount - 50) * 2 : 0) / 100.0f};
}
