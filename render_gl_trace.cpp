#include "render_gl_trace.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render::glTrace;
using DrawArrays = void (*)(unsigned int, int, int);
using DrawElements = void (*)(unsigned int, int, unsigned int, const void*);
using BufferData = void (*)(unsigned int, long, const void*, unsigned int);
using BufferSubData = void (*)(unsigned int, long, long, const void*);
constexpr unsigned int importCount = 4;
unsigned long originals[importCount];
RenderGlCounter counters[RenderGlOperationCount]{};
unsigned long long sequences[RenderGlOperationCount]{};
long long (*now)();
bool active, attempted;
bool installedSlots[importCount]{};
bool installDrawProc(unsigned long base);
RenderGlDrawObserver drawObserver;
bool consumeDraw() {
    auto observer = __atomic_load_n(&drawObserver, __ATOMIC_ACQUIRE);
    return observer && observer();
}
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
    if (count > 0 && consumeDraw()) return;
    auto start = begin(RenderGlDrawArrays, count > 0 ? count : 0);
    reinterpret_cast<DrawArrays>(originals[RenderGlDrawArrays])(mode, first, count);
    end(RenderGlDrawArrays, start);
}
void drawElements(unsigned int mode, int count, unsigned int type, const void* indices) {
    if (count > 0 && consumeDraw()) return;
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
namespace {
bool installSlots(unsigned long base, unsigned int first, unsigned int last) {
    if (!hooks::supported(base)) return false;
    hooks::Patch patches[RenderGlOperationCount]{};
    unsigned int count = 0;
    for (unsigned int i = first; i < last; ++i) {
        if (installedSlots[i]) continue;
        if (!hooks::readable(base, profile::slots[i], 8)
            || !hooks::readable(base, profile::plt[i], 16, true)
            || !hooks::matches(base, profile::plt[i], profile::pltSignatures[i], 6)) return false;
        auto pointer = *reinterpret_cast<const unsigned long*>(base + profile::slots[i]);
        if (!hooks::readable(pointer, 0, 1, true)) return false;
        if (pointer >= base + profile::pltBegin && pointer < base + profile::pltEnd) return false;
        for (unsigned int j = 0; j < importCount; ++j)
            if (pointer == replacements[j] || pointer == base + profile::plt[j]
                || pointer == base + profile::plt[j] + 6) return false;
        originals[i] = pointer;
        patches[count++] = {profile::slots[i], pointer, replacements[i]};
    }
    if (!count) return true;
    if (hooks::install(first == 0 ? "Render GL draws" : "Render GL buffers", base, patches, count) != hooks::InstallResult::Installed)
        return false;
    for (unsigned int i = first; i < last; ++i) installedSlots[i] = true;
    return true;
}
}
bool render_gl_trace_install(unsigned long base) {
    if (attempted || !now) return false;
    if (!installSlots(base, 0, 2) || !installSlots(base, 2, importCount) || !installDrawProc(base)) return false;
    attempted = true;
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

namespace {
using Proc = void (*)();
using GetProc = Proc (*)(const char*);
unsigned long procOriginal;
bool procInstalled;
unsigned long extensionOriginals[8]{};
const char* const extensionNames[] = {
    "glDrawArraysInstanced", "glDrawArraysInstancedOES",
    "glDrawElementsInstanced", "glDrawElementsInstancedOES",
    "glDrawArraysIndirect", "glDrawArraysIndirectEXT",
    "glDrawElementsIndirect", "glDrawElementsIndirectEXT"
};
using ArraysInstanced = void (*)(unsigned int, int, int, int);
using ElementsInstanced = void (*)(unsigned int, int, unsigned int, const void*, int);
using ArraysIndirect = void (*)(unsigned int, const void*);
using ElementsIndirect = void (*)(unsigned int, unsigned int, const void*);
template<unsigned int I> void arraysInstanced(unsigned int mode, int first, int count, int instances) {
    if (count > 0 && instances > 0 && consumeDraw()) return;
    auto start = begin(RenderGlDrawArraysInstanced, count > 0 && instances > 0
        ? static_cast<unsigned long long>(count) * instances : 0);
    reinterpret_cast<ArraysInstanced>(__atomic_load_n(&extensionOriginals[I], __ATOMIC_ACQUIRE))
        (mode, first, count, instances);
    end(RenderGlDrawArraysInstanced, start);
}
template<unsigned int I> void elementsInstanced(unsigned int mode, int count, unsigned int type,
                                               const void* indices, int instances) {
    if (count > 0 && instances > 0 && consumeDraw()) return;
    auto start = begin(RenderGlDrawElementsInstanced, count > 0 && instances > 0
        ? static_cast<unsigned long long>(count) * instances : 0);
    reinterpret_cast<ElementsInstanced>(__atomic_load_n(&extensionOriginals[I], __ATOMIC_ACQUIRE))
        (mode, count, type, indices, instances);
    end(RenderGlDrawElementsInstanced, start);
}
template<unsigned int I> void arraysIndirect(unsigned int mode, const void* indirect) {
    if (consumeDraw()) return;
    auto start = begin(RenderGlDrawArraysIndirect, 0);
    reinterpret_cast<ArraysIndirect>(__atomic_load_n(&extensionOriginals[I], __ATOMIC_ACQUIRE))(mode, indirect);
    end(RenderGlDrawArraysIndirect, start);
}
template<unsigned int I> void elementsIndirect(unsigned int mode, unsigned int type, const void* indirect) {
    if (consumeDraw()) return;
    auto start = begin(RenderGlDrawElementsIndirect, 0);
    reinterpret_cast<ElementsIndirect>(__atomic_load_n(&extensionOriginals[I], __ATOMIC_ACQUIRE))
        (mode, type, indirect);
    end(RenderGlDrawElementsIndirect, start);
}
const unsigned long extensionWrappers[] = {
    reinterpret_cast<unsigned long>(arraysInstanced<0>), reinterpret_cast<unsigned long>(arraysInstanced<1>),
    reinterpret_cast<unsigned long>(elementsInstanced<2>), reinterpret_cast<unsigned long>(elementsInstanced<3>),
    reinterpret_cast<unsigned long>(arraysIndirect<4>), reinterpret_cast<unsigned long>(arraysIndirect<5>),
    reinterpret_cast<unsigned long>(elementsIndirect<6>), reinterpret_cast<unsigned long>(elementsIndirect<7>)
};
bool sameName(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
Proc getDrawProc(const char* name) {
    auto original = reinterpret_cast<GetProc>(procOriginal)(name);
    if (!original) return original;
    auto address = reinterpret_cast<unsigned long>(original);
    for (auto wrapper : extensionWrappers) if (address == wrapper) return original;
    for (unsigned int i = 0; i < 8; ++i) {
        if (!sameName(name, extensionNames[i])) continue;
        unsigned long expected = 0;
        __atomic_compare_exchange_n(&extensionOriginals[i], &expected, address, false,
                                    __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&extensionOriginals[i], __ATOMIC_ACQUIRE) != address) return original;
        return reinterpret_cast<Proc>(extensionWrappers[i]);
    }
    return original;
}
bool installDrawProc(unsigned long base) {
    if (procInstalled) return true;
    namespace procProfile = minecraft_build::current::render::drawProc;
    if (!hooks::supported(base) || !hooks::readable(base, procProfile::slot, 8)
        || !hooks::readable(base, procProfile::plt, 16, true)
        || !hooks::matches(base, procProfile::plt, procProfile::pltSignature, 6)) return false;
    auto pointer = *reinterpret_cast<const unsigned long*>(base + procProfile::slot);
    if (!hooks::readable(pointer, 0, 1, true) || pointer == reinterpret_cast<unsigned long>(getDrawProc)
        || (pointer >= base + profile::pltBegin && pointer < base + profile::pltEnd)) return false;
    procOriginal = pointer;
    hooks::Patch patch{procProfile::slot, pointer, reinterpret_cast<unsigned long>(getDrawProc)};
    procInstalled = hooks::install("Render GL draw lookup", base, &patch, 1) == hooks::InstallResult::Installed;
    return procInstalled;
}
}
bool render_gl_trace_set_draw_observer(RenderGlDrawObserver observer) {
    if (!observer) {
        __atomic_store_n(&drawObserver, observer, __ATOMIC_RELEASE);
        return true;
    }
    if (!hooks::initialize()) return false;
    auto base = hooks::find_game();
    if (!installSlots(base, 0, 2) || !installDrawProc(base)) return false;
    __atomic_store_n(&drawObserver, observer, __ATOMIC_RELEASE);
    return true;
}
