#include "render_frame_trace.h"
#include "minecraft_build.h"
#include "hook_manager.h"
#include "launcher_api.h"

namespace {
namespace profile = minecraft_build::current::render::frameTrace;
long long (*wallNow)(), (*cpuNow)();
unsigned long (*threadSelf)();
bool active, attempted;
unsigned long previousThread;
RenderFrameStamp previousEnd;
RenderFrameTiming timings[RenderFrameStageCount]{};
unsigned long long gapResets;
using Present = unsigned int (*)(void*, void*);
Present presentOriginal;
bool presentInstalled;
using Submit = void (*)(void*,void*,void*,void*);
Submit submitOriginal;
bool submitInstalled;
void record(RenderFrameStage stage, RenderFrameStamp start, RenderFrameStamp finish) {
    if (start.wall<=0 || start.cpu<=0 || finish.wall<start.wall || finish.cpu<start.cpu) return;
    auto& timing=timings[stage];
    auto duration=static_cast<unsigned long long>(finish.wall-start.wall);
    __atomic_fetch_add(&timing.calls,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&timing.wallNs,duration,__ATOMIC_RELAXED);
    __atomic_fetch_add(&timing.cpuNs,finish.cpu-start.cpu,__ATOMIC_RELAXED);
    auto maximum=__atomic_load_n(&timing.wallMaxNs,__ATOMIC_RELAXED);
    while (duration>maximum && !__atomic_compare_exchange_n(&timing.wallMaxNs,&maximum,duration,
        false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
}
unsigned int present(void* display,void* surface) {
    auto start=presentInstalled ? render_frame_trace_stamp() : RenderFrameStamp{};
    auto result=presentOriginal(display,surface);
    render_frame_trace_record(FramePresent,start);
    return result;
}
void submit(void* renderer,void* frame,void* clearQuad,void* textBlitter) {
    auto start=submitInstalled ? render_frame_trace_stamp() : RenderFrameStamp{};
    submitOriginal(renderer,frame,clearQuad,textBlitter);
    render_frame_trace_record(FrameNativeSubmit,start);
}
bool installSubmit(unsigned long base) {
    if (!hooks::supported(base)
        || !hooks::matches_pointer(base,profile::submitSlot,profile::submitFunction)
        || !hooks::readable(base,profile::submitFunction,sizeof(profile::submitSignature),true)
        || !hooks::readable(base,profile::submitReturn,sizeof(profile::submitReturnSignature),true)
        || !hooks::matches(base,profile::submitFunction,profile::submitSignature,sizeof(profile::submitSignature))
        || !hooks::matches(base,profile::submitCaller,profile::submitCallerSignature,sizeof(profile::submitCallerSignature))
        || !hooks::matches(base,profile::submitCtor,profile::submitCtorSignature,sizeof(profile::submitCtorSignature))
        || !hooks::matches(base,profile::submitReturn,profile::submitReturnSignature,sizeof(profile::submitReturnSignature))) return false;
    submitOriginal=reinterpret_cast<Submit>(base+profile::submitFunction);
    hooks::Patch patch{profile::submitSlot,base+profile::submitFunction,reinterpret_cast<unsigned long>(submit)};
    return hooks::install("Frame submit trace",base,&patch,1)==hooks::InstallResult::Installed;
}
bool installPresent(unsigned long base) {
    if (!hooks::supported(base) || !hooks::readable(base,profile::swapSlot,8)
        || !hooks::readable(base,profile::swapPlt,16,true)
        || !hooks::matches(base,profile::swapPlt,profile::swapSignature,sizeof(profile::swapSignature))) return false;
    auto pointer=*reinterpret_cast<const unsigned long*>(base+profile::swapSlot);
    auto wrapper=reinterpret_cast<unsigned long>(present);
    if (pointer==wrapper || !hooks::readable(pointer,0,1,true)
        || (pointer>=base+profile::pltBegin && pointer<base+profile::pltEnd)) return false;
    presentOriginal=reinterpret_cast<Present>(pointer);
    hooks::Patch patch{profile::swapSlot,pointer,wrapper};
    return hooks::install("Frame present trace",base,&patch,1)==hooks::InstallResult::Installed;
}
}
void render_frame_trace_configure(long long (*wallClock)(),long long (*cpuClock)()) {
    if (attempted) return;
    wallNow=wallClock; cpuNow=cpuClock;
    auto libc=mcpelauncher_host_dlopen("libc.so.6",2);
    if (libc) threadSelf=reinterpret_cast<decltype(threadSelf)>(mcpelauncher_host_dlsym(libc,"pthread_self"));
    __atomic_store_n(&active,wallNow && cpuNow && threadSelf,__ATOMIC_RELEASE);
}
void render_frame_trace_disable() { __atomic_store_n(&active,false,__ATOMIC_RELEASE); }
RenderFrameStamp render_frame_trace_stamp() {
    if (!__atomic_load_n(&active,__ATOMIC_ACQUIRE)) return {};
    return {wallNow(),cpuNow()};
}
RenderFrameStamp render_frame_trace_begin() {
    auto start=render_frame_trace_stamp();
    if (!start.wall || !start.cpu) return {};
    auto thread=threadSelf();
    // Only subtract CPU timestamps from the same callback thread.
    if (previousEnd.wall && previousThread==thread) record(FrameOutsideCallback,previousEnd,start);
    else if (previousEnd.wall) __atomic_fetch_add(&gapResets,1,__ATOMIC_RELAXED);
    previousEnd={}; previousThread=thread;
    return start;
}
void render_frame_trace_record(RenderFrameStage stage,RenderFrameStamp start) {
    if (stage<0 || stage>=RenderFrameStageCount || !start.wall || !__atomic_load_n(&active,__ATOMIC_ACQUIRE)) return;
    record(stage,start,render_frame_trace_stamp());
}
void render_frame_trace_end(RenderFrameStamp start) {
    if (!start.wall || !__atomic_load_n(&active,__ATOMIC_ACQUIRE)) return;
    auto finish=render_frame_trace_stamp();
    record(FrameClientCallback,start,finish);
    previousEnd=finish;
}
RenderFrameHooks render_frame_trace_install(unsigned long base) {
    if (attempted || !__atomic_load_n(&active,__ATOMIC_ACQUIRE) || !hooks::supported(base)) return {};
    attempted=true;
    submitInstalled=installSubmit(base);
    presentInstalled=installPresent(base);
    // Retained patches forward through permanent originals, with their individual stage inactive.
    return {submitInstalled,presentInstalled};
}
RenderFrameSnapshot render_frame_trace_snapshot() {
    RenderFrameSnapshot result{};
    for (unsigned int i=0;i<RenderFrameStageCount;++i) {
#define DRAIN(field) result.stages[i].field=__atomic_exchange_n(&timings[i].field,0,__ATOMIC_RELAXED)
        DRAIN(calls); DRAIN(wallNs); DRAIN(wallMaxNs); DRAIN(cpuNs);
#undef DRAIN
    }
    result.gapResets=__atomic_exchange_n(&gapResets,0,__ATOMIC_RELAXED);
    return result;
}
