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
float seenRain, seenThunder;
void nativeWeatherTick(void* weather) {
    seenRain = weatherField<float>(weather, 0x38);
    seenThunder = weatherField<float>(weather, 0x44);
    ++weatherField<int>(weather, 0x30);
    weatherField<float>(weather, 0x34) = seenRain;
    weatherField<float>(weather, 0x40) = seenThunder;
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
    client_set_environment_clouds(true);
    assert(!client_environment_sky_quarter_resolution());
    client_set_environment_sky_quarter_resolution(true);
    assert(client_environment_sky_quarter_resolution() && saved.skyQuarterResolution);
    client_set_environment_sky_quarter_resolution(false);
    assert(!client_environment_sky_quarter_resolution() && !saved.skyQuarterResolution);
    client_set_environment_cloud_detail(3); client_set_environment_cloud_samples(64); client_set_environment_cloud_resolution(2);
    assert(saved.clouds && saved.cloudDetail == 3 && saved.cloudSamples == 64 && saved.cloudResolution == 2);
    assert(pack(settings(snapshot())) == snapshot());
    client_set_environment_cloud_detail(-1); client_set_environment_cloud_samples(-1); client_set_environment_cloud_resolution(-1);
    assert(saved.cloudDetail == 1 && saved.cloudSamples == 8 && saved.cloudResolution == 0);
    client_set_environment_cloud_detail(99); client_set_environment_cloud_samples(999); client_set_environment_cloud_resolution(999);
    assert(saved.cloudDetail == 3 && saved.cloudSamples == 64 && saved.cloudResolution == 2);
    client_set_environment_cloud_samples(29); assert(saved.cloudSamples == 32);
    client_set_environment_clouds(false); assert(!saved.clouds);

    // Native interpolation is restored before forwarding, and server worlds
    // never receive the override. New packet values supersede our snapshot.
    alignas(8) unsigned char weatherObject[0x68]{}, dimensionObject[0x1c0]{}, levelObject[8]{};
    weatherField<void*>(weatherObject, 0x58) = dimensionObject;
    weatherField<void*>(dimensionObject, 0xa0) = levelObject;
    weatherField<unsigned long>(dimensionObject, 0) = profile::fogTables[0];
    weatherField<unsigned long>(levelObject, 0) = profile::weatherClientLevelTable;
    originalWeatherTick = nativeWeatherTick;
    weatherField<float>(weatherObject, 0x38) = 0.2f;
    weatherField<float>(weatherObject, 0x44) = 0.1f;
    client_set_environment_weather(true); client_set_environment_weather_amount(75);
    weatherTick(weatherObject); assert(seenRain == 0.2f && seenThunder == 0.1f);
    assert(weatherField<float>(weatherObject, 0x38) == 0.2f); // Module OFF.
    client_set_environment(true);
    for (int amount : {0, 25, 50, 75, 100}) {
        client_set_environment_weather_amount(amount);
        auto levels = environment_weather();
        assert(levels.rain == (amount < 50 ? amount * 2 : 100) / 100.0f);
        assert(levels.thunder == (amount > 50 ? (amount - 50) * 2 : 0) / 100.0f);
        weatherTick(weatherObject);
        assert(seenRain == 0.2f && seenThunder == 0.1f);
        assert(weatherField<float>(weatherObject, 0x38) == levels.rain);
        assert(weatherField<float>(weatherObject, 0x44) == levels.thunder);
    }
    weatherField<float>(weatherObject, 0x38) = 0.3f; // Fresh native/packet change.
    weatherTick(weatherObject); assert(seenRain == 0.3f);
    client_set_environment_weather(false); weatherTick(weatherObject);
    assert(weatherField<float>(weatherObject, 0x38) == 0.3f);
    assert(weatherField<float>(weatherObject, 0x44) == 0.1f);
    client_set_environment_weather(true); client_set_environment_weather_amount(100);
    weatherField<unsigned long>(levelObject, 0) = 0; // Server Level.
    weatherTick(weatherObject);
    assert(weatherField<float>(weatherObject, 0x38) == 0.3f);
    weatherField<unsigned long>(levelObject, 0) = profile::weatherClientLevelTable;
    weatherTick(weatherObject);
    client_set_environment(false); weatherTick(weatherObject);
    assert(weatherField<float>(weatherObject, 0x38) == 0.3f);
    client_set_environment_weather_amount(-1); assert(saved.weatherAmount == 0);
    client_set_environment_weather_amount(101); assert(saved.weatherAmount == 100);

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
    std::memcpy(image + profile::weatherTickFunction, profile::weatherTickSignature, sizeof(profile::weatherTickSignature));
    std::memcpy(image + profile::weatherClientCheck, profile::weatherClientSignature, sizeof(profile::weatherClientSignature));
    std::memcpy(image + profile::weatherInterpolation, profile::weatherInterpolationSignature, sizeof(profile::weatherInterpolationSignature));
    const unsigned char tickReturn[] = {0x58, 0x5b, 0x5d, 0xc3};
    std::memcpy(image + profile::weatherTickFunction + sizeof(profile::weatherTickSignature), tickReturn, sizeof(tickReturn));
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
    rejected(profile::weatherTickFunction);
    rejected(profile::weatherClientCheck);
    rejected(profile::weatherInterpolation);
    assert(install(base));
    assert(originalAngle == reinterpret_cast<Angle>(base + profile::angleFunction));
    assert(slot(profile::fogTables[0] + profile::angleSlot) == reinterpret_cast<unsigned long>(&angle));
    assert(slot(profile::fogTables[0] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<0>));
    assert(slot(profile::fogTables[1] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<1>));
    assert(slot(profile::fogTables[2] + profile::fogSlot) == reinterpret_cast<unsigned long>(&fog<2>));
    // Execute the installed branch and stolen-instruction trampoline.
    weatherField<unsigned long>(dimensionObject, 0) = base + profile::fogTables[0];
    weatherField<unsigned long>(levelObject, 0) = base + profile::weatherClientLevelTable;
    weatherHistory = {};
    client_set_environment(true); client_set_environment_weather(true);
    client_set_environment_weather_amount(75);
    auto installedTick = reinterpret_cast<WeatherTick>(base + profile::weatherTickFunction);
    installedTick(weatherObject);
    assert(weatherField<float>(weatherObject, 0x38) == 1.0f);
    assert(weatherField<float>(weatherObject, 0x44) == 0.5f);
    client_set_environment_weather(false); installedTick(weatherObject);
    assert(weatherField<float>(weatherObject, 0x38) == 0.3f);
    assert(weatherField<float>(weatherObject, 0x44) == 0.1f);
    assert(munmap(image, size) == 0);

}
