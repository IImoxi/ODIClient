#include <time.h>
#include "launcher_api.h"
#include "fps_limiter.h"
#include "hook_manager.h"
#include "minecraft_build.h"

namespace {
using ClockGetTime = int (*)(clockid_t, timespec*);
using NanoSleep = int (*)(const timespec*, timespec*);
ClockGetTime clockGetTime;
NanoSleep nanoSleep;
const char* error;
bool active;
bool apiAttempted;
long long deadline;
long long frameTimestamp;
long long frameDelta;
long long previousFrameTimestamp;
namespace nativeBuild = minecraft_build::current::fpsLimiter;
struct NativePacing {
    unsigned int intervalBits;
    unsigned int limit;
    unsigned long frames;
};
NativePacing* nativePacing;
bool nativeInstalled, nativeRequested;
unsigned long previousNativeFrames;
bool installNative(unsigned long base) {
    if (!hooks::supported(base)
        || !hooks::readable(base, nativeBuild::intervalSite, sizeof(nativeBuild::intervalStore), true)
        || !hooks::readable(base, nativeBuild::intervalCalculation, sizeof(nativeBuild::calculationSignature), true)
        || !hooks::readable(base, nativeBuild::waitSite, sizeof(nativeBuild::waitSignature), true)
        || !hooks::matches(base, nativeBuild::intervalSite, nativeBuild::intervalStore, sizeof(nativeBuild::intervalStore))
        || !hooks::matches(base, nativeBuild::intervalCalculation, nativeBuild::calculationSignature, sizeof(nativeBuild::calculationSignature))
        || !hooks::matches(base, nativeBuild::waitSite, nativeBuild::waitSignature, sizeof(nativeBuild::waitSignature))) return false;
    auto pageSize = hooks::page_size();
    if (!pageSize) return false;
    auto relay = hooks::allocate_near(base + nativeBuild::intervalSite, 2);
    if (!relay) return false;
    auto data = reinterpret_cast<NativePacing*>(relay + pageSize);
    *data = {};
    // Preserve all registers and flags. Replace only the local interval store;
    // inactive execution replays the original instruction exactly. No ABI call.
    const unsigned char code[] = {
        0x50,0x9c,0x48,0xb8, 0,0,0,0,0,0,0,0,
        0x83,0x38,0,0x74,0x0f,
        0xf0,0x48,0xff,0x40,0x08,0x8b,0x00,
        0x89,0x85,0,0,0,0,0xeb,0x08,
        0,0,0,0,0,0,0,0,
        0x9d,0x58,0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0
    };
    for (unsigned long i = 0; i < sizeof(code); ++i) relay[i] = code[i];
    for (unsigned long i = 0; i < sizeof(nativeBuild::intervalStore); ++i)
        relay[32+i] = nativeBuild::intervalStore[i];
    for (int i = 0; i < 4; ++i) relay[26+i] = nativeBuild::intervalStore[4+i];
    // Byte copies avoid unaligned C++ stores in generated instructions.
    auto dataAddress = reinterpret_cast<unsigned long>(data);
    auto continuation = base + nativeBuild::intervalSite + sizeof(nativeBuild::intervalStore);
    // Override the local candidate before its positive/unlimited branch. This
    // activates native pacing even when the saved vanilla option is unlimited.
    auto capRelay = relay + 128;
    const unsigned char capCode[] = {
        0x50,0x9c,0x48,0xb8,0,0,0,0,0,0,0,0,
        0x83,0x38,0,0x74,0x04,0x44,0x8b,0x78,0x04,0x9d,0x58,
        0x45,0x85,0xff,0x7e,0x13,0xf3,0x41,0x0f,0x2a,0xcf,
        0xff,0x25,0,0,0,0,0,0,0,0,0,0,0,0,
        0xff,0x25,0,0,0,0,0,0,0,0,0,0,0,0
    };
    for (unsigned long i = 0; i < sizeof(capCode); ++i) capRelay[i] = capCode[i];
    auto positive = base + nativeBuild::intervalCalculation + 10;
    auto unlimited = base + nativeBuild::intervalCalculation + 27;
    for (int i = 0; i < 8; ++i) {
        capRelay[4+i] = static_cast<unsigned char>(dataAddress >> (i*8));
        capRelay[39+i] = static_cast<unsigned char>(positive >> (i*8));
        capRelay[53+i] = static_cast<unsigned char>(unlimited >> (i*8));
    }
    for (int i = 0; i < 8; ++i) {
        relay[4+i] = static_cast<unsigned char>(dataAddress >> (i*8));
        relay[48+i] = static_cast<unsigned char>(continuation >> (i*8));
    }
    long delta = reinterpret_cast<unsigned long>(relay) - (base + nativeBuild::intervalSite + 5);
    if (delta < -2147483648L || delta > 2147483647L || !hooks::make_executable(relay, pageSize)) {
        hooks::release(relay, pageSize * 2); return false;
    }
    auto original = *reinterpret_cast<const unsigned long*>(base + nativeBuild::intervalSite);
    unsigned long replacement = 0x90909000000000e9UL;
    auto bytes = reinterpret_cast<unsigned char*>(&replacement);
    for (int i = 0; i < 4; ++i) bytes[1+i] = static_cast<unsigned char>(static_cast<unsigned int>(delta) >> (i*8));
    long capDelta = reinterpret_cast<unsigned long>(capRelay) - (base + nativeBuild::intervalCalculation + 5);
    if (capDelta < -2147483648L || capDelta > 2147483647L) {
        hooks::release(relay, pageSize * 2); return false;
    }
    unsigned long capReplacement = 0x90909000000000e9UL;
    auto capBytes = reinterpret_cast<unsigned char*>(&capReplacement);
    for (int i = 0; i < 4; ++i) capBytes[1+i] = static_cast<unsigned char>(static_cast<unsigned int>(capDelta) >> (i*8));
    unsigned long capOriginal;
    __builtin_memcpy(&capOriginal, nativeBuild::calculationSignature, sizeof(capOriginal));
    hooks::Patch patches[] = {{nativeBuild::intervalSite, original, replacement},
        {nativeBuild::intervalCalculation, capOriginal, capReplacement}};
    auto result = hooks::install("FPS native pacing", base, patches, 2);
    if (result != hooks::InstallResult::Installed) {
        // Retained relays remain alive and inactive, including their data page.
        if (result != hooks::InstallResult::Retained) hooks::release(relay, pageSize * 2);
        return false;
    }
    nativePacing = data;
    return true;
}
void setError(const char* value) { __atomic_store_n(&error, value, __ATOMIC_RELAXED); }

void recordFrameTime(long long current) {
    frameDelta = previousFrameTimestamp ? current - previousFrameTimestamp : 0;
    frameTimestamp = current;
    previousFrameTimestamp = current;
}

bool loadAPI() {
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) return false;
    clockGetTime = reinterpret_cast<ClockGetTime>(mcpelauncher_host_dlsym(libc, "clock_gettime"));
    nanoSleep = reinterpret_cast<NanoSleep>(mcpelauncher_host_dlsym(libc, "nanosleep"));
    return clockGetTime && nanoSleep;
}
long long now() {
    timespec time{};
    if (clockGetTime(CLOCK_MONOTONIC, &time) != 0) return 0;
    return static_cast<long long>(time.tv_sec) * 1000000000LL + time.tv_nsec;
}

}

const char* fps_limiter_error() { return __atomic_load_n(&error, __ATOMIC_RELAXED); }

void fps_limiter_init(bool lowInputDelayEnabled) {
    // Leave game code untouched unless reduced input delay was enabled at startup.
    // Hook installation remains startup-only; enabling later needs a restart.
    if (!lowInputDelayEnabled || !hooks::initialize()) return;
    auto base = hooks::find_game();
    nativeInstalled = base && installNative(base);
}
bool fps_limiter_native_supported() { return nativeInstalled; }
void fps_limiter_native_update(bool enabled, int limit) {
    nativeRequested = enabled;
    if (!nativeInstalled) return;
    if (limit < 30) limit = 30;
    if (limit > 480) limit = 480;
    __atomic_store_n(&nativePacing->limit, static_cast<unsigned int>(limit), __ATOMIC_RELAXED);
    float interval = enabled ? 1000.0f / limit : 0.0f;
    unsigned int bits;
    __builtin_memcpy(&bits, &interval, sizeof(bits));
    __atomic_store_n(&nativePacing->intervalBits, bits, __ATOMIC_RELEASE);
}

long long fps_limiter_frame_timestamp_ns() {
    return __atomic_load_n(&frameTimestamp, __ATOMIC_RELAXED);
}

long long fps_limiter_frame_delta_ns() { return __atomic_load_n(&frameDelta, __ATOMIC_RELAXED); }

void fps_limiter_wait(bool enabled, bool gameplay, int limit) {
    auto nativeFrames = nativeInstalled ? __atomic_load_n(&nativePacing->frames, __ATOMIC_ACQUIRE) : 0;
    bool nativeObserved = nativeFrames != previousNativeFrames;
    previousNativeFrames = nativeFrames;
    if (!clockGetTime && !apiAttempted) {
        apiAttempted = true;
        if (loadAPI()) apiAttempted = false;
    }
    if (!clockGetTime) {
        if (enabled && gameplay) setError("FPS Limit: host timer unavailable");
        active = false;
        return;
    }
    long long current = now();
    if (!current) {
        if (enabled && gameplay) setError("FPS Limit: clock unavailable");
        active = false;
        return;
    }
    if (!enabled || !gameplay) { active = false; recordFrameTime(current); return; }
    if (nativeRequested && nativeInstalled) {
        // A callback without a native store does not prove a frame needs pacing.
        // Never add a late wait to an installed native-pacing experiment.
        active = false;
        setError(nativeObserved ? nullptr : "FPS Limit: native interval not observed; late wait skipped");
        recordFrameTime(current); return;
    }
    if (nativeRequested) setError("FPS Limit: native pacing unsupported; using standard pacing");
    long long interval = 1000000000LL / (limit < 30 ? 30 : limit > 480 ? 480 : limit);
    if (!active) { deadline = current; active = true; }
    deadline += interval;
    if (current - deadline > interval) deadline = current;
    while (current < deadline) {
        long long remaining = deadline - current;
        timespec sleep{static_cast<time_t>(remaining / 1000000000LL),
                       static_cast<long>(remaining % 1000000000LL)};
        nanoSleep(&sleep, nullptr);
        current = now();
        if (!current) { setError("FPS Limit: clock unavailable"); active = false; return; }
    }
    deadline = current > deadline ? current : deadline;
    recordFrameTime(current);
}
