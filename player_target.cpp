#include "player_target.h"
#include "hook_manager.h"
#include "minecraft_build.h"

namespace {
namespace profile = minecraft_build::current::player_target;
namespace chat_profile = minecraft_build::current::chat;
namespace actor_profile = minecraft_build::current::particles;
unsigned long gameBase;
const char* error = "Player targeting: initializing";
template<class T> T field(const void* p, unsigned long offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(p) + offset);
}
bool finite(float value) { return __builtin_isfinite(value); }
struct Vec { float v[3]; };
struct Search {
    void* local;
    Vec origin, direction;
    float closest;
    char* name;
    unsigned long capacity;
    bool found;
};
// libc++ std::function<bool(Player const&)> uses a 32-byte inline buffer followed
// by its callable pointer. The native iterator moves it, invokes slot +0x30,
// and destroys it. Our stack callable is deliberately outside that buffer;
// its destroy slots do nothing and no native pointer survives the call.
struct Callable { const unsigned long* table; Search* search; };
void destroy(void*) {}
bool visit(Callable* callable, void* actor) {
    Search& s = *callable->search;
    if (!actor || actor == s.local) return true;
    const auto table = field<unsigned long>(actor, 0);
    if (table != gameBase + actor_profile::remotePlayerTable
        && table != gameBase + actor_profile::localPlayerTable) return true;
    // Same live level excludes dimension transitions and unrelated local worlds.
    if (field<void*>(actor, chat_profile::playerLevel)
        != field<void*>(s.local, chat_profile::playerLevel)
        || field<void*>(actor, profile::actorDimension)
            != field<void*>(s.local, profile::actorDimension)) return true;
    const auto box = field<const float*>(actor, profile::actorShape);
    if (!box) return true;
    float near = 0, far = __builtin_inff();
    for (unsigned i = 0; i < 3; ++i) {
        const float low = box[i], high = box[i + 3];
        if (!finite(low) || !finite(high) || low > high) return true;
        const float d = s.direction.v[i], origin = s.origin.v[i];
        if (d == 0) { if (origin < low || origin > high) return true; continue; }
        float a = (low - origin) / d, b = (high - origin) / d;
        if (a > b) { float swap = a; a = b; b = swap; }
        if (a > near) near = a;
        if (b < far) far = b;
        if (near > far) return true;
    }
    if (!finite(near) || far < 0 || near >= s.closest) return true;
    // A closer player with an unusable name still blocks players behind them.
    s.closest = near;
    s.found = false;
    s.name[0] = 0;
    const auto native = static_cast<const unsigned char*>(actor) + profile::actorName;
    const bool longString = native[0] & 1;
    const unsigned long length = longString ? field<unsigned long>(native, 8) : native[0] >> 1;
    const char* text = longString ? field<const char*>(native, 16)
                                  : reinterpret_cast<const char*>(native + 1);
    if (!text || !length || length >= s.capacity || length > 64
        || (!longString && length > 22)) return true;
    for (unsigned long i = 0; i < length; ++i) {
        const unsigned char c = text[i];
        // Copy plain names only; callers apply their command's argument format.
        if (c < 32 || c == 127 || c == '"' || c == '\\') return true;
    }
    for (unsigned long i = 0; i < length; ++i) s.name[i] = text[i];
    s.name[length] = 0;
    s.found = true;
    return true;
}
const unsigned long callableTable[] = {0, 0, 0, 0,
    reinterpret_cast<unsigned long>(destroy), reinterpret_cast<unsigned long>(destroy),
    reinterpret_cast<unsigned long>(visit)};
struct alignas(16) NativeFunction { unsigned char storage[32]; Callable* callable; };
bool rayFromInverse(void* camera, const float* inverse, Vec& origin, Vec& direction) {
    // Native picking unprojects clip-space far (+1) and near (-1), then adds
    // camera world origin. Crosshair center has clip-space x/y zero.
    Vec nearPoint, farPoint;
    for (unsigned end = 0; end < 2; ++end) {
        const float z = end ? 1.f : -1.f;
        const float w = inverse[11] * z + inverse[15];
        if (!finite(w) || w == 0) return false;
        Vec& point = end ? farPoint : nearPoint;
        for (unsigned i = 0; i < 3; ++i) {
            point.v[i] = (inverse[8 + i] * z + inverse[12 + i]) / w
                         + field<float>(camera, profile::cameraOrigin + i * 4);
            if (!finite(point.v[i])) return false;
        }
    }
    float squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        origin.v[i] = nearPoint.v[i];
        direction.v[i] = farPoint.v[i] - nearPoint.v[i];
        squared += direction.v[i] * direction.v[i];
    }
    if (!finite(squared) || squared <= 0) return false;
    return true; // Ray parameter scaling does not affect closest intersection.
}
bool ray(void* camera, Vec& origin, Vec& direction) {
    float product[16], inverse[16];
    using Multiply = void* (*)(float*, const void*, const void*);
    using Inverse = void* (*)(float*, const float*);
    const auto bytes = static_cast<const unsigned char*>(camera);
    reinterpret_cast<Multiply>(gameBase + profile::matrixMultiply)(
        product, bytes + profile::cameraView, bytes + profile::cameraProjection);
    reinterpret_cast<Inverse>(gameBase + profile::matrixInverse)(inverse, product);
    return rayFromInverse(camera, inverse, origin, direction);

}
}
void player_target_init() {
    static bool initialized;
    if (initialized) return;
    initialized = true;
    const char* failure = "Ping unavailable: unsupported player targeting ABI";
    if (hooks::initialize()) {
        auto base = hooks::find_game();
        bool valid = base && hooks::supported(base)
            && hooks::matches_pointer(base, profile::clientLevelTable + profile::forEachPlayerSlot,
                                       profile::forEachPlayer);
#define CHECK_TARGET(name) valid = valid && hooks::matches(base, profile::name, profile::name##Signature, sizeof(profile::name##Signature))
        valid = valid && hooks::matches(base, chat_profile::clientPlayerGetter,
            chat_profile::clientPlayerGetterSignature, sizeof(chat_profile::clientPlayerGetterSignature));
        CHECK_TARGET(playerNameRead);
        CHECK_TARGET(cameraGetter);
        CHECK_TARGET(matrixMultiply);
        CHECK_TARGET(matrixInverse);
        CHECK_TARGET(forEachPlayer);
        CHECK_TARGET(playerIteration);
        CHECK_TARGET(playerIterationCall);
        CHECK_TARGET(shapeRead);
        CHECK_TARGET(cameraPickRead);
        CHECK_TARGET(cameraMatrices);
#undef CHECK_TARGET
        if (valid) { gameBase = base; failure = nullptr; }
    }
    __atomic_store_n(&error, failure, __ATOMIC_RELEASE);
}
const char* player_target_error() { return __atomic_load_n(&error, __ATOMIC_ACQUIRE); }
bool player_target_name(const ChatLiveContext& context, char* name, unsigned long capacity) {
    if (!name || !capacity) return false;
    name[0] = 0;
    if (player_target_error() || !context.client
        || field<unsigned long>(context.client, 0) != gameBase + chat_profile::clientVtable) return false;
    using Getter = void* (*)(void*);
    // Direct original getter avoids another live-listener notification.
    void* local = reinterpret_cast<Getter>(gameBase + chat_profile::clientPlayerGetter)(context.client);
    if (!local || field<unsigned long>(local, 0) != gameBase + actor_profile::localPlayerTable) return false;
    void* level = field<void*>(local, chat_profile::playerLevel);
    if (!level || field<unsigned long>(level, 0) != gameBase + profile::clientLevelTable) return false;
    void* camera = reinterpret_cast<Getter>(gameBase + profile::cameraGetter)(context.client);
    if (!camera) return false;
    Search search {local, {}, {}, __builtin_inff(), name, capacity, false};
    if (!ray(camera, search.origin, search.direction)) return false;
    Callable callable {callableTable, &search};
    NativeFunction function {{}, &callable};
    using Iterate = void (*)(void*, NativeFunction*);
    reinterpret_cast<Iterate>(gameBase + profile::forEachPlayer)(level, &function);
    return search.found;
}
