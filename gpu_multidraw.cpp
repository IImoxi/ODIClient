#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include "gpu_multidraw.h"
#include "launcher_api.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render::gpuMultidraw;
using Arrays = void (*)(unsigned int, const void*, int, int);
using Elements = void (*)(unsigned int, unsigned int, const void*, int, int);
Arrays nativeArrays, multiArrays;
Elements nativeElements, multiElements;
decltype(&eglGetCurrentContext) currentContext;
decltype(&eglGetProcAddress) getProc;
decltype(&glGetString) getString;
decltype(&glGetStringi) getStringi;
decltype(&glGetIntegerv) getInteger;
EGLContext supportedContext;
bool enabled, installed, ready, available, attempted, capabilityChecked, eglAttempted;
const char* status = "Not initialized";
GpuMultidrawSnapshot counters{};
void setStatus(const char* value) { __atomic_store_n(&status, value, __ATOMIC_RELEASE); }
bool equal(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool canBatch(const void* offset, int count, int stride, int minimum) {
    if (!(count >= 4 && stride >= minimum && (stride & 3) == 0
        && (reinterpret_cast<unsigned long>(offset) & 3) == 0
        && __atomic_load_n(&ready, __ATOMIC_ACQUIRE)
        && currentContext() == supportedContext)) return false;
    GLint vao=0, indirect=0, feedback=0, index=1;
    getInteger(GL_VERTEX_ARRAY_BINDING, &vao);
    getInteger(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect);
    getInteger(GL_TRANSFORM_FEEDBACK_ACTIVE, &feedback);
    if (minimum == 20) getInteger(GL_ELEMENT_ARRAY_BUFFER_BINDING, &index);
    return vao != 0 && indirect != 0 && index != 0 && feedback == 0;
}
void record(bool elements, int count) {
    auto& batches = elements ? counters.element_batches : counters.array_batches;
    __atomic_fetch_add(&batches, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counters.commands, count, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counters.avoided_calls, count - 1, __ATOMIC_RELAXED);
}
void arrays(unsigned int mode, const void* offset, int count, int stride) {
    if (count <= 0) return; // Preserve native fallback's guard before its prologue.
    if (__atomic_load_n(&enabled, __ATOMIC_RELAXED)) {
        if (canBatch(offset, count, stride, 16)) {
            record(false, count); multiArrays(mode, offset, count, stride); return;
        }
        __atomic_fetch_add(&counters.fallback_batches, 1, __ATOMIC_RELAXED);
    }
    nativeArrays(mode, offset, count, stride);
}
void elements(unsigned int mode, unsigned int type, const void* offset, int count, int stride) {
    if (count <= 0) return;
    if (__atomic_load_n(&enabled, __ATOMIC_RELAXED)) {
        if (canBatch(offset, count, stride, 20)) {
            record(true, count); multiElements(mode, type, offset, count, stride); return;
        }
        __atomic_fetch_add(&counters.fallback_batches, 1, __ATOMIC_RELAXED);
    }
    nativeElements(mode, type, offset, count, stride);
}
void writeWord(unsigned char* destination, unsigned long word) {
    for (unsigned int i=0; i<8; ++i) destination[i] = word >> (i*8);
}
void absoluteJump(unsigned char* destination, unsigned long target) {
    destination[0] = 0xff; destination[1] = 0x25;
    for (unsigned int i=2; i<6; ++i) destination[i] = 0;
    writeWord(destination+6, target);
}
}

bool client_gpu_multidraw_enabled() { return __atomic_load_n(&enabled, __ATOMIC_RELAXED); }
void client_set_gpu_multidraw(bool value) { __atomic_store_n(&enabled, value, __ATOMIC_RELAXED); }
bool gpu_multidraw_available() { return __atomic_load_n(&available, __ATOMIC_ACQUIRE); }
const char* gpu_multidraw_status() { return __atomic_load_n(&status, __ATOMIC_ACQUIRE); }

bool gpu_multidraw_install(unsigned long base) {
    if (attempted || !hooks::supported(base)) return false;
    for (unsigned int i=0; i<2; ++i) {
        if (!hooks::readable(base, profile::fallback[i], profile::fallbackSizes[i], true)
            || !hooks::matches(base, profile::fallback[i], profile::fallbackSignatures[i], profile::fallbackSizes[i])
            || !hooks::readable(base, profile::singleSlots[i], 8)) return false;
    }
    attempted = true;
    auto size = hooks::page_size();
    auto relay = hooks::allocate_near(base + profile::fallback[0], 1);
    if (!relay) return false;
    hooks::Patch patches[2]{};
    const unsigned long wrappers[] = {reinterpret_cast<unsigned long>(arrays), reinterpret_cast<unsigned long>(elements)};
    for (unsigned int i=0; i<2; ++i) {
        auto entry = relay + i*64;
        absoluteJump(entry, wrappers[i]);
        // Both stolen prologues are push rbp; mov rsp,rbp. The count guards live in wrappers.
        for (unsigned int j=0; j<4; ++j) entry[16+j] = profile::fallbackSignatures[i][4+j];
        absoluteJump(entry+20, base + profile::fallback[i] + 8);
        auto delta = reinterpret_cast<unsigned long>(entry) - (base + profile::fallback[i] + 5);
        if (static_cast<long>(delta) < -2147483648L || static_cast<long>(delta) > 2147483647L) {
            hooks::release(relay, size); return false;
        }
        unsigned char jump[8] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        for (unsigned int j=0; j<4; ++j) jump[1+j] = delta >> (j*8);
        unsigned long original=0, replacement=0;
        for (unsigned int j=0; j<8; ++j) {
            original |= static_cast<unsigned long>(profile::fallbackSignatures[i][j]) << (j*8);
            replacement |= static_cast<unsigned long>(jump[j]) << (j*8);
        }
        patches[i] = {profile::fallback[i], original, replacement};
    }
    nativeArrays = reinterpret_cast<Arrays>(relay+16);
    nativeElements = reinterpret_cast<Elements>(relay+80);
    if (!hooks::make_executable(relay, size)) { hooks::release(relay, size); return false; }
    auto result = hooks::install("GPU multidraw", base, patches, 2);
    if (result != hooks::InstallResult::Installed) {
        if (result != hooks::InstallResult::Retained) hooks::release(relay, size);
        setStatus("Native hook unavailable"); return false;
    }
    installed = true;
    setStatus("Waiting for graphics context");
    return true;
}
void gpu_multidraw_init() {
    if (!hooks::initialize() || !gpu_multidraw_install(hooks::find_game())) setStatus("Native hook unavailable");
}
void gpu_multidraw_frame() {
    if (!installed) return;
    if (!eglAttempted) {
        eglAttempted = true;
        auto library = mcpelauncher_host_dlopen("libEGL.so.1", 2);
        if (!library) { setStatus("EGL unavailable"); return; }
        currentContext = reinterpret_cast<decltype(currentContext)>(mcpelauncher_host_dlsym(library, "eglGetCurrentContext"));
        getProc = reinterpret_cast<decltype(getProc)>(mcpelauncher_host_dlsym(library, "eglGetProcAddress"));
        if (!currentContext || !getProc) { setStatus("EGL entry points unavailable"); return; }
    }
    if (!currentContext || !getProc) return;
    auto context = currentContext();
    if (context == EGL_NO_CONTEXT) { __atomic_store_n(&available, false, __ATOMIC_RELEASE); return; }
    if (capabilityChecked) {
        if (ready) {
            __atomic_store_n(&available, context == supportedContext, __ATOMIC_RELEASE);
            setStatus(context == supportedContext ? "GLES multidraw available" : "Graphics context changed; native fallback");
        }
        return;
    }
    getString = reinterpret_cast<decltype(getString)>(getProc("glGetString"));
    getStringi = reinterpret_cast<decltype(getStringi)>(getProc("glGetStringi"));
    getInteger = reinterpret_cast<decltype(getInteger)>(getProc("glGetIntegerv"));
    if (!getString || !getStringi || !getInteger) { setStatus("GL queries unavailable"); capabilityChecked = true; return; }
    auto version = reinterpret_cast<const char*>(getString(GL_VERSION));
    const char prefix[] = "OpenGL ES ";
    unsigned int i=0;
    while (version && prefix[i] && version[i] == prefix[i]) ++i;
    if (!version || prefix[i] || version[i] != '3' || version[i+1] != '.' || version[i+2] < '1' || version[i+2] > '9') {
        capabilityChecked = true; setStatus("Needs GLES 3.1+; native fallback"); return;
    }
    GLint count=0; getInteger(GL_NUM_EXTENSIONS, &count);
    bool supported=false;
    for (GLint extension=0; extension<count && extension<16384; ++extension)
        if (equal(reinterpret_cast<const char*>(getStringi(GL_EXTENSIONS, extension)), "GL_EXT_multi_draw_indirect")) supported=true;
    if (!supported) { capabilityChecked = true; setStatus("Missing EXT_multi_draw_indirect; native fallback"); return; }
    multiArrays = reinterpret_cast<Arrays>(getProc("glMultiDrawArraysIndirectEXT"));
    multiElements = reinterpret_cast<Elements>(getProc("glMultiDrawElementsIndirectEXT"));
    capabilityChecked = true;
    if (!multiArrays || !multiElements) { setStatus("Multidraw entry points unavailable; native fallback"); return; }
    supportedContext = context;
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
    __atomic_store_n(&available, true, __ATOMIC_RELEASE);
    setStatus("GLES multidraw available");
}
GpuMultidrawSnapshot gpu_multidraw_snapshot() {
    GpuMultidrawSnapshot result{};
#define DRAIN(name) result.name = __atomic_exchange_n(&counters.name, 0, __ATOMIC_RELAXED)
    DRAIN(array_batches); DRAIN(element_batches); DRAIN(commands); DRAIN(avoided_calls); DRAIN(fallback_batches);
#undef DRAIN
    return result;
}
