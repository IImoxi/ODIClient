#include <initializer_list>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../player_target.cpp"
namespace hooks {
bool initialize() { return false; }
unsigned long find_game() { return 0; }
bool supported(unsigned long) { return false; }
bool matches(unsigned long, unsigned long, const unsigned char*, unsigned long) { return false; }
bool matches_pointer(unsigned long, unsigned long, unsigned long) { return false; }
}
int main() {
    alignas(8) unsigned char local[0xd00]{}, actor[0xd00]{}, second[0xd00]{};
    gameBase = 0x1000;
    const auto put = [](unsigned char* p, unsigned long offset, unsigned long value) {
        std::memcpy(p + offset, &value, sizeof(value));
    };
    put(local, 0, gameBase + actor_profile::localPlayerTable);
    put(actor, 0, gameBase + actor_profile::remotePlayerTable);
    put(second, 0, gameBase + actor_profile::remotePlayerTable);
    for (auto p : {local, actor, second}) { put(p, chat_profile::playerLevel, 123); put(p, profile::actorDimension, 456); }
    float box[] = {-0.3f, 0, 1000, 0.3f, 1.8f, 1000.6f};
    float nearer[] = {-0.3f, 0, 500, 0.3f, 1.8f, 500.6f};
    put(actor, profile::actorShape, reinterpret_cast<unsigned long>(box));
    put(second, profile::actorShape, reinterpret_cast<unsigned long>(nearer));
    auto name = [](unsigned char* p, const char* value) {
        p[profile::actorName] = std::strlen(value) * 2;
        std::strcpy(reinterpret_cast<char*>(p + profile::actorName + 1), value);
    };
    name(actor, "Far Player"); name(second, "Near Player");
    char output[65]{};
    Search search {local, {{0, 1, 0}}, {{0, 0, 1}}, __builtin_inff(), output, sizeof(output), false};
    Callable callable {callableTable, &search};
    assert(visit(&callable, actor) && search.found && !std::strcmp(output, "Far Player"));
    assert(visit(&callable, second) && !std::strcmp(output, "Near Player"));
    assert(visit(&callable, local) && !std::strcmp(output, "Near Player"));
    search.found = false; search.closest = __builtin_inff();
    put(actor, profile::actorDimension, 999);
    visit(&callable, actor); assert(!search.found);
    put(actor, profile::actorDimension, 456);
    box[0] = 2; box[3] = 3;
    visit(&callable, actor); assert(!search.found); // Parallel ray misses X slab.
    box[0] = -0.3f; box[3] = 0.3f; box[2] = -1000; box[5] = -999;
    visit(&callable, actor); assert(!search.found); // Behind the camera.
    box[2] = 1000; box[5] = 1000.6f;
    name(actor, "Bad\"Player"); visit(&callable, actor); assert(!search.found);
    search.closest = __builtin_inff();
    const char* longName = "A long player name for long storage";
    actor[profile::actorName] = 1;
    put(actor, profile::actorName + 8, std::strlen(longName));
    put(actor, profile::actorName + 16, reinterpret_cast<unsigned long>(longName));
    visit(&callable, actor); assert(search.found && !std::strcmp(output, longName));
    search.capacity = 5; search.found = false; search.closest = __builtin_inff();
    visit(&callable, actor); assert(!search.found);
    unsigned char camera[0x118]{};
    const float cameraOrigin[] = {10, 20, 30};
    std::memcpy(camera + profile::cameraOrigin, cameraOrigin, sizeof(cameraOrigin));
    float inverse[16]{};
    inverse[0] = inverse[5] = inverse[10] = inverse[15] = 1;
    Vec origin{}, direction{};
    assert(rayFromInverse(camera, inverse, origin, direction));
    assert(origin.v[0] == 10 && origin.v[1] == 20 && origin.v[2] == 29);
    assert(direction.v[0] == 0 && direction.v[1] == 0 && direction.v[2] == 2);
    // A rotated view projects forward along X instead of world Z.
    inverse[10] = 0; inverse[8] = 1;
    assert(rayFromInverse(camera, inverse, origin, direction));
    assert(origin.v[0] == 9 && origin.v[2] == 30 && direction.v[0] == 2 && direction.v[2] == 0);
    // Homogeneous division is independent for near and far endpoints.
    inverse[11] = 0.5f;
    assert(rayFromInverse(camera, inverse, origin, direction));
    assert(origin.v[0] == 8 && direction.v[0] > 2.66f && direction.v[0] < 2.67f);
    inverse[15] = 0; inverse[11] = 0;
    assert(!rayFromInverse(camera, inverse, origin, direction));
    inverse[15] = 1; inverse[8] = __builtin_nanf("");
    assert(!rayFromInverse(camera, inverse, origin, direction));
    player_target_init(); assert(player_target_error());
    assert(!player_target_name({nullptr}, output, sizeof(output)) && !output[0]);
    std::puts("player target checks passed");
}
