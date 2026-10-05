#include "particles.h"
#include "client_modules.h"
#include "client_settings.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::particles;
using Attack = bool (*)(void*, void*, const void*);
using Emit = void (*)(void*, void*, int);
Attack originals[2];
Emit emit;
unsigned long gameBase;
bool ready, enabled, gameplay;
const char* error = "Particles: initializing";

template<class T> T field(const void* object, unsigned long offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object) + offset);
}
bool attack(unsigned int index, void* mode, void* target, const void* item) {
    if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE)
        && __atomic_load_n(&enabled, __ATOMIC_RELAXED)
        && __atomic_load_n(&gameplay, __ATOMIC_ACQUIRE) && mode && target) {
        auto player = field<void*>(mode, profile::modePlayer);
        auto targetTable = field<unsigned long>(target, 0);
        // Native arguments live only during this callback; never cache Actors.
        // The exact LocalPlayer check excludes server-side GameModes in local worlds.
        if (player && player != target
            && field<unsigned long>(player, 0) == gameBase + profile::localPlayerTable
            && (targetTable == gameBase + profile::remotePlayerTable
                || targetTable == gameBase + profile::localPlayerTable)
            && field<void*>(player, profile::localParticleContext))
            emit(player, target, 16);
    }
    // Emit before forwarding: integrated-server attacks can remove the target.
    // Every original argument and the native return value are preserved.
    return originals[index](mode, target, item);
}
bool baseAttack(void* mode, void* target, const void* item) { return attack(0, mode, target, item); }
bool survivalAttack(void* mode, void* target, const void* item) { return attack(1, mode, target, item); }
bool install(unsigned long base) {
    if (!hooks::supported(base)
        || !hooks::matches(base, profile::attackFunctions[0], profile::attackEntry, sizeof(profile::attackEntry))
        || !hooks::matches(base, profile::attackFunctions[1], profile::survivalAttackEntry, sizeof(profile::survivalAttackEntry))
        || !hooks::readable(base, profile::attackFunctions[0], sizeof(profile::attackEntry), true)
        || !hooks::readable(base, profile::attackFunctions[1], sizeof(profile::survivalAttackEntry), true)
        || !hooks::readable(base, profile::attackBody, profile::attackBodySize, true)
        || !hooks::matches(base, profile::attackPlayerSite, profile::attackPlayerRead, sizeof(profile::attackPlayerRead))
        || !hooks::readable(base, profile::criticalEmitter, sizeof(profile::criticalEntry), true)
        || !hooks::readable(base, profile::emitterBody, profile::emitterBodySize, true)
        || !hooks::matches(base, profile::criticalEmitter, profile::criticalEntry, sizeof(profile::criticalEntry))
        || !hooks::matches(base, profile::emitterBody, profile::emitterEntry, sizeof(profile::emitterEntry))
        || !hooks::matches_pointer(base, profile::localPlayerTable + profile::criticalSlot, profile::criticalEmitter)
        || !hooks::readable(base, profile::remotePlayerTable, 8)) return false;
    hooks::Patch patches[2];
    const Attack replacements[] = {baseAttack, survivalAttack};
    for (unsigned int i = 0; i < 2; ++i) {
        if (!hooks::matches_pointer(base, profile::attackTables[i] + profile::attackSlot,
                                   profile::attackFunctions[i])) return false;
        originals[i] = reinterpret_cast<Attack>(base + profile::attackFunctions[i]);
        patches[i] = {profile::attackTables[i] + profile::attackSlot,
                      base + profile::attackFunctions[i], reinterpret_cast<unsigned long>(replacements[i])};
    }
    gameBase = base;
    emit = reinterpret_cast<Emit>(base + profile::criticalEmitter);
    // Retained hooks continue forwarding, with behavior inactive.
    return hooks::install("Particles", base, patches, 2) == hooks::InstallResult::Installed;
}
}
void particles_init() {
    static bool initialized;
    if (initialized) return;
    initialized = true;
    __atomic_store_n(&enabled, client_settings_get_particles(), __ATOMIC_RELAXED);
    const char* failure = "Particles unavailable: unsupported build or attack/particle ABI";
    if (hooks::initialize()) {
        auto base = hooks::find_game();
        if (base && install(base)) {
            __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
            failure = nullptr;
        }
    }
    __atomic_store_n(&error, failure, __ATOMIC_RELEASE);
}
void particles_update(bool value) { __atomic_store_n(&gameplay, value, __ATOMIC_RELEASE); }
const char* particles_error() { return __atomic_load_n(&error, __ATOMIC_ACQUIRE); }
void client_set_particles(bool value) {
    __atomic_store_n(&enabled, value, __ATOMIC_RELAXED);
    client_settings_set_particles(value);
}
bool client_particles_enabled() { return __atomic_load_n(&enabled, __ATOMIC_RELAXED); }
