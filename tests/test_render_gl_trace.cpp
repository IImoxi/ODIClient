#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../render_gl_trace.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
static int forwards[4], clockReads;
static long long clockValue = 1000;
static long long clockNow() { ++clockReads; return clockValue += 100; }
static void nativeArrays(unsigned int mode, int first, int count) {
    assert(mode == 4 && first == 3 && count == 9); ++forwards[0];
}
static void nativeElements(unsigned int mode, int count, unsigned int type, const void* index) {
    assert(mode == 4 && count == 12 && type == 0x1403 && index == reinterpret_cast<void*>(24));
    ++forwards[1];
}
static void nativeData(unsigned int target, long size, const void* data, unsigned int usage) {
    assert(target == 0x8892 && size == 4096 && !data && usage == 0x88e4); ++forwards[2];
}
static void nativeSubData(unsigned int target, long offset, long size, const void* data) {
    assert(target == 0x8892 && offset == 17 && size == 256 && data == reinterpret_cast<void*>(42));
    ++forwards[3];
}
int main() {
    assert(hooks::initialize());
    unsigned long size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    auto base = reinterpret_cast<unsigned long>(image);
    const unsigned char note[] = {4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    std::memcpy(image + minecraft_build::current::buildNote + 16,
        minecraft_build::current::buildId, 20);
    const unsigned long functions[] = {reinterpret_cast<unsigned long>(nativeArrays),
        reinterpret_cast<unsigned long>(nativeElements), reinterpret_cast<unsigned long>(nativeData),
        reinterpret_cast<unsigned long>(nativeSubData)};
    for (unsigned int i = 0; i < 4; ++i) {
        std::memcpy(image + profile::plt[i], profile::pltSignatures[i], 6);
        *reinterpret_cast<unsigned long*>(image + profile::slots[i]) = functions[i];
    }
    assert(mprotect(image, size, PROT_READ|PROT_EXEC) == 0);
    render_gl_trace_configure(clockNow);
    auto mutate = [&](unsigned long offset, unsigned long value) {
        auto page = image + (offset & ~(hooks::page_size()-1));
        assert(mprotect(page, hooks::page_size(), PROT_READ|PROT_WRITE) == 0);
        std::memcpy(image + offset, &value, 8);
        assert(mprotect(page, hooks::page_size(), PROT_READ|PROT_EXEC) == 0);
    };
    unsigned long saved;
    std::memcpy(&saved, image + profile::plt[0], 8);
    mutate(profile::plt[0], saved ^ 1); assert(!render_gl_trace_install(base));
    mutate(profile::plt[0], saved);
    mutate(profile::slots[0], 0); assert(!render_gl_trace_install(base));
    mutate(profile::slots[0], base + profile::plt[0] + 6); assert(!render_gl_trace_install(base));
    mutate(profile::slots[0], replacements[0]); assert(!render_gl_trace_install(base));
    mutate(profile::slots[0], functions[0]);
    assert(render_gl_trace_install(base));
    assert(!render_gl_trace_install(base));
    auto arrays = reinterpret_cast<DrawArrays>(*reinterpret_cast<unsigned long*>(image+profile::slots[0]));
    auto elements = reinterpret_cast<DrawElements>(*reinterpret_cast<unsigned long*>(image+profile::slots[1]));
    auto data = reinterpret_cast<BufferData>(*reinterpret_cast<unsigned long*>(image+profile::slots[2]));
    auto sub = reinterpret_cast<BufferSubData>(*reinterpret_cast<unsigned long*>(image+profile::slots[3]));
    for (int i=0; i<65; ++i) arrays(4,3,9);
    elements(4,12,0x1403,reinterpret_cast<void*>(24));
    data(0x8892,4096,nullptr,0x88e4);
    sub(0x8892,17,256,reinterpret_cast<void*>(42));
    auto result = render_gl_trace_snapshot();
    assert(forwards[0] == 65 && forwards[1] == 1 && forwards[2] == 1 && forwards[3] == 1);
    assert(result.operations[0].calls == 65 && result.operations[0].units == 585);
    assert(result.operations[0].samples == 2 && result.operations[0].sampleNs == 200);
    assert(result.operations[0].sampleMaxNs == 100 && clockReads == 10);
    assert(result.operations[1].units == 12 && result.operations[2].units == 4096
        && result.operations[3].units == 256);
    auto empty = render_gl_trace_snapshot();
    assert(empty.operations[0].calls == 0 && empty.operations[0].samples == 0);
    render_gl_trace_disable(); // Retained/inactive hooks must always forward without timer work.
    arrays(4,3,9); assert(forwards[0] == 66 && clockReads == 10);
    assert(render_gl_trace_snapshot().operations[0].calls == 0);
    puts("render GL import trace checks passed");
}
