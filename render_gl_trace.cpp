#include "render_gl_trace.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render::glTrace;
using DrawArrays = void (*)(unsigned int, int, int);
using DrawElements = void (*)(unsigned int, int, unsigned int, const void*);
using BufferData = void (*)(unsigned int, long, const void*, unsigned int);
using BufferSubData = void (*)(unsigned int, long, long, const void*);
unsigned long originals[RenderGlOperationCount];
RenderGlCounter counters[RenderGlOperationCount]{};
unsigned long long sequences[RenderGlOperationCount]{};
long long (*now)();
bool active, attempted;
long long begin(RenderGlOperation operation, unsigned long long units) {
    if (!__atomic_load_n(&active, __ATOMIC_ACQUIRE)) return 0;
    auto& counter = counters[operation];
    __atomic_fetch_add(&counter.calls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counter.units, units, __ATOMIC_RELAXED);
    auto sequence = __atomic_fetch_add(&sequences[operation], 1, __ATOMIC_RELAXED);
    return (sequence & 63) == 0 ? now() : 0;
}
void end(RenderGlOperation operation, long long start) {
    if (start <= 0) return;
    auto finish = now();
    if (finish < start) return;
    auto duration = static_cast<unsigned long long>(finish - start);
    auto& counter = counters[operation];
    __atomic_fetch_add(&counter.samples, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counter.sampleNs, duration, __ATOMIC_RELAXED);
    auto maximum = __atomic_load_n(&counter.sampleMaxNs, __ATOMIC_RELAXED);
    while (duration > maximum && !__atomic_compare_exchange_n(&counter.sampleMaxNs,
        &maximum, duration, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
void drawArrays(unsigned int mode, int first, int count) {
    auto start = begin(RenderGlDrawArrays, count > 0 ? count : 0);
    reinterpret_cast<DrawArrays>(originals[RenderGlDrawArrays])(mode, first, count);
    end(RenderGlDrawArrays, start);
}
void drawElements(unsigned int mode, int count, unsigned int type, const void* indices) {
    auto start = begin(RenderGlDrawElements, count > 0 ? count : 0);
    reinterpret_cast<DrawElements>(originals[RenderGlDrawElements])(mode, count, type, indices);
    end(RenderGlDrawElements, start);
}
void bufferData(unsigned int target, long size, const void* data, unsigned int usage) {
    auto start = begin(RenderGlBufferData, size > 0 ? size : 0);
    reinterpret_cast<BufferData>(originals[RenderGlBufferData])(target, size, data, usage);
    end(RenderGlBufferData, start);
}
void bufferSubData(unsigned int target, long offset, long size, const void* data) {
    auto start = begin(RenderGlBufferSubData, size > 0 ? size : 0);
    reinterpret_cast<BufferSubData>(originals[RenderGlBufferSubData])(target, offset, size, data);
    end(RenderGlBufferSubData, start);
}
const unsigned long replacements[] = {reinterpret_cast<unsigned long>(drawArrays),
    reinterpret_cast<unsigned long>(drawElements), reinterpret_cast<unsigned long>(bufferData),
    reinterpret_cast<unsigned long>(bufferSubData)};
}
void render_gl_trace_configure(long long (*clock)()) { if (!attempted) now = clock; }
void render_gl_trace_disable() { __atomic_store_n(&active, false, __ATOMIC_RELEASE); }
bool render_gl_trace_install(unsigned long base) {
    if (attempted || !now || !hooks::supported(base)) return false;
    hooks::Patch patches[RenderGlOperationCount]{};
    for (unsigned int i = 0; i < RenderGlOperationCount; ++i) {
        if (!hooks::readable(base, profile::slots[i], 8)
            || !hooks::readable(base, profile::plt[i], 16, true)
            || !hooks::matches(base, profile::plt[i], profile::pltSignatures[i], 6)) return false;
        auto pointer = *reinterpret_cast<const unsigned long*>(base + profile::slots[i]);
        if (!hooks::readable(pointer, 0, 1, true)) return false;
        if (pointer >= base + profile::pltBegin && pointer < base + profile::pltEnd) return false;
        for (unsigned int j = 0; j < RenderGlOperationCount; ++j)
            if (pointer == replacements[j] || pointer == base + profile::plt[j]
                || pointer == base + profile::plt[j] + 6) return false;
        originals[i] = pointer;
        patches[i] = {profile::slots[i], pointer, replacements[i]};
    }
    attempted = true;
    auto result = hooks::install("Render GL trace", base, patches, RenderGlOperationCount);
    // Retained slots still forward through permanent wrappers/originals, without sampling.
    if (result != hooks::InstallResult::Installed) return false;
    __atomic_store_n(&active, true, __ATOMIC_RELEASE);
    return true;
}
RenderGlSnapshot render_gl_trace_snapshot() {
    RenderGlSnapshot result{};
    for (unsigned int i = 0; i < RenderGlOperationCount; ++i) {
        auto& source = counters[i];
        auto& destination = result.operations[i];
        destination.calls = __atomic_exchange_n(&source.calls, 0, __ATOMIC_RELAXED);
        destination.units = __atomic_exchange_n(&source.units, 0, __ATOMIC_RELAXED);
        destination.samples = __atomic_exchange_n(&source.samples, 0, __ATOMIC_RELAXED);
        destination.sampleNs = __atomic_exchange_n(&source.sampleNs, 0, __ATOMIC_RELAXED);
        destination.sampleMaxNs = __atomic_exchange_n(&source.sampleMaxNs, 0, __ATOMIC_RELAXED);
    }
    return result;
}
