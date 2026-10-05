#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <thread>
#include <sys/mman.h>
#include "zoom.cpp"
constexpr auto buildNote = minecraft_build::current::buildNote;
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
static Proxy nativeOption{}, overrideOption{};
static int calls;
static void* optionGetter(void*, int id) { ++calls; assert(id == 47 || id == 48); return nativeOption.option; }
static void prepare(Proxy& proxy, float value) {
    *reinterpret_cast<void**>(proxy.option + 8) = proxy.definition;
    float* values = reinterpret_cast<float*>(proxy.option + 0x10);
    values[0] = 30; values[1] = 110; values[2] = value;
}
int main() {
    tangent = &tanf; arcTangent = &atanf;
    allocate = &malloc; release = &free;
    createKey = &pthread_key_create; getSpecific = &pthread_getspecific; setSpecific = &pthread_setspecific;
    assert(hooks::initialize());
    assert(createKey(&proxyKey, release) == 0);
    prepare(nativeOption, 70); prepare(overrideOption, 90);
    void* table[29]{}; table[28] = reinterpret_cast<void*>(&optionGetter);
    void** reader = table;
    status = "Unsupported"; zoom_update(true, 30); assert(level == 0);
    status = nullptr;
    zoom_update(false, 30); assert(cameraOption(&reader, 47) == nativeOption.option);
    zoom_update(true, 30);
    auto proxy = static_cast<unsigned char*>(cameraOption(&reader, 47));
    float result = *reinterpret_cast<float*>(proxy+0x18);
    assert(proxy != nativeOption.option && std::fabs(result-26.272f) < 0.01f);
    assert(*reinterpret_cast<float*>(nativeOption.option+0x18) == 70);
    assert(cameraOption(&reader, 48) == nativeOption.option);
    // Resolve native overrides without changing them and isolate camera threads.
    *reinterpret_cast<void**>(nativeOption.definition+0x1a0) = overrideOption.option;
    proxy = static_cast<unsigned char*>(cameraOption(&reader, 47));
    assert(std::fabs(*reinterpret_cast<float*>(proxy+0x18)-36.8699f) < 0.01f);
    std::thread worker([&] {
        assert(cameraOption(&reader, 47) != proxy);
    }); worker.join();
    *reinterpret_cast<void**>(nativeOption.definition+0x1a0) = nullptr;
    zoom_update(true, 300);
    proxy = static_cast<unsigned char*>(cameraOption(&reader, 47));
    float narrow = *reinterpret_cast<float*>(proxy+0x18);
    assert(narrow > 0 && std::fabs(narrow - scaledFov(70, 300)) < 0.0001f);
    assert(narrow < 3.0f && proxy != nativeOption.option);
    zoom_update(true, 301); assert(cameraOption(&reader, 47) == nativeOption.option);
    zoom_update(false, 30); assert(cameraOption(&reader, 47) == nativeOption.option);

    // Exercise build/signature rejection and execute the actual patched call sites.
    unsigned long size = buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    unsigned char header[] = {4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image+buildNote, header, sizeof(header));
    std::memcpy(image+buildNote+16, minecraft_build::current::buildId, sizeof(minecraft_build::current::buildId));
    const unsigned char code[] = {0x48,0x8b,0x07,0xff,0x90,0xe0,0,0,0,0x48,0x89,0xc2,0xc3};
    for (auto site: sites) std::memcpy(image+site-3, code, sizeof(code));
    const unsigned char firstCode[] = {0x48,0x8b,0x07,0xbe,47,0,0,0,0xff,0x90,0xe0,0,0,0,0x48,0x89,0xc2,0xc3};
    std::memcpy(image+sites[0]-8, firstCode, sizeof(firstCode));
    const unsigned char floorCode[] = {0xf3,0x0f,0x10,0x0d,0x25,0x46,0xab,0xf5,0x0f,0x28,0xc1,0xc3};
    std::memcpy(image+floorSite, floorCode, sizeof(floorCode));
    unsigned long base = reinterpret_cast<unsigned long>(image);
    assert(mprotect(image, size, PROT_READ | PROT_EXEC) == 0);
    // Change fixture bytes through a writable page, then restore RX for the gate.
    auto corrupt = [&](unsigned long offset) {
        auto page = image + (offset & ~(hooks::page_size() - 1));
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_WRITE) == 0);
        image[offset] ^= 1;
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    };
    corrupt(buildNote+16); assert(!install(base)); corrupt(buildNote+16);
    corrupt(sites[0]); assert(!install(base)); corrupt(sites[0]);
    corrupt(floorSite); assert(!install(base)); corrupt(floorSite);
    corrupt(sites[0]-8); assert(!install(base)); corrupt(sites[0]-8);
    assert(install(base));
    zoom_update(true, 20);
    for (auto site: sites) {
        auto call = reinterpret_cast<void* (*)(void*, int)>(image+site-(site == sites[0] ? 8 : 3));
        auto output = static_cast<unsigned char*>(call(&reader, 47));
        assert(std::fabs(*reinterpret_cast<float*>(output+0x18)-38.5907f) < 0.01f);
        zoom_update(false, 30); assert(call(&reader,47) == nativeOption.option);
        zoom_update(true, 20);
    }
    auto readFloor = reinterpret_cast<float (*)()>(image+floorSite);
    zoom_update(false, 300); assert(readFloor() == 5.0f);
    zoom_update(true, 300); assert(std::fabs(readFloor()-0.1f) < 0.0001f);
    float maximum = std::fmax(scaledFov(70, 300), readFloor());
    zoom_update(true, 295);
    float reversed = std::fmax(scaledFov(70, 295), readFloor());
    assert(reversed > maximum); // One reverse step changes the actual clamped FOV.
    zoom_update(false, 295); assert(readFloor() == 5.0f);
    assert(munmap(image, size) == 0);
    std::puts("PASS: FOV optics, unchanged native settings, override resolution, per-thread proxies, build/opcode gates, all four executable camera calls, and active/inactive FOV floor with immediate reversal");
}
