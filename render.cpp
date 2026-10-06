#include "render.h"
#include "render_frame_trace.h"
#include "gpu_multidraw.h"
#include "gpu_uniform_cache.h"
#include "render_gl_trace.h"
#include "client_modules.h"
#include "client_settings.h"
#include "minecraft_build.h"
#include "hook_manager.h"
#include "launcher_api.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
namespace profile = minecraft_build::current::render;
using ListCallback = void (*)(void*, const void*);
ListCallback original;
bool ready;
const char* error = "Render: initializing";
bool tracing;
int traceFd = -1;
int (*traceClock)(clockid_t, timespec*);
ssize_t (*traceWrite)(int, const void*, size_t);
int (*traceFormat)(char*, size_t, const char*, ...);
unsigned long long traceCalls, traceNs, traceMaxNs;
long long traceStart;
unsigned long long traceFrames, traceFrameNs, traceFrameMaxNs;
struct TraceTiming { unsigned long long calls, ns, maximum; };
TraceTiming preparationTiming;
unsigned long long preparationCpuNs, traceSectionsProduced, traceSectionsKept;
using PrepareLists = void (*)(void*, const void*, const void*, float, bool);
PrepareLists prepareOriginal;
bool preparationReady;
bool installPreparationTrace(unsigned long base);
void recordTiming(TraceTiming& target, long long start, long long finish) {
    if (start <= 0 || finish < start) return;
    auto duration = static_cast<unsigned long long>(finish-start);
    __atomic_fetch_add(&target.calls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&target.ns, duration, __ATOMIC_RELAXED);
    auto maximum = __atomic_load_n(&target.maximum, __ATOMIC_RELAXED);
    while (duration > maximum && !__atomic_compare_exchange_n(&target.maximum, &maximum,
        duration, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
long long traceNow() {
    timespec value{};
    return traceClock && !traceClock(CLOCK_MONOTONIC, &value)
        ? static_cast<long long>(value.tv_sec) * 1000000000LL + value.tv_nsec : 0;
}
long long traceThreadNow() {
    timespec value{};
    return traceClock && !traceClock(CLOCK_THREAD_CPUTIME_ID, &value)
        ? static_cast<long long>(value.tv_sec) * 1000000000LL + value.tv_nsec : 0;
}
void initTrace(unsigned long base) {
    auto libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) return;
    auto environment = reinterpret_cast<char* (*)(const char*)>(mcpelauncher_host_dlsym(libc, "getenv"));
    const char* path = environment ? environment("ODI_TERRAIN_TRACE") : nullptr;
    if (!path || path[0] != '/') return;
    auto openFile = reinterpret_cast<int (*)(const char*, int, ...)>(mcpelauncher_host_dlsym(libc, "open"));
    traceClock = reinterpret_cast<decltype(traceClock)>(mcpelauncher_host_dlsym(libc, "clock_gettime"));
    traceWrite = reinterpret_cast<decltype(traceWrite)>(mcpelauncher_host_dlsym(libc, "write"));
    traceFormat = reinterpret_cast<decltype(traceFormat)>(mcpelauncher_host_dlsym(libc, "snprintf"));
    if (!openFile || !traceClock || !traceWrite || !traceFormat) return;
    traceFd = openFile(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    traceStart = traceNow();
    if (traceFd < 0 || !traceStart) return;
    constexpr char header[] = "# ODIClient terrain trace v6; new session; CPU/API times, not GPU execution times\n"
        "monotonic_ns,window_ms,callbacks,callback_total_ms,callback_max_ms,frames,frame_avg_ms,frame_max_ms,render_options,"
        "sections_produced,sections_kept,prepare_calls,prepare_wall_total_ms,prepare_wall_max_ms,prepare_thread_cpu_ms,"
        "draw_arrays_calls,draw_arrays_vertices,draw_arrays_samples,draw_arrays_sample_total_ms,draw_arrays_sample_max_ms,"
        "draw_elements_calls,draw_elements_indices,draw_elements_samples,draw_elements_sample_total_ms,draw_elements_sample_max_ms,"
        "buffer_data_calls,buffer_data_requested_bytes,buffer_data_samples,buffer_data_sample_total_ms,buffer_data_sample_max_ms,"
        "buffer_sub_data_calls,buffer_sub_data_requested_bytes,buffer_sub_data_samples,buffer_sub_data_sample_total_ms,buffer_sub_data_sample_max_ms,"
        "gpu_multidraw_enabled,gpu_multidraw_available,gpu_array_batches,gpu_element_batches,gpu_commands,gpu_avoided_calls,gpu_fallback_batches,gpu_status,"
        "uniform_cache_enabled,uniform_cache_available,uniform_calls,uniform_skipped_calls,uniform_skipped_bytes,uniform_status,uniform_eligible_calls,uniform_owner_rejected_calls,uniform_uncacheable_calls,"
        "outside_callback_calls,outside_callback_wall_total_ms,outside_callback_wall_max_ms,outside_callback_thread_cpu_ms,"
        "client_callback_calls,client_callback_wall_total_ms,client_callback_wall_max_ms,client_callback_thread_cpu_ms,"
        "limiter_calls,limiter_wall_total_ms,limiter_wall_max_ms,limiter_thread_cpu_ms,"
        "overlays_calls,overlays_wall_total_ms,overlays_wall_max_ms,overlays_thread_cpu_ms,"
        "native_submit_calls,native_submit_wall_total_ms,native_submit_wall_max_ms,native_submit_thread_cpu_ms,"
        "present_calls,present_wall_total_ms,present_wall_max_ms,present_thread_cpu_ms,frame_gap_resets\n";
    if (traceWrite(traceFd, header, sizeof(header)-1) != sizeof(header)-1) return;
    render_gl_trace_configure(traceNow);
    bool gl = render_gl_trace_install(base);
    bool prepare = installPreparationTrace(base);
    render_frame_trace_configure(traceNow,traceThreadNow);
    auto frame = render_frame_trace_install(base);
    char status[256];
    int size = traceFormat(status, sizeof(status), "# prepare_hook=%u gl_import_hooks=%u gl_timing_sample_stride=64 frame_submit_hook=%u egl_present_hook=%u; nested timings overlap\n", unsigned(prepare), unsigned(gl), unsigned(frame.submit), unsigned(frame.present));
    if (size <= 0 || size >= int(sizeof(status)) || traceWrite(traceFd, status, size) != size) {
        render_gl_trace_disable();
        render_frame_trace_disable();
        return;
    }
    gpu_uniform_cache_trace_enable(true);
    __atomic_store_n(&tracing, true, __ATOMIC_RELEASE);
}
// One atomic snapshot per callback, including all distances.
constexpr unsigned int belowShift = 8, aboveShift = 17, radiusShift = 26;
constexpr unsigned long long distanceMask = 511;
unsigned long long options = (64ull << belowShift) | (128ull << aboveShift) | (128ull << radiusShift);
struct ChunkPosition { int x, y, z; };
struct PositionList { ChunkPosition *begin, *end, *capacity; };
static_assert(sizeof(ChunkPosition) == profile::positionSize);
static_assert(sizeof(PositionList) == profile::vectorSize);

template<class T> T field(const void* object, unsigned long offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object) + offset);
}
unsigned long long snapshot() { return __atomic_load_n(&options, __ATOMIC_ACQUIRE); }
RenderSettings settings(unsigned long long value) {
    return {bool(value & 1), bool(value & 2), bool(value & 4),
            int((value >> belowShift) & distanceMask), int((value >> aboveShift) & distanceMask), bool(value & 8),
            int((value >> radiusShift) & distanceMask)};
}
int distance(int value) { return value < 16 ? 16 : value > 256 ? 256 : value; }
void change(unsigned long long mask, unsigned long long value) {
    unsigned long long old = snapshot(), next;
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
void callNativeList(void* closure, const void* results) {
    if (!__atomic_load_n(&tracing, __ATOMIC_ACQUIRE) || !closure
        || !__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
        original(closure, results);
        return;
    }
    auto list = outputList(closure);
    unsigned long before = 0, after = 0;
    bool valid = count(list, before);
    original(closure, results);
    if (valid && count(list, after) && after >= before)
        __atomic_fetch_add(&traceSectionsProduced, after-before, __ATOMIC_RELAXED);
}
void processRenderList(void* closure, const void* results) {
    unsigned long long value = snapshot();
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE) || !(value & 1) || !(value & 14) || !closure) {
        callNativeList(closure, results);
        return;
    }
    auto list = outputList(closure);
    auto position = field<const float*>(closure, profile::closurePosition);
    unsigned long previous = 0;
    bool valid = count(list, previous) && position && position[1] > -30000000.0f
        && position[1] < 30000000.0f; // Also rejects NaN/infinity.
    if (valid && (value & 8)) valid = position[0] > -30000000.0f && position[0] < 30000000.0f
        && position[2] > -30000000.0f && position[2] < 30000000.0f;
    float cameraX = valid ? position[0] : 0.0f;
    float cameraY = valid ? position[1] : 0.0f;
    float cameraZ = valid ? position[2] : 0.0f;

    // Preserve frustum checks, dirty-section bookkeeping, and every native side effect.
    callNativeList(closure, results);
    unsigned long total;
    if (!valid || !count(list, total) || total < previous || total == previous) return;
    auto input = list->begin + previous;
    auto output = input;
    float lower = cameraY - ((value >> belowShift) & distanceMask);
    float upper = cameraY + ((value >> aboveShift) & distanceMask);
    float radius = (value >> radiusShift) & distanceMask;
    for (; input != list->end; ++input) {
        float bottom = static_cast<float>(input->y) * profile::sectionSize;
        float top = bottom + profile::sectionSize;
        if (((value & 2) && top <= lower) || ((value & 4) && bottom >= upper)) continue;
        if (value & 8) {
            // Distance to the nearest point of the section's horizontal bounds:
            // retain every section intersecting the radius, including corners.
            float x = static_cast<float>(input->x) * profile::sectionSize;
            float z = static_cast<float>(input->z) * profile::sectionSize;
            float dx = cameraX < x ? x - cameraX : cameraX > x + profile::sectionSize
                ? cameraX - (x + profile::sectionSize) : 0.0f;
            float dz = cameraZ < z ? z - cameraZ : cameraZ > z + profile::sectionSize
                ? cameraZ - (z + profile::sectionSize) : 0.0f;
            if (dx * dx + dz * dz > radius * radius) continue;
        }
        if (output != input) *output = *input;
        ++output;
    }
    // Only the suffix produced by this camera is filtered. Native code merges
    // multiple camera lists and creates matching visibility masks afterward.
    list->end = output;
}
void renderList(void* closure, const void* results) {
    if (!__atomic_load_n(&tracing, __ATOMIC_ACQUIRE)) {
        processRenderList(closure, results);
        return;
    }
    auto start = traceNow();
    auto list = closure && __atomic_load_n(&ready, __ATOMIC_ACQUIRE) ? outputList(closure) : nullptr;
    unsigned long before = 0, after = 0;
    bool valid = count(list, before);
    processRenderList(closure, results);
    auto end = traceNow();
    if (valid && count(list, after) && after >= before)
        __atomic_fetch_add(&traceSectionsKept, after-before, __ATOMIC_RELAXED);
    if (!start || end < start) return;
    auto duration = static_cast<unsigned long long>(end - start);
    __atomic_fetch_add(&traceCalls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&traceNs, duration, __ATOMIC_RELAXED);
    auto maximum = __atomic_load_n(&traceMaxNs, __ATOMIC_RELAXED);
    while (duration > maximum && !__atomic_compare_exchange_n(&traceMaxNs, &maximum, duration,
            false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
void prepareLists(void* builder, const void* position, const void* direction, float value, bool flag) {
    if (!__atomic_load_n(&tracing, __ATOMIC_ACQUIRE)
        || !__atomic_load_n(&preparationReady, __ATOMIC_ACQUIRE)) {
        prepareOriginal(builder, position, direction, value, flag);
        return;
    }
    auto start = traceNow();
    auto cpuStart = traceThreadNow();
    prepareOriginal(builder, position, direction, value, flag);
    auto cpuFinish = traceThreadNow();
    auto finish = traceNow();
    recordTiming(preparationTiming, start, finish);
    if (cpuStart > 0 && cpuFinish >= cpuStart)
        __atomic_fetch_add(&preparationCpuNs, cpuFinish-cpuStart, __ATOMIC_RELAXED);
}
bool installPreparationTrace(unsigned long base) {
    if (!hooks::supported(base)
        || !hooks::readable(base, profile::prepareFunction, sizeof(profile::prepareFunctionSignature), true)
        || !hooks::readable(base, profile::prepareCall, sizeof(profile::prepareCallSignature), true)
        || !hooks::matches(base, profile::prepareFunction, profile::prepareFunctionSignature, sizeof(profile::prepareFunctionSignature))
        || !hooks::matches(base, profile::prepareArguments, profile::prepareArgumentsSignature, sizeof(profile::prepareArgumentsSignature))
        || !hooks::matches(base, profile::prepareCall, profile::prepareCallSignature, sizeof(profile::prepareCallSignature))) return false;
    auto size = hooks::page_size();
    auto relay = hooks::allocate_near(base + profile::prepareCall, 1);
    if (!relay) return false;
    // Tail jump preserves the call site's return address and observed register ABI.
    relay[0] = 0xff; relay[1] = 0x25;
    for (int i = 2; i < 6; ++i) relay[i] = 0;
    *reinterpret_cast<unsigned long*>(relay+6) = reinterpret_cast<unsigned long>(prepareLists);
    auto displacement = reinterpret_cast<unsigned long>(relay) - (base + profile::prepareCall + 5);
    long delta = static_cast<long>(displacement);
    if (delta < -2147483648L || delta > 2147483647L || !hooks::make_executable(relay, size)) {
        hooks::release(relay, size);
        return false;
    }
    prepareOriginal = reinterpret_cast<PrepareLists>(base + profile::prepareFunction);
    auto expected = *reinterpret_cast<const unsigned long*>(base + profile::prepareCall);
    auto replacement = expected;
    auto bytes = reinterpret_cast<unsigned char*>(&replacement);
    for (int i = 0; i < 4; ++i) bytes[i+1] = static_cast<unsigned int>(delta) >> (i*8);
    const hooks::Patch patch{profile::prepareCall, expected, replacement};
    auto result = hooks::install("Render preparation trace", base, &patch, 1);
    if (result == hooks::InstallResult::Installed) {
        __atomic_store_n(&preparationReady, true, __ATOMIC_RELEASE);
        return true;
    }
    if (result != hooks::InstallResult::Retained) hooks::release(relay, size);
    return false;
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
        | (static_cast<unsigned long long>(distance(saved.aboveDistance)) << aboveShift)
        | (static_cast<unsigned long long>(distance(saved.radius)) << radiusShift)
        | (unsigned(saved.horizontal) << 3), __ATOMIC_RELEASE);
    const char* failure = "Render unavailable: hook manager";
    if (hooks::initialize()) {
        auto base = hooks::find_game();
        failure = "Render unavailable: unsupported build or chunk-list ABI";
        if (base && install(base)) {
            initTrace(base);
            __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
            failure = nullptr;
        }
    }
    __atomic_store_n(&error, failure, __ATOMIC_RELEASE);
}
const char* render_error() { return __atomic_load_n(&error, __ATOMIC_ACQUIRE); }
void render_trace_frame(long long frameDeltaNs) {
    if (!__atomic_load_n(&tracing, __ATOMIC_ACQUIRE)) return;
    if (frameDeltaNs > 0) {
        ++traceFrames;
        traceFrameNs += frameDeltaNs;
        if (static_cast<unsigned long long>(frameDeltaNs) > traceFrameMaxNs) traceFrameMaxNs = frameDeltaNs;
    }
    auto now = traceNow();
    if (now - traceStart < 1000000000LL) return;
    // Counters are approximate at window boundaries when callbacks run concurrently.
    auto calls = __atomic_exchange_n(&traceCalls, 0, __ATOMIC_RELAXED);
    auto total = __atomic_exchange_n(&traceNs, 0, __ATOMIC_RELAXED);
    auto maximum = __atomic_exchange_n(&traceMaxNs, 0, __ATOMIC_RELAXED);
    auto produced = __atomic_exchange_n(&traceSectionsProduced, 0, __ATOMIC_RELAXED);
    auto kept = __atomic_exchange_n(&traceSectionsKept, 0, __ATOMIC_RELAXED);
    auto prepareCalls = __atomic_exchange_n(&preparationTiming.calls, 0, __ATOMIC_RELAXED);
    auto prepareTotal = __atomic_exchange_n(&preparationTiming.ns, 0, __ATOMIC_RELAXED);
    auto prepareMaximum = __atomic_exchange_n(&preparationTiming.maximum, 0, __ATOMIC_RELAXED);
    auto prepareCpu = __atomic_exchange_n(&preparationCpuNs, 0, __ATOMIC_RELAXED);
    auto gl = render_gl_trace_snapshot();
    char line[3072];
    int size = traceFormat(line, sizeof(line), "%lld,%.3f,%llu,%.6f,%.6f,%llu,%.6f,%.6f,%u,%llu,%llu,%llu,%.6f,%.6f,%.6f",
        now, (now-traceStart)/1000000.0, calls, total/1000000.0, maximum/1000000.0,
        traceFrames, traceFrames ? traceFrameNs/1000000.0/traceFrames : 0.0,
        traceFrameMaxNs/1000000.0, unsigned(snapshot() & 15), produced, kept, prepareCalls,
        prepareTotal/1000000.0, prepareMaximum/1000000.0, prepareCpu/1000000.0);
    for (auto& operation : gl.operations) {
        if (size <= 0 || size >= int(sizeof(line))) break;
        int added = traceFormat(line+size, sizeof(line)-size, ",%llu,%llu,%llu,%.6f,%.6f",
            operation.calls, operation.units, operation.samples, operation.sampleNs/1000000.0,
            operation.sampleMaxNs/1000000.0);
        if (added <= 0 || added >= int(sizeof(line))-size) { size = 0; break; }
        size += added;
    }
    auto gpu = gpu_multidraw_snapshot();
    if (size > 0 && size < int(sizeof(line))) {
        int added = traceFormat(line+size, sizeof(line)-size, ",%u,%u,%llu,%llu,%llu,%llu,%llu,%s",
            unsigned(client_gpu_multidraw_enabled()), unsigned(gpu_multidraw_available()),
            gpu.array_batches, gpu.element_batches, gpu.commands, gpu.avoided_calls, gpu.fallback_batches, gpu_multidraw_status());
        if (added <= 0 || added >= int(sizeof(line))-size) size = 0;
        else size += added;
    }
    auto uniform = gpu_uniform_cache_snapshot();
    if (size > 0 && size < int(sizeof(line))) {
        int added = traceFormat(line+size, sizeof(line)-size, ",%u,%u,%llu,%llu,%llu,%s,%llu,%llu,%llu",
            unsigned(client_uniform_cache_enabled()), unsigned(gpu_uniform_cache_available()),
            uniform.calls, uniform.skipped, uniform.skipped_bytes, gpu_uniform_cache_status(),
            uniform.eligible, uniform.owner_rejected, uniform.uncacheable);
        if (added <= 0 || added >= int(sizeof(line))-size) size = 0;
        else size += added;
    }
    auto frame = render_frame_trace_snapshot();
    for (auto& stage : frame.stages) {
        if (size <= 0 || size >= int(sizeof(line))) break;
        int added = traceFormat(line+size, sizeof(line)-size, ",%llu,%.6f,%.6f,%.6f",
            stage.calls, stage.wallNs/1000000.0, stage.wallMaxNs/1000000.0, stage.cpuNs/1000000.0);
        if (added <= 0 || added >= int(sizeof(line))-size) { size = 0; break; }
        size += added;
    }
    if (size > 0 && size < int(sizeof(line))) {
        int added = traceFormat(line+size, sizeof(line)-size, ",%llu", frame.gapResets);
        if (added <= 0 || added >= int(sizeof(line))-size) size = 0;
        else size += added;
    }
    if (size > 0 && size < int(sizeof(line))-1) line[size++] = '\n';
    else size = 0;
    if (!size || traceWrite(traceFd, line, size) != size) {
        render_frame_trace_disable();
        gpu_uniform_cache_trace_enable(false);
        __atomic_store_n(&tracing, false, __ATOMIC_RELEASE);
        render_gl_trace_disable();
    }
    traceStart = now;
    traceFrames = traceFrameNs = traceFrameMaxNs = 0;
}
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

void client_set_render_horizontal(bool value) { change(8, unsigned(value) << 3); }
bool client_render_horizontal() { return snapshot() & 8; }
void client_set_render_radius(int value) { change(distanceMask << radiusShift, static_cast<unsigned long long>(distance(value)) << radiusShift); }
int client_render_radius() { return (snapshot() >> radiusShift) & distanceMask; }
