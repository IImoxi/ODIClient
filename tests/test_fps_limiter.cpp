#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>
#include <sys/mman.h>
#include "../fps_limiter.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
static long long clockNs = 1000000000;
static int sleeps;
static int fakeClock(clockid_t, timespec* time) {
    time->tv_sec = clockNs / 1000000000; time->tv_nsec = clockNs % 1000000000; return 0;
}
static int fakeSleep(const timespec* time, timespec*) {
    ++sleeps; clockNs += time->tv_sec * 1000000000LL + time->tv_nsec; return 0;
}
struct Snapshot { unsigned long rax, flags; float xmm0[4]; };

int main() {
    clockGetTime = fakeClock; nanoSleep = fakeSleep;
    // Standard pacing, disabled clocks, missed native callbacks and overrun reset.
    fps_limiter_wait(false, true, 120); assert(sleeps == 0 && frameDelta == 0);
    fps_limiter_wait(true, true, 120); assert(sleeps == 1 && frameDelta == 1000000000 / 120);
    clockNs += 1000000000;
    fps_limiter_wait(true, true, 120); assert(sleeps == 1);
    fps_limiter_native_update(true, 120);
    fps_limiter_wait(true, true, 120); assert(sleeps == 2 && fps_limiter_error());

    fps_limiter_init(false);
    assert(!nativeInstalled && !nativePacing);
    assert(hooks::initialize());
    auto size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    const unsigned char note[] = {4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    std::memcpy(image + minecraft_build::current::buildNote + 16, minecraft_build::current::buildId, 20);
    std::memcpy(image + nativeBuild::intervalCalculation, nativeBuild::calculationSignature, sizeof(nativeBuild::calculationSignature));
    std::memcpy(image + nativeBuild::waitSite, nativeBuild::waitSignature, sizeof(nativeBuild::waitSignature));
    std::memcpy(image + nativeBuild::intervalSite, nativeBuild::intervalStore, sizeof(nativeBuild::intervalStore));
    // A native-stack fixture executes the real installed relay and records the
    // registers/flags that the replaced store must leave untouched.
    const unsigned char before[] = {
        0x55,0x48,0x89,0xe5,0x48,0x81,0xec,0x30,0x03,0,0,
        0x48,0xb8,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x48,0x39,0xc0
    };
    const unsigned char after[] = {
        0x48,0x89,0x07,0x9c,0x58,0x48,0x89,0x47,0x08,0x0f,0x11,0x47,0x10,
        0xf3,0x0f,0x10,0x85,0xd8,0xfc,0xff,0xff,0xc9,0xc3
    };
    std::memcpy(image + nativeBuild::intervalSite - sizeof(before), before, sizeof(before));

    auto base = reinterpret_cast<unsigned long>(image);
    assert(mprotect(image, size, PROT_READ | PROT_EXEC) == 0);
    auto corrupt = [&](unsigned long offset) {
        auto page = image + (offset & ~(hooks::page_size() - 1));
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_WRITE) == 0);
        image[offset] ^= 1;
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    };
    for (auto offset : {minecraft_build::current::buildNote + 16, nativeBuild::intervalSite,
                       nativeBuild::intervalCalculation, nativeBuild::waitSite}) {
        corrupt(offset); assert(!installNative(base)); corrupt(offset);
    }
    assert(installNative(base)); nativeInstalled = true;
    // Inject fixture continuations only after the native gates passed.
    auto storePage = image + (nativeBuild::intervalSite & ~(hooks::page_size() - 1));
    assert(mprotect(storePage, hooks::page_size(), PROT_READ | PROT_WRITE) == 0);
    std::memcpy(image + nativeBuild::intervalSite + 8, after, sizeof(after));
    assert(mprotect(storePage, hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    // Execute the candidate relay with both vanilla unlimited and positive caps.
    auto capPage = image + (nativeBuild::intervalCalculation & ~(hooks::page_size() - 1));
    assert(mprotect(capPage, hooks::page_size(), PROT_READ | PROT_WRITE) == 0);
    const unsigned char capBefore[] = {0x41,0x57,0x41,0x89,0xf7}; // save r15; esi -> r15d
    const unsigned char capPositive[] = {0x0f,0x28,0xc1,0x41,0x5f,0xc3}; // xmm1 -> return
    const unsigned char capUnlimited[] = {0x0f,0x57,0xc0,0x41,0x5f,0xc3}; // return zero
    std::memcpy(image + nativeBuild::intervalCalculation - 5, capBefore, sizeof(capBefore));
    std::memcpy(image + nativeBuild::intervalCalculation + 10, capPositive, sizeof(capPositive));
    std::memcpy(image + nativeBuild::intervalCalculation + 27, capUnlimited, sizeof(capUnlimited));
    assert(mprotect(capPage, hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    auto capRun = reinterpret_cast<float (*)(int, int)>(image + nativeBuild::intervalCalculation - 5);
    fps_limiter_native_update(false, 120);
    assert(capRun(0, 0) == 0 && capRun(0, 60) == 60);
    fps_limiter_native_update(true, 120);
    assert(capRun(0, 0) == 120 && capRun(0, 60) == 120);
    fps_limiter_native_update(true, 480);
    assert(capRun(0, -1) == 480);
    fps_limiter_native_update(false, 120);
    assert(capRun(0, 0) == 0 && capRun(0, 60) == 60);
    using Fixture = float (*)(Snapshot*, float);
    auto run = reinterpret_cast<Fixture>(image + nativeBuild::intervalSite - sizeof(before));
    auto check = [&](float expected) {
        Snapshot snapshot{};
        float result = run(&snapshot, 1000.0f / 60);
        assert(std::fabs(result - expected) < 1e-5f);
        assert(snapshot.rax == 0x1122334455667788UL);
        assert((snapshot.flags & 0x8d5) == 0x44); // ZF and PF from cmp rax,rax.
        assert(snapshot.xmm0[0] == 1000.0f / 60);
    };
    fps_limiter_native_update(false, 120); check(1000.0f / 60);
    assert(nativePacing->frames == 0);
    fps_limiter_native_update(true, 120); check(1000.0f / 120);
    assert(nativePacing->frames == 1);
    // Native conversion: milliseconds -> nanoseconds, subtract elapsed ns,
    // then convert the remaining budget to microseconds. Check a real budget.
    Snapshot budgetSnapshot{};
    float milliseconds = run(&budgetSnapshot, 1000.0f / 60);
    float remainingUs = (milliseconds * 1000.0f * 1000.0f - 2000000.0f) / 1000.0f;
    assert(std::fabs(remainingUs - 6333.333f) < 0.01f);
    int beforeSleeps = sleeps;
    clockNs += 1000000000 / 120;
    fps_limiter_wait(true, true, 120);
    assert(sleeps == beforeSleeps && !fps_limiter_error());
    assert(frameTimestamp == clockNs && frameDelta == 1000000000 / 120);
    // Extra callbacks must not add a late wait to installed native pacing.
    fps_limiter_wait(true, true, 120); assert(sleeps == beforeSleeps && fps_limiter_error());
    fps_limiter_native_update(true, 1); check(1000.0f / 30);
    fps_limiter_native_update(true, 999); check(1000.0f / 480);
    fps_limiter_native_update(false, 120); check(1000.0f / 60);
    fps_limiter_wait(false, true, 120); assert(sleeps == beforeSleeps);
    fps_limiter_native_update(false, 120);
    fps_limiter_wait(true, true, 120);
    assert(sleeps == beforeSleeps + 1); // Disabling native mode restores standard pacing.
    assert(!installNative(base)); // Cannot duplicate an installed owner/patch.
    // The fixture/relay intentionally live to process exit, like native hooks.
    std::puts("PASS: fixed native pacing ABI and gates, disabled replay, FPS bounds, standard fallback and no late wait");
}
