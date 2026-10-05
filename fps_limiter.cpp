#include <time.h>
#include "launcher_api.h"
#include "fps_limiter.h"

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

long long fps_limiter_frame_timestamp_ns() {
    return __atomic_load_n(&frameTimestamp, __ATOMIC_RELAXED);
}

long long fps_limiter_frame_delta_ns() { return __atomic_load_n(&frameDelta, __ATOMIC_RELAXED); }

void fps_limiter_wait(bool enabled, bool gameplay, int limit) {
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
