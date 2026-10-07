#include <cassert>
#include <initializer_list>
#include <dlfcn.h>
#include <cstring>
#include <sys/mman.h>
#include "../environment.cpp"
extern "C" void* mcpelauncher_host_dlopen(const char* n, int f) { return dlopen(n, f); }
extern "C" void* mcpelauncher_host_dlsym(void* l, const char* n) { return dlsym(l, n); }
EnvironmentSettings saved;
void sky_renderer_init() {}
EnvironmentSettings client_settings_get_environment() { return saved; }
void client_settings_set_environment(EnvironmentSettings s) { saved = s; }
Color nativeFog(void*, const Color& c, float b) { return {c.r*b, c.g*b, c.b*b, c.a}; }
int receivedTicks;
float receivedPartial;
float nativeAngle(void*, int ticks, float partial) {
    receivedTicks = ticks; receivedPartial = partial; return 0.25f;
}
int main() {
    originals[0] = originals[1] = originals[2] = nativeFog;
    originalAngle = nativeAngle;
    Color c{0.2f, 0.4f, 0.6f, 0.7f};
    client_set_environment(true); client_set_environment_fog(true); client_set_environment_time(true);
    assert(fog<0>(nullptr, c, 0.5f).r == 0.1f); // Fail closed until installed.
    ready = true;
    client_set_environment_hue(120); client_set_environment_saturation(100);
    auto green = fog<0>(nullptr, c, 0.5f);
    assert(green.r == 0 && green.g == 1 && green.b == 0 && green.a == c.a);
    for (int hue = 0; hue <= 360; ++hue) {
        client_set_environment_hue(hue);
        for (Color color : {fog<0>(nullptr, c, 1), fog<1>(nullptr, c, 1), fog<2>(nullptr, c, 1)})
            assert(color.r >= 0 && color.r <= 1 && color.g >= 0 && color.g <= 1 && color.b >= 0 && color.b <= 1);
    }
    client_set_environment_hue(360);
    assert(fog<0>(nullptr, c, 1).r == 1 && fog<0>(nullptr, c, 1).g == 0);
    client_set_environment_saturation(0); client_set_environment_value(40);
    auto gray = fog<0>(nullptr, c, 1); assert(gray.r == 0.4f && gray.g == gray.r && gray.b == gray.r);
    client_set_environment_ticks(23999);
    assert(angle(nullptr, 1234, 0.75f) == 0.25f && receivedTicks == 23999 && receivedPartial == 0);
    assert(environment_sky_angle() == 0.25f);
    client_set_environment_time(false); angle(nullptr, 1234, 0.75f);
    assert(receivedTicks == 1234 && receivedPartial == 0.75f);
    client_set_environment_fog(false); assert(fog<1>(nullptr, c, 0.5f).b == c.b*0.5f);
    client_set_environment_time(true); client_set_environment_fog(true); client_set_environment(false);
    angle(nullptr, 5678, 0.25f); assert(receivedTicks == 5678 && receivedPartial == 0.25f);
    assert(fog<2>(nullptr, c, 0.5f).r == c.r*0.5f);
    client_set_environment_ticks(-5); client_set_environment_hue(999); client_set_environment_value(999);
    client_set_environment_saturation(-1);
    assert(saved.ticks == 0 && saved.hue == 360 && saved.value == 100 && saved.saturation == 0);
    assert(!saved.enabled && saved.time && saved.fog);

    // Installation checks the exact build, native callers and every patched slot.
    assert(hooks::initialize());
    unsigned long size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    auto base = reinterpret_cast<unsigned long>(image);
    unsigned int note[] = {4, 20, 3, 0x00554e47};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    std::memcpy(image + minecraft_build::current::buildNote + 16, minecraft_build::current::buildId, 20);
    const unsigned long sites[] = {profile::fogOverworldSite, profile::fogPassthroughSite,
        profile::angleEntrySite, profile::fogCallerSite, profile::angleCallerSite};
    const unsigned char* signatures[] = {profile::fogOverworldSignature, profile::fogPassthroughSignature,
        profile::angleEntrySignature, profile::fogCallerSignature, profile::angleCallerSignature};
    const unsigned long sizes[] = {sizeof(profile::fogOverworldSignature), sizeof(profile::fogPassthroughSignature),
        sizeof(profile::angleEntrySignature), sizeof(profile::fogCallerSignature), sizeof(profile::angleCallerSignature)};
    for (int i = 0; i < 5; ++i) std::memcpy(image + sites[i], signatures[i], sizes[i]);
    auto slot = [&](unsigned long offset) -> unsigned long& {
        return *reinterpret_cast<unsigned long*>(image + offset);
    };
    for (int i = 0; i < 3; ++i) slot(profile::fogTables[i] + profile::fogSlot) = base + profile::fogFunctions[i];
    slot(profile::fogTables[0] + profile::angleSlot) = base + profile::angleFunction;
    assert(mprotect(image, size, PROT_READ | PROT_EXEC) == 0);
    auto corrupt = [&](unsigned long offset) {
        auto page = image + (offset & ~(hooks::page_size() - 1));
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_WRITE) == 0);
        image[offset] ^= 1;
        assert(mprotect(page, hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    };
    auto rejected = [&](unsigned long offset) {
        corrupt(offset); assert(!install(base)); corrupt(offset);
        for (int i = 0; i < 3; ++i)
            assert(slot(profile::fogTables[i] + profile::fogSlot) == base + profile::fogFunctions[i]);
        assert(slot(profile::fogTables[0] + profile::angleSlot) == base + profile::angleFunction);
    };
    rejected(minecraft_build::current::buildNote + 16);
    for (auto site : sites) rejected(site);
    for (auto table : profile::fogTables) rejected(table + profile::fogSlot);
    rejected(profile::fogTables[0] + profile::angleSlot);
    assert(install(base));
    assert(originalAngle == reinterpret_cast<Angle>(base + profile::angleFunction));
    assert(slot(profile::fogTables[0] + profile::angleSlot) == reinterpret_cast<unsigned long>(&angle));
    assert(slot(profile::fogTables[0] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<0>));
    assert(slot(profile::fogTables[1] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<1>));
    assert(slot(profile::fogTables[2] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<2>));
    assert(munmap(image, size) == 0);

}
