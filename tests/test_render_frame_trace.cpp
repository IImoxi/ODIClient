#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../render_frame_trace.cpp"

static long long wallValue=1000,cpuValue=1000;
static int wallReads,cpuReads,threadReads,submitForwards,presentForwards;
static unsigned long currentThread=1;
static long long wallClock() { ++wallReads; return wallValue; }
static long long cpuClock() { ++cpuReads; return cpuValue; }
static unsigned long mockThread() { ++threadReads; return currentThread; }
static void nativeSubmit(void* renderer,void* frame,void* clear,void* text) {
    assert(renderer==reinterpret_cast<void*>(1) && frame==reinterpret_cast<void*>(2)
        && clear==reinterpret_cast<void*>(3) && text==reinterpret_cast<void*>(4));
    ++submitForwards; wallValue+=37;cpuValue+=11;
}
static unsigned int nativePresent(void* display,void* surface) {
    assert(display==reinterpret_cast<void*>(5) && surface==reinterpret_cast<void*>(6));
    ++presentForwards;wallValue+=53;cpuValue+=13;return 0x12345678;
}
extern "C" void* mcpelauncher_host_dlopen(const char* name,int flags) { return dlopen(name,flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library,const char* name) {
    if (std::strcmp(name,"pthread_self")==0) return reinterpret_cast<void*>(mockThread);
    return dlsym(library,name);
}
int main(int argc,char** argv) {
    bool rejectSubmit=argc>1 && std::strcmp(argv[1],"--present-only")==0;
    bool rejectPresent=argc>1 && std::strcmp(argv[1],"--submit-only")==0;
    assert(hooks::initialize());
    auto size=minecraft_build::current::buildNote+4096;
    auto image=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image!=MAP_FAILED);auto base=reinterpret_cast<unsigned long>(image);
    const unsigned char note[]={4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,minecraft_build::current::buildId,20);
    std::memcpy(image+profile::submitFunction,profile::submitSignature,sizeof(profile::submitSignature));
    std::memcpy(image+profile::submitCaller,profile::submitCallerSignature,sizeof(profile::submitCallerSignature));
    std::memcpy(image+profile::submitCtor,profile::submitCtorSignature,sizeof(profile::submitCtorSignature));
    std::memcpy(image+profile::submitReturn,profile::submitReturnSignature,sizeof(profile::submitReturnSignature));
    // Execute the actual gated prologue, forward its four untouched pointer registers, then restore saved registers.
    auto bridge=image+profile::submitFunction+sizeof(profile::submitSignature);
    *bridge++=0x48;*bridge++=0xb8;
    auto destination=reinterpret_cast<unsigned long>(nativeSubmit);std::memcpy(bridge,&destination,8);bridge+=8;
    *bridge++=0xff;*bridge++=0xd0;
    std::memcpy(bridge,profile::submitReturnSignature,sizeof(profile::submitReturnSignature));
    *reinterpret_cast<unsigned long*>(image+profile::submitSlot)=base+profile::submitFunction;
    std::memcpy(image+profile::swapPlt,profile::swapSignature,sizeof(profile::swapSignature));
    *reinterpret_cast<unsigned long*>(image+profile::swapSlot)=reinterpret_cast<unsigned long>(nativePresent);
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    auto edit=[&](unsigned long offset,const void* value,unsigned long length) {
        auto page=image+(offset&~(hooks::page_size()-1));
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_WRITE)==0);
        std::memcpy(image+offset,value,length);
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_EXEC)==0);
    };
    render_frame_trace_configure(wallClock,cpuClock);
    auto originalSubmit=base+profile::submitFunction;
    unsigned long nullPointer=0;edit(profile::submitSlot,&nullPointer,8);
    assert(!installSubmit(base));assert(*reinterpret_cast<unsigned long*>(image+profile::submitSlot)==0);
    edit(profile::submitSlot,&originalSubmit,8);
    const unsigned long gates[]={profile::submitFunction,profile::submitCaller,profile::submitCtor,profile::submitReturn};
    for (auto gate:gates) {
        auto saved=image[gate];unsigned char changed=saved^1;edit(gate,&changed,1);
        assert(!installSubmit(base));
        assert(*reinterpret_cast<unsigned long*>(image+profile::submitSlot)==originalSubmit);
        assert(*reinterpret_cast<unsigned long*>(image+profile::swapSlot)==reinterpret_cast<unsigned long>(nativePresent));
        edit(gate,&saved,1);
    }
    auto presentPointer=reinterpret_cast<unsigned long>(nativePresent);
    edit(profile::swapSlot,&nullPointer,8); assert(!installPresent(base));
    edit(profile::swapSlot,&presentPointer,8);
    auto savedPlt=image[profile::swapPlt];unsigned char changedPlt=savedPlt^1;
    edit(profile::swapPlt,&changedPlt,1);assert(!installPresent(base));
    edit(profile::swapPlt,&savedPlt,1);
    assert(*reinterpret_cast<unsigned long*>(image+profile::submitSlot)==originalSubmit);
    if (rejectSubmit) { unsigned char invalid=image[profile::submitCaller]^1;edit(profile::submitCaller,&invalid,1); }
    if (rejectPresent) edit(profile::swapPlt,&changedPlt,1);
    auto installed=render_frame_trace_install(base);assert(installed.submit==!rejectSubmit && installed.present==!rejectPresent);
    auto again=render_frame_trace_install(base);assert(!again.submit && !again.present);
    auto submitSlot=reinterpret_cast<void(*)(void*,void*,void*,void*)>(*reinterpret_cast<unsigned long*>(image+profile::submitSlot));
    auto presentSlot=reinterpret_cast<Present>(*reinterpret_cast<unsigned long*>(image+profile::swapSlot));
    submitSlot(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4));
    assert(presentSlot(reinterpret_cast<void*>(5),reinterpret_cast<void*>(6))==0x12345678);
    auto measured=render_frame_trace_snapshot();
    assert(measured.stages[FrameNativeSubmit].calls==static_cast<unsigned int>(installed.submit)
        && measured.stages[FrameNativeSubmit].wallNs==(installed.submit?37:0)
        && measured.stages[FrameNativeSubmit].cpuNs==(installed.submit?11:0));
    assert(measured.stages[FramePresent].calls==static_cast<unsigned int>(installed.present)
        && measured.stages[FramePresent].wallNs==(installed.present?53:0)
        && measured.stages[FramePresent].cpuNs==(installed.present?13:0));
    wallValue=2000;cpuValue=2000;auto first=render_frame_trace_begin();
    wallValue=2100;cpuValue=2020;auto limiter=render_frame_trace_stamp();
    wallValue=2300;cpuValue=2021;render_frame_trace_record(FrameLimiter,limiter);
    auto overlay=render_frame_trace_stamp();wallValue=2400;cpuValue=2051;render_frame_trace_record(FrameOverlays,overlay);
    wallValue=2500;cpuValue=2100;render_frame_trace_end(first);
    wallValue=3000;cpuValue=2200;auto second=render_frame_trace_begin();
    wallValue=3100;cpuValue=2210;render_frame_trace_end(second);
    currentThread=2;wallValue=4000;cpuValue=9000;auto third=render_frame_trace_begin();
    wallValue=4100;cpuValue=9010;render_frame_trace_end(third);
    measured=render_frame_trace_snapshot();
    assert(measured.stages[FrameOutsideCallback].calls==1 && measured.stages[FrameOutsideCallback].wallNs==500 && measured.stages[FrameOutsideCallback].cpuNs==100 && measured.gapResets==1);
    assert(measured.stages[FrameClientCallback].calls==3 && measured.stages[FrameClientCallback].wallNs==700 && measured.stages[FrameClientCallback].wallMaxNs==500 && measured.stages[FrameClientCallback].cpuNs==120);
    assert(measured.stages[FrameLimiter].wallNs==200 && measured.stages[FrameLimiter].cpuNs==1);
    assert(measured.stages[FrameOverlays].wallNs==100 && measured.stages[FrameOverlays].cpuNs==30);
    wallValue=100;cpuValue=100;render_frame_trace_record(FrameOverlays,{101,100});
    render_frame_trace_record(RenderFrameStageCount,{100,100});assert(render_frame_trace_snapshot().stages[FrameOverlays].calls==0);
    render_frame_trace_disable();auto beforeWall=wallReads,beforeCpu=cpuReads,beforeThread=threadReads;
    submitSlot(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4));
    assert(presentSlot(reinterpret_cast<void*>(5),reinterpret_cast<void*>(6))==0x12345678);
    assert(!render_frame_trace_stamp().wall && !render_frame_trace_begin().wall);
    render_frame_trace_record(FrameLimiter,{1,1});render_frame_trace_end({1,1});
    assert(wallReads==beforeWall && cpuReads==beforeCpu && threadReads==beforeThread && submitForwards==2 && presentForwards==2);
    auto empty=render_frame_trace_snapshot();for (const auto& stage:empty.stages) assert(!stage.calls);
    puts("Frame trace native ABI/gates/timing/thread-gap/passthrough checks passed");
}
