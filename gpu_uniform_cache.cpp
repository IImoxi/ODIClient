#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "gpu_uniform_cache.h"
#include "launcher_api.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render::uniformCache;

using Vector = void (*)(GLint, GLsizei, const GLfloat*);
using Scalar = void (*)(GLint, GLint);
using IntegerArray = void (*)(GLint, GLsizei, const GLint*);
using Program = void (*)(GLuint);
using MakeCurrent = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
unsigned long originals[9];
decltype(&eglGetCurrentContext) getContext;
decltype(&glGetIntegerv) getInteger;
unsigned long (*threadSelf)();
bool enabled, tracing, installed, ready, attempted, adapterAttempted, ownerEstablished, contextValid;
unsigned long ownerThread, ownerContext;
unsigned long long epoch=1, cacheEpoch;
GLint currentProgram;
bool programKnown;
const char* status="Not initialized";
GpuUniformCacheSnapshot counters{};
GpuUniformCacheSnapshot foreignCounters{};
struct Entry { GLint location; unsigned int opcode, size; unsigned int words[4]; bool valid; };
Entry entries[64]{}; // Only the established frame thread reads/writes these entries.
void invalidate() { __atomic_fetch_add(&epoch, 1, __ATOMIC_ACQ_REL); }
void resetOwnerCache(unsigned long long generation) {
    for (auto& entry: entries) entry.valid=false;
    programKnown=false; currentProgram=0; cacheEpoch=generation;
}
void setStatus(const char* value) { __atomic_store_n(&status, value, __ATOMIC_RELEASE); }
bool ownerThreadMatches() {
    return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)
        && threadSelf()==__atomic_load_n(&ownerThread,__ATOMIC_ACQUIRE);
}
void addCounter(bool own, unsigned long long& local, unsigned long long& foreign, unsigned long long amount=1) {
    if (own) local+=amount;
    else __atomic_fetch_add(&foreign,amount,__ATOMIC_RELAXED);
}
unsigned int wordAt(const void* value, unsigned int index) {
    unsigned int word;
    __builtin_memcpy(&word, static_cast<const unsigned char*>(value)+index*4, sizeof(word));
    return word;
}
// Epoch changes make other-thread calls and lifecycle operations invalidate the owner's cache.
// Values are copied only after native submission; invalid native calls are never intentionally generated.
bool prepare(GLint location, GLsizei count, const void* value,
             unsigned int opcode, unsigned int size, unsigned long long& generation) {
    generation=0;
    bool active=__atomic_load_n(&enabled,__ATOMIC_RELAXED);
    bool trace=__atomic_load_n(&tracing,__ATOMIC_RELAXED);
    if (!active && !trace) return false;
    bool own=ownerThreadMatches();
    if (trace) addCounter(own,counters.calls,foreignCounters.calls);
    if (!active) return false;
    if (!own || !__atomic_load_n(&contextValid,__ATOMIC_ACQUIRE)) {
        if (trace) addCounter(own,counters.owner_rejected,foreignCounters.owner_rejected);
        invalidate(); return false;
    }
    if (location<0 || count!=1 || !value) {
        if (trace) ++counters.uncacheable;
        invalidate(); generation=0; return false;
    }
    generation=__atomic_load_n(&epoch,__ATOMIC_ACQUIRE);
    if (cacheEpoch!=generation) resetOwnerCache(generation);
    if (!programKnown) {
        getInteger(GL_CURRENT_PROGRAM,&currentProgram); programKnown=true;
    }
    if (currentProgram<=0 || __atomic_load_n(&epoch,__ATOMIC_ACQUIRE)!=generation) {
        if (trace) ++counters.uncacheable;
        generation=0; invalidate(); return false;
    }
    if (trace) ++counters.eligible;
    auto& entry=entries[static_cast<unsigned int>(location)&63];
    if (!entry.valid || entry.location!=location || entry.opcode!=opcode || entry.size!=size) return false;
    for (unsigned int i=0;i<size/4;++i) if (entry.words[i]!=wordAt(value,i)) return false;
    if (__atomic_load_n(&epoch,__ATOMIC_ACQUIRE)!=generation) { generation=0; return false; }
    if (trace) {
        ++counters.skipped;
        counters.skipped_bytes+=size;
    }
    return true;
}
void finish(GLint location,const void* value,unsigned int opcode,unsigned int size,unsigned long long generation) {
    if (!generation) { if (__atomic_load_n(&enabled,__ATOMIC_RELAXED)) invalidate(); return; }
    if (__atomic_load_n(&epoch,__ATOMIC_ACQUIRE)!=generation) return;
    auto& entry=entries[static_cast<unsigned int>(location)&63];
    entry.valid=false; entry.location=location; entry.opcode=opcode; entry.size=size;
    for (unsigned int i=0;i<size/4;++i) entry.words[i]=wordAt(value,i);
    entry.valid=__atomic_load_n(&epoch,__ATOMIC_ACQUIRE)==generation;
}
void vector4(GLint location,GLsizei count,const GLfloat* value) {
    unsigned long long generation;
    if (prepare(location,count,value,2,16,generation)) return;
    reinterpret_cast<Vector>(originals[3])(location,count,value);
    finish(location,value,2,16,generation);
}
void useProgram(GLuint program) {
    invalidate(); reinterpret_cast<Program>(originals[6])(program); invalidate();
    if (ownerThreadMatches())
        __atomic_store_n(&contextValid,reinterpret_cast<unsigned long>(getContext())==ownerContext,__ATOMIC_RELEASE);
}
EGLBoolean makeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context) {
    invalidate();
    if (ownerThreadMatches()) __atomic_store_n(&contextValid,false,__ATOMIC_RELEASE);
    auto result=reinterpret_cast<MakeCurrent>(originals[8])(display,draw,read,context);
    invalidate();
    if (ownerThreadMatches())
        // Launcher EGL adapters can translate the game's handle; compare the actual host context.
        __atomic_store_n(&contextValid,result && reinterpret_cast<unsigned long>(getContext())==ownerContext,__ATOMIC_RELEASE);
    return result;
}
void linkProgram(GLuint program) { invalidate(); reinterpret_cast<Program>(originals[1])(program); invalidate(); }
void deleteProgram(GLuint program) { invalidate(); reinterpret_cast<Program>(originals[2])(program); invalidate(); }
void uniform1i(GLint location,GLint value) {
    unsigned long long generation;
    if (prepare(location,1,&value,3,4,generation)) return;
    reinterpret_cast<Scalar>(originals[5])(location,value);
    finish(location,&value,3,4,generation);
}
void uniform1iv(GLint location,GLsizei count,const GLint* value) {
    unsigned long long generation;
    if (prepare(location,count,value,3,4,generation)) return;
    reinterpret_cast<IntegerArray>(originals[7])(location,count,value);
    finish(location,value,3,4,generation);
}
const unsigned long replacements[]={0,reinterpret_cast<unsigned long>(linkProgram),
    reinterpret_cast<unsigned long>(deleteProgram),reinterpret_cast<unsigned long>(vector4),0,
    reinterpret_cast<unsigned long>(uniform1i),reinterpret_cast<unsigned long>(useProgram),reinterpret_cast<unsigned long>(uniform1iv),
    reinterpret_cast<unsigned long>(makeCurrent)};
}
void gpu_uniform_cache_trace_enable(bool value) { __atomic_store_n(&tracing,value,__ATOMIC_RELAXED); }
bool client_uniform_cache_enabled() { return __atomic_load_n(&enabled,__ATOMIC_RELAXED); }
void client_set_uniform_cache(bool value) { __atomic_store_n(&enabled,value,__ATOMIC_RELAXED); invalidate(); }
bool gpu_uniform_cache_available() {
    return __atomic_load_n(&ready,__ATOMIC_ACQUIRE) && __atomic_load_n(&contextValid,__ATOMIC_ACQUIRE);
}
const char* gpu_uniform_cache_status() { return __atomic_load_n(&status,__ATOMIC_ACQUIRE); }
bool gpu_uniform_cache_install(unsigned long base) {
    if (attempted || !hooks::supported(base)) return false;
    hooks::Patch patches[7]{};
    for (unsigned int i=0;i<9;++i) {
        if (!hooks::readable(base,profile::slots[i],8) || !hooks::readable(base,profile::plt[i],16,true)
            || !hooks::matches(base,profile::plt[i],profile::pltSignatures[i],6)) return false;
        auto pointer=*reinterpret_cast<const unsigned long*>(base+profile::slots[i]);
        if (!hooks::readable(pointer,0,1,true) || (pointer>=base+profile::pltBegin && pointer<base+profile::pltEnd)) return false;
        for (auto replacement:replacements) if (pointer==replacement) return false;
        originals[i]=pointer;
    }
    // Matrix uploads stay native: their comparison cost did not beat the driver in the benchmark.
    constexpr unsigned int selected[]={1,2,3,5,6,7,8};
    for (unsigned int i=0;i<7;++i) {
        auto slot=selected[i]; patches[i]={profile::slots[slot],originals[slot],replacements[slot]};
    }
    attempted=true;
    if (hooks::install("Uniform reuse v2",base,patches,7)!=hooks::InstallResult::Installed) {
        setStatus("Native imports unavailable"); return false;
    }
    installed=true; setStatus("Waiting for graphics context"); return true;
}
void gpu_uniform_cache_init() {
    if (!hooks::initialize() || !gpu_uniform_cache_install(hooks::find_game())) setStatus("Native imports unavailable");
}
void gpu_uniform_cache_frame() {
    invalidate(); // The mod/other frame rendering may have touched shader state outside the game imports.
    __atomic_store_n(&contextValid,false,__ATOMIC_RELEASE);
    if (!installed) return;
    if (!adapterAttempted) {
        adapterAttempted=true;
        auto egl=mcpelauncher_host_dlopen("libEGL.so.1",2);
        auto libc=mcpelauncher_host_dlopen("libc.so.6",2);
        if (egl && libc) {
            getContext=reinterpret_cast<decltype(getContext)>(mcpelauncher_host_dlsym(egl,"eglGetCurrentContext"));
            auto getProc=reinterpret_cast<decltype(&eglGetProcAddress)>(mcpelauncher_host_dlsym(egl,"eglGetProcAddress"));
            threadSelf=reinterpret_cast<decltype(threadSelf)>(mcpelauncher_host_dlsym(libc,"pthread_self"));
            if (getProc) getInteger=reinterpret_cast<decltype(getInteger)>(getProc("glGetIntegerv"));
        }
    }
    if (!getContext || !getInteger || !threadSelf) { setStatus("Graphics/thread adapter unavailable"); return; }
    auto context=getContext();
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    if (context==EGL_NO_CONTEXT) { setStatus("Waiting for graphics context"); return; }
    if (!ownerEstablished) {
        __atomic_store_n(&ownerContext,reinterpret_cast<unsigned long>(context),__ATOMIC_RELEASE);
        __atomic_store_n(&ownerThread,threadSelf(),__ATOMIC_RELEASE);
        ownerEstablished=true;
    } else if (reinterpret_cast<unsigned long>(context)!=__atomic_load_n(&ownerContext,__ATOMIC_ACQUIRE)
               || threadSelf()!=__atomic_load_n(&ownerThread,__ATOMIC_ACQUIRE)) {
        setStatus("Graphics context/thread changed; native fallback"); return;
    }
    __atomic_store_n(&contextValid,true,__ATOMIC_RELEASE);
    __atomic_store_n(&ready,true,__ATOMIC_RELEASE);
    setStatus("Uniform reuse v2 available");
}
GpuUniformCacheSnapshot gpu_uniform_cache_snapshot() {
    GpuUniformCacheSnapshot result{};
    // The frame callback drains its own counters. Foreign threads only touch atomic counters.
    bool own=threadSelf && threadSelf()==__atomic_load_n(&ownerThread,__ATOMIC_ACQUIRE);
#define DRAIN(name) result.name=__atomic_exchange_n(&foreignCounters.name,0,__ATOMIC_RELAXED); \
    if (own) { result.name+=counters.name; counters.name=0; }
    DRAIN(calls); DRAIN(skipped); DRAIN(skipped_bytes); DRAIN(eligible); DRAIN(owner_rejected); DRAIN(uncacheable);
#undef DRAIN
    return result;
}
