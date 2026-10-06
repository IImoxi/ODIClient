#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <cstdlib>
#include <sys/mman.h>
#include "../auto_gg.cpp"
static PopupCallback popupCallback;
static void* popupContext;
static bool popupBusy;
static std::string popupTitle, popupMessage;
bool popup_show(const char* title, const char* message, PopupCallback callback, void* context, int) {
    if (popupBusy) return false;
    popupBusy = true; popupCallback = callback; popupContext = context;
    popupTitle = title; popupMessage = message; return true;
}
bool popup_cancel_if(PopupCallback callback, void* context) {
    if (!popupBusy || popupCallback != callback || popupContext != context) return false;
    popupBusy = false; callback(PopupAnswer::Cancelled, context); return true;
}
static void answerPopup(PopupAnswer answer) {
    assert(popupBusy); popupBusy = false; popupCallback(answer, popupContext);
}
constexpr auto buildNote = minecraft_build::current::buildNote;

extern "C" void* mcpelauncher_host_dlopen(const char* path, int flags) { return dlopen(path, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* lib, const char* name) { return dlsym(lib, name); }
alignas(8) static unsigned char handlerObject[96], clientObject[0x640], playerObject[3000];
static void* senderMethods[4], *playerMethods[0x6c0 / 8], *levelMethods[0x740 / 8], *gameMethods[0xc0 / 8];
static void** levelObject = levelMethods;
static void** gameObject = gameMethods;
static bool remoteServer;
static std::vector<std::string> echoed;
static bool isRemote(void*) { return remoteServer; }
static void* getLevel(void*) { return &levelObject; }
static void echo(void*, const void* author, const void* message) {
    assert(author == playerObject + 0xb10);
    const auto words = static_cast<const unsigned long*>(message);
    echoed.emplace_back(reinterpret_cast<const char*>(words[2]), words[1]);
}
static void** senderObject = senderMethods;
static std::vector<std::string> sent, commands;
static int destroyed, stringsDestroyed;
static bool hasPlayer = true;
static void* getPlayer(void*) { return hasPlayer ? playerObject : nullptr; }
static void* getSender(void*) { return &senderObject; }
static void* getIdentity(void*) { return nullptr; }
static void getXuid(void* out, void*) { std::memset(out, 0, 24); }
static void destroyXuid(void*) { ++stringsDestroyed; }
static void destroyPacket(void*) { ++destroyed; }
static void* packetMethods[] = {reinterpret_cast<void*>(destroyPacket)};
static void* createPacket(void* out, const void* author, const void* message,
                          const void* filtered, const void* platform, const void* xuid) {
    assert(author == playerObject + 0xb10);
    assert(!static_cast<const unsigned char*>(filtered)[24]);
    assert(!static_cast<const unsigned char*>(platform)[0]);
    assert(!static_cast<const unsigned char*>(xuid)[0]);
    auto bytes = static_cast<unsigned char*>(out);
    *reinterpret_cast<void***>(bytes) = packetMethods;
    *reinterpret_cast<unsigned int*>(bytes + 0xc0) = 1;
    std::memcpy(bytes + 0xa8, message, 24);
    return out;
}
static unsigned char minecraftObject[0xc0], commandEngine[0x58];
static unsigned long actorId = 1;
static CommandUuid commandUuid(int) { return {1, 2}; }
static const unsigned long* uniquePlayerId(void*) { return &actorId; }
static void destroyCommandString(void*) {}
static int submitNativeCommand(void*, const void* context, bool) {
    auto words = static_cast<const unsigned long*>(context);
    commands.emplace_back(reinterpret_cast<const char*>(words[2]), words[1]);
    return 0;
}
static void dispatchActions(void* handler) {
    auto_gg_lobby_dispatch(handler);
    chat_notify_live({clientObject});
}
static void constructString(void* out, const char* text, unsigned long size) {
    auto words = static_cast<unsigned long*>(out);
    words[0] = 257; words[1] = size; words[2] = reinterpret_cast<unsigned long>(text);
}
static void sendPacket(void*, const void* value) {
    if (*reinterpret_cast<const unsigned int*>(static_cast<const unsigned char*>(value) + 0x20) == 0x4d) {
        auto words = reinterpret_cast<const unsigned long*>(static_cast<const unsigned char*>(value) + 0x30);
        commands.emplace_back(reinterpret_cast<const char*>(words[2]), words[1]); return;
    }
    char text[4097];
    assert(packetMessage(static_cast<const unsigned char*>(value), text));
    sent.emplace_back(text);
}
static double clockTime = 100;
static int fakeClock(clockid_t, timespec* t) noexcept {
    t->tv_sec = static_cast<long>(clockTime);
    t->tv_nsec = static_cast<long>((clockTime - t->tv_sec) * 1e9);
    return 0;
}
static int forwarded;
static void original(void*, const void*, void*, void*) { ++forwarded; }
static void packet(unsigned char* bytes, const char* message, unsigned int variant = 0) {
    std::memset(bytes, 0, 208);
    *reinterpret_cast<unsigned int*>(bytes + 0xc0) = variant;
    auto field = reinterpret_cast<unsigned long*>(bytes + (variant == 1 ? 0xa8 : 0x90));
    field[0] = 4097; field[1] = std::strlen(message); field[2] = reinterpret_cast<unsigned long>(message);
}
static void receive(const char* message, unsigned int variant = 0) {
    alignas(8) unsigned char bytes[208];
    packet(bytes, message, variant);
    void* shared[] = {bytes, nullptr};
    dispatch(nullptr, nullptr, handlerObject, shared);
}
static void reset() {
    dirty = false; sent.clear(); echoed.clear(); hasPlayer = true; remoteServer = false;
    auto_gg_update(true, true);
    lastMatch = -30; enabled = true;
    copy(trigger, "You won the game", sizeof(trigger)); copy(response, "gg", sizeof(response));
}

int main() {
    auto_gg_init(); // No Minecraft mapped: must gracefully report unavailable.
    assert(!ready && error && !findGame());
    getTime = fakeClock;
    originalDispatch = original;
    ready = true; error = nullptr;
    unsigned long mockSize = buildNote + 4096;
    auto mockImage = static_cast<unsigned char*>(mmap(nullptr, mockSize, PROT_READ | PROT_WRITE,
                                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(mockImage != MAP_FAILED);
    gameBase = reinterpret_cast<unsigned long>(mockImage);
    *reinterpret_cast<void***>(handlerObject) = reinterpret_cast<void**>(mockImage + 0x16f3dc90);
    *reinterpret_cast<void**>(handlerObject + 0x58) = clientObject;
    auto clientMethods = reinterpret_cast<void**>(mockImage + 0x16e97c68);
    *reinterpret_cast<void***>(clientObject) = clientMethods;
    clientMethods[0x100 / 8] = reinterpret_cast<void*>(getPlayer);
    clientMethods[0x930 / 8] = reinterpret_cast<void*>(getSender);
    clientMethods[0x488 / 8] = reinterpret_cast<void*>(getXuid);
    clientMethods[0x5a0 / 8] = reinterpret_cast<void*>(getIdentity);
    senderMethods[3] = reinterpret_cast<void*>(sendPacket);
    *reinterpret_cast<void***>(playerObject) = playerMethods;
    playerMethods[0x6b0 / 8] = reinterpret_cast<void*>(getLevel);
    levelMethods[0x748 / 8] = reinterpret_cast<void*>(echo);
    gameMethods[0xb8 / 8] = reinterpret_cast<void*>(isRemote);
    *reinterpret_cast<void**>(clientObject + 0x188) = &gameObject;
    createChat = createPacket; destroyString = destroyXuid;
    constructNativeString = constructString;
    *reinterpret_cast<void**>(clientObject + chat::clientMinecraft) = minecraftObject;
    *reinterpret_cast<void**>(minecraftObject + chat::minecraftCommands) = commandEngine;
    *reinterpret_cast<void**>(playerObject + chat::playerLevel) = &levelObject;
    executeCommand = submitNativeCommand; playerUniqueId = uniquePlayerId; newCommandUuid = commandUuid;
    destroyString = destroyCommandString; // Command fixtures use borrowed string storage.

    // Confirmation parses both modes and sends from a live gameplay callback.
    LobbyWatchSettings lobby; lobby.enabled = true;
    auto setRules = [&](const char* rules) {
        copy(lobby.rules, rules, sizeof(lobby.rules)); client_settings_set_lobby_watch(lobby);
        commands.clear(); sent.clear(); auto_gg_update(true, true);
    };
    setRules("steve?/hub");
    auto_gg_lobby_player(handlerObject, "SuperSTEVE");
    assert(popupBusy && commands.empty() && popupMessage == "/hub");
    assert(popupTitle.find("SuperSTEVE") != std::string::npos);
    answerPopup(PopupAnswer::No); dispatchActions(handlerObject); assert(commands.empty());
    auto_gg_lobby_player(handlerObject, "steve"); answerPopup(PopupAnswer::Yes);
    assert(commands.empty()); dispatchActions(handlerObject);
    assert(commands == std::vector<std::string>{"/hub"});
    dispatchActions(handlerObject); assert(commands.size() == 1);
    setRules("steve?#/hub");
    auto_gg_lobby_player(handlerObject, "Steve");
    assert(popupBusy && sent.empty() && popupMessage == "/hub" && popupTitle.find("chat") != std::string::npos);
    answerPopup(PopupAnswer::Yes); dispatchActions(handlerObject);
    assert(sent == std::vector<std::string>{"/hub"} && commands.empty());
    setRules("steve?/hub"); auto_gg_lobby_player(handlerObject, "steve");
    auto_gg_lobby_reset(); assert(!popupBusy); dispatchActions(handlerObject); assert(commands.empty());
    auto_gg_lobby_player(handlerObject, "steve");
    lobby.enabled = false; client_settings_set_lobby_watch(lobby);
    assert(!popupBusy); dispatchActions(handlerObject); assert(commands.empty());
    lobby.enabled = true; setRules("steve?/hub"); popupBusy = true;
    auto_gg_lobby_player(handlerObject, "steve");
    assert(lobbyConfirmation.state == LobbyConfirmation::Idle && commands.empty()); popupBusy = false;
    setRules("steve?/hub"); auto_gg_lobby_player(handlerObject, "steve");
    setRules("steve?/home"); assert(!popupBusy); dispatchActions(handlerObject); assert(commands.empty());
    auto_gg_lobby_player(handlerObject, "steve"); answerPopup(PopupAnswer::Yes);
    alignas(8) unsigned char otherHandler[96]; std::memcpy(otherHandler, handlerObject, sizeof(otherHandler));
    dispatchActions(otherHandler); dispatchActions(handlerObject); assert(commands.empty());
    setRules("steve/hub"); auto_gg_lobby_player(handlerObject, "Steve");
    assert(commands == std::vector<std::string>{"/hub"} && !popupBusy);
    setRules("steve#/hub"); auto_gg_lobby_player(handlerObject, "Steve");
    assert(sent == std::vector<std::string>{"/hub"} && !popupBusy);
    setRules("steve?/hub"); auto_gg_lobby_player(handlerObject, "alex"); assert(!popupBusy);
    auto_gg_lobby_player(handlerObject, "steve"); answerPopup(PopupAnswer::Yes);
    auto_gg_update(true, false); dispatchActions(handlerObject); assert(commands.empty());
    auto_gg_update(true, true); dispatchActions(handlerObject); assert(commands.size() == 1);
    lobbyOriginals[0] = lobbyOriginals[1] = lobbyOriginals[2] = original;
    int nativeForwarded = forwarded;
    lobbyDispatch<0>(nullptr, nullptr, handlerObject, nullptr);
    lobbyDispatch<1>(nullptr, nullptr, handlerObject, nullptr);
    lobbyDispatch<2>(nullptr, nullptr, handlerObject, nullptr);
    assert(forwarded == nativeForwarded + 3);
    client_settings_set_lobby_watch({}); sent.clear(); commands.clear(); echoed.clear();
    // Party detection searches the full decoded message, independently of AutoGG.
    enabled = false;
    client_settings_set_cc_utils({true, true});
    int inviteForwarded = forwarded;
    receive("\xc2\xa7" "aYou have recieved a party invite from \xc2\xa7" "eplayer471\xc2\xa7" "a.");
    assert(popupBusy && forwarded == inviteForwarded && popupMessage.find("player471") != std::string::npos);
    answerPopup(PopupAnswer::No); dispatchActions(handlerObject); assert(commands.empty());
    receive("[Party] party invite from Alex Smith. More text");
    assert(popupMessage.find("Alex Smith") != std::string::npos);
    answerPopup(PopupAnswer::Yes); assert(commands.empty());
    auto_gg_update(true, false); dispatchActions(handlerObject); assert(commands.empty());
    auto_gg_update(true, true); dispatchActions(handlerObject);
    assert(commands == std::vector<std::string>{"/p accept Alex_Smith"});
    dispatchActions(handlerObject); assert(commands.size() == 1); commands.clear();
    receive("You have recieved a party invite from Steve.");
    receive("You have recieved a party invite from Steve."); // Same pending invite stays hidden.
    assert(popupBusy && forwarded == inviteForwarded);
    client_settings_set_cc_utils({true, false}); assert(!popupBusy);
    receive("You have recieved a party invite from Steve."); assert(forwarded == ++inviteForwarded);
    client_settings_set_cc_utils({true, true}); popupBusy = true;
    receive("You have recieved a party invite from Steve."); // Busy prompt must preserve native chat.
    assert(forwarded == ++inviteForwarded && partyInvite.state == PartyInvite::Idle); popupBusy = false;
    receive("You have recieved a party invite from Steve."); answerPopup(PopupAnswer::Yes);
    auto_gg_world_reset(); dispatchActions(handlerObject); assert(commands.empty());
    receive("You have recieved a party invite from Steve.");
    dispatchActions(otherHandler); assert(!popupBusy);
    receive("You have recieved a party invite from Bad/name.");
    receive("You have recieved a party invite from .");
    receive("Nothing here");
    assert(forwarded == inviteForwarded + 3 && !popupBusy);
    receive("You have recieved a party invite from Steve.", 3); assert(!popupBusy); // Unsupported variant.
    client_settings_set_cc_utils({});
    destroyed = stringsDestroyed = 0;

    ChatModsSettings blacklist;
    blacklist.enabled = blacklist.blacklist = true;
    copy(blacklist.keywords, " , spam , BUY NOW,, ", sizeof(blacklist.keywords));
    client_settings_set_chat_mods(blacklist);
    enabled = false;
    int before = forwarded;
    receive("Some SPAM message");
    receive("Please buy now!");
    receive("colored \xc2\xa7" "cspam");
    assert(forwarded == before);
    receive("ordinary message"); assert(forwarded == before + 1);
    receive("spam", 3); assert(forwarded == before + 2); // Unknown payloads pass through.
    blacklist.blacklist = false; client_settings_set_chat_mods(blacklist);
    receive("spam"); assert(forwarded == before + 3);
    blacklist.blacklist = true; blacklist.enabled = false; client_settings_set_chat_mods(blacklist);
    receive("spam"); assert(forwarded == before + 4);
    blacklist.enabled = true; copy(blacklist.keywords, " , , ", sizeof(blacklist.keywords));
    client_settings_set_chat_mods(blacklist);
    receive("ordinary message"); assert(forwarded == before + 5);
    client_settings_set_chat_mods({}); forwarded = 0;
    destroyString = destroyXuid;

    char decoded[4097];
    alignas(8) unsigned char bytes[208];
    packet(bytes, "\xc2\xa7" "aYou won \xc2\xa7" "lthe game!");
    assert(packetMessage(bytes, decoded) && std::strcmp(decoded, "You won the game!") == 0);
    for (unsigned int variant = 0; variant < 3; ++variant) {
        packet(bytes, "a long enough message to use a heap string", variant);
        assert(packetMessage(bytes, decoded) && std::strcmp(decoded, "a long enough message to use a heap string") == 0);
    }
    *reinterpret_cast<unsigned int*>(bytes + 0xc0) = 99;
    assert(!packetMessage(bytes, decoded));
    packet(bytes, "unused"); bytes[0x90] = 2; bytes[0x91] = 'x';
    assert(packetMessage(bytes, decoded) && std::strcmp(decoded, "x") == 0);
    bytes[0x90] = 48;
    assert(!packetMessage(bytes, decoded));
    assert(validText("gg \xf0\x9f\x91\x8d") && validText(""));
    assert(!validText("gg\n/command") && !validText("\xc0\x80") && !validText("\xed\xa0\x80")
           && !validText("\xf0\x9f") && !validText(std::string(256, 'a').c_str()));

    reset();
    enabled = false; receive("You won the game"); assert(sent.empty() && forwarded == 1);
    enabled = true; receive("You lost the game"); assert(sent.empty());
    receive("\xc2\xa7" "aCongratulations! You won the game!");
    assert(sent == std::vector<std::string>({"gg"}));
    assert(destroyed == 1 && stringsDestroyed == 1);
    assert(echoed == sent && std::strstr(auto_gg_status(), "response sent"));
    receive("You won the game"); assert(sent.size() == 1);
    clockTime += 16; receive("You won the game"); assert(sent.size() == 2);

    for (unsigned int variant = 0; variant < 3; ++variant) {
        reset(); copy(trigger, "Hi", sizeof(trigger));
        receive("Hi", variant); assert(sent == std::vector<std::string>({"gg"}));
    }
    reset(); remoteServer = true; receive("You won the game");
    assert(sent == std::vector<std::string>({"gg"}) && echoed.empty()); // No duplicate echo on remote servers.
    reset(); auto_gg_update(false, true); receive("You won the game"); assert(sent.empty());
    reset(); auto_gg_update(true, false); receive("You won the game"); assert(sent.empty());
    reset(); auto_gg_on_keyboard(0); receive("You won the game"); assert(sent.empty());
    reset(); hasPlayer = false; receive("You won the game"); assert(sent.empty());
    assert(std::strstr(auto_gg_status(), "local player unavailable"));
    hasPlayer = true; receive("You won the game"); assert(sent.size() == 1); // A failed send must not consume cooldown.
    reset(); copy(response, trigger, sizeof(response)); receive("You won the game"); assert(sent.empty());
    reset(); trigger[0] = 0; receive("anything"); assert(sent.empty());
    reset(); response[0] = 0; receive("You won the game"); assert(sent.empty());
    reset(); auto_gg_set_response("/say gg"); assert(std::strcmp(response, "gg") == 0);
    reset(); auto_gg_set_response("good game \xf0\x9f\x91\x8d");
    auto_gg_update(true, true); receive("You won the game");
    assert(sent == std::vector<std::string>({"good game \xf0\x9f\x91\x8d"}));
    // The live legacy subclass shares the base layout but has its own vptr.
    reset();
    *reinterpret_cast<void***>(handlerObject) = reinterpret_cast<void**>(mockImage + legacyHandlerVtable);
    receive("You won the game");
    assert(sent == std::vector<std::string>({"gg"}) && echoed == sent);
    // Unknown classes must still be rejected, even with a valid client pointer.
    *reinterpret_cast<void***>(handlerObject) = reinterpret_cast<void**>(mockImage + legacyHandlerVtable + 8);
    assert(!sendChat(handlerObject, "gg"));
    assert(std::strstr(auto_gg_status(), "unverified incoming chat handler"));
    *reinterpret_cast<void***>(handlerObject) = reinterpret_cast<void**>(mockImage + handlerVtable);
    assert(!sendChat(nullptr, "gg"));
    *reinterpret_cast<void***>(clientObject) = nullptr;
    assert(!sendChat(handlerObject, "gg"));
    *reinterpret_cast<void***>(clientObject) = clientMethods;
    assert(munmap(mockImage, mockSize) == 0);

    char directory[] = "/tmp/autogg-check-XXXXXX";
    assert(mkdtemp(directory));
    std::snprintf(configPath, sizeof(configPath), "%s/odiclient.conf", directory);
    std::snprintf(legacyConfigPath, sizeof(legacyConfigPath), "%s/autogg.conf", directory);
    reset(); auto_gg_set_trigger("Victory!"); auto_gg_set_response("good game \xc3\xa9");
    client_settings_set_blur(true, 144, true);
    client_settings_set_zoom(true, 90, 300, 25);
    client_settings_set_particles(true);
    client_settings_set_cc_utils({true, true});
    client_settings_set_environment({true, true, true, 18000, 240, 75, 80});
    client_settings_set_fps_display({true, true, 1750, 5, 3});
    lobby.enabled = true; copy(lobby.rules, "steve?/hub,alex?#/hello", sizeof(lobby.rules));
    client_settings_set_lobby_watch(lobby);
    client_settings_set_render({true, true, false, 80, 160, true, 96});
    auto_gg_update(true, true);
    reset(); enabled = false; savedRender = {}; savedParticles = false; savedFpsDisplay = {}; savedEnvironment = {}; savedCCUtils = {}; loadConfig();
    assert(savedCCUtils.enabled && savedCCUtils.partyInvites);
    assert(client_settings_get_fps_display().enabled && client_settings_get_fps_display().low
           && client_settings_get_fps_display().intervalMs == 1750
           && client_settings_get_fps_display().fontScale == 5 && client_settings_get_fps_display().anchor == 3);
    auto environment = client_settings_get_environment();
    assert(environment.enabled && environment.time && environment.fog && environment.ticks == 18000
           && environment.hue == 240 && environment.saturation == 75 && environment.value == 80);
    client_settings_set_environment({true, true, true, -1, 999, -1, 999});
    assert(savedEnvironment.ticks == 0 && savedEnvironment.hue == 360
           && savedEnvironment.saturation == 0 && savedEnvironment.value == 100);
    assert(client_settings_get_particles());
    assert(client_settings_get_lobby_watch().enabled && std::strcmp(client_settings_get_lobby_watch().rules, "steve?/hub,alex?#/hello") == 0);
    assert(enabled && std::strcmp(trigger, "Victory!") == 0 && std::strcmp(response, "good game \xc3\xa9") == 0);
    bool zoom; int zoomKey, zoomDefault, zoomScroll;
    client_settings_get_zoom(&zoom, &zoomKey, &zoomDefault, &zoomScroll);
    assert(zoom && zoomKey == 90 && zoomDefault == 300 && zoomScroll == 25);
    bool blurAverage, screenBlur;
    int averageHz;
    client_settings_get_blur(&blurAverage, &averageHz, &screenBlur);
    assert(blurAverage && averageHz == 144 && screenBlur);
    auto render = client_settings_get_render();
    assert(render.enabled && render.below && !render.above && render.belowDistance == 80
           && render.aboveDistance == 160 && render.horizontal && render.radius == 96);
    // Version 19 preserves enable/low and rounds its interval to the new stepped range.
    FILE* version19File = std::fopen(configPath, "r"); assert(version19File);
    char version19Lines[35][512]{};
    for (auto& line : version19Lines) assert(std::fgets(line, sizeof(line), version19File));
    std::fclose(version19File);
    for (const char* interval : {"100", "1100", "5000"}) {
        version19File = std::fopen(configPath, "w"); assert(version19File);
        std::fputs("AUTOGG19\n", version19File);
        for (int i = 1; i < 34; ++i) std::fputs(version19Lines[i], version19File);
        std::fprintf(version19File, "%s\n", interval); std::fclose(version19File);
        savedFpsDisplay = {}; dirty = false; loadConfig();
        assert(!savedEnvironment.enabled && !savedEnvironment.time && !savedEnvironment.fog
               && savedEnvironment.ticks == 6000 && savedEnvironment.value == 100);
        assert(!savedCCUtils.enabled && !savedCCUtils.partyInvites);
        int expected = !std::strcmp(interval, "100") ? 250 : !std::strcmp(interval, "1100") ? 1000 : 2000;
        assert(savedFpsDisplay.enabled && savedFpsDisplay.low && savedFpsDisplay.intervalMs == expected
               && savedFpsDisplay.fontScale == 2 && savedFpsDisplay.anchor == 0 && dirty);
    }
    client_settings_set_fps_display({true, true, 1750, 5, 3}); saveConfig();
    // Version 18 migrates existing settings and defaults the FPS overlay off.
    FILE* oldFps = std::fopen(configPath, "r"); assert(oldFps);
    char fpsLines[32][512]{};
    for (auto& line : fpsLines) assert(std::fgets(line, sizeof(line), oldFps));
    std::fclose(oldFps);
    oldFps = std::fopen(configPath, "w"); assert(oldFps);
    std::fputs("AUTOGG18\n", oldFps);
    for (int i = 1; i < 32; ++i) std::fputs(fpsLines[i], oldFps);
    std::fclose(oldFps); dirty = false; loadConfig();
    assert(!savedFpsDisplay.enabled && !savedFpsDisplay.low && savedFpsDisplay.intervalMs == 1000 && dirty);
    assert(client_settings_get_render().horizontal && client_settings_get_render().radius == 96);
    client_settings_set_fps_display({true, true, 0}); assert(savedFpsDisplay.intervalMs == 250);
    client_settings_set_fps_display({true, true, 9999}); assert(savedFpsDisplay.intervalMs == 2000);
    saveConfig(); savedFpsDisplay = {}; loadConfig();
    assert(savedFpsDisplay.enabled && savedFpsDisplay.low && savedFpsDisplay.intervalMs == 2000);
    // Invalid interval rejects the entire configuration.
    oldFps = std::fopen(configPath, "w"); assert(oldFps);
    std::fputs("AUTOGG19\n", oldFps);
    for (int i = 1; i < 32; ++i) std::fputs(fpsLines[i], oldFps);
    std::fputs("1\n1\n5001\n", oldFps); std::fclose(oldFps);
    savedFpsDisplay = {}; loadConfig(); assert(!savedFpsDisplay.enabled);
    // New format rejects non-stepped intervals, invalid scales/anchors, and missing fields.
    for (const char* extra : {"1\n1\n300\n2\n0\n", "1\n1\n1000\n11\n0\n",
                              "1\n1\n1000\n2\n4\n", "1\n1\n1000\n2\n"}) {
        oldFps = std::fopen(configPath, "w"); assert(oldFps);
        std::fputs("AUTOGG20\n", oldFps);
        for (int i = 1; i < 32; ++i) std::fputs(fpsLines[i], oldFps);
        std::fputs(extra, oldFps); std::fclose(oldFps);
        savedFpsDisplay = {}; loadConfig(); assert(!savedFpsDisplay.enabled);
    }
    client_settings_set_fps_display({true, true, 1100, -1, 5});
    assert(savedFpsDisplay.intervalMs == 1000 && savedFpsDisplay.fontScale == 0 && savedFpsDisplay.anchor == 0);
    client_settings_set_fps_display({true, true, 1125, 99, 3});
    assert(savedFpsDisplay.intervalMs == 1250 && savedFpsDisplay.fontScale == ui_scale::count - 1 && savedFpsDisplay.anchor == 3);
    dirty = true; saveConfig();
    // Previous format keeps existing settings and defaults the new cutoff OFF.
    FILE* oldRender = std::fopen(configPath, "r"); assert(oldRender);
    char oldLines[30][512]{};
    for (auto& line : oldLines) assert(std::fgets(line, sizeof(line), oldRender));
    std::fclose(oldRender);
    oldRender = std::fopen(configPath, "w"); assert(oldRender);
    std::fputs("AUTOGG17\n", oldRender);
    for (int i = 1; i < 30; ++i) std::fputs(oldLines[i], oldRender);
    std::fclose(oldRender); loadConfig();
    render = client_settings_get_render();
    assert(render.enabled && render.belowDistance == 80 && !render.horizontal && render.radius == 128 && dirty);
    assert(client_settings_get_lobby_watch().enabled);
    assert(!client_settings_get_fps_display().enabled && !client_settings_get_fps_display().low
           && client_settings_get_fps_display().intervalMs == 1000);
    client_settings_set_center_cursor(true);
    auto_gg_toggle(); auto_gg_update(true, true);
    enabled = true; savedCenterCursor = false; loadConfig(); assert(!enabled && client_settings_get_center_cursor());
    unlink(configPath);
    FILE* legacy = std::fopen(legacyConfigPath, "w"); assert(legacy);
    std::fputs("AUTOGG3\n1\nLegacy!\ngg\n0\n1\n35\n1\n120\n", legacy);
    std::fclose(legacy);
    loadConfig();
    assert(std::strcmp(trigger, "Legacy!") == 0 && savedBlur && savedBlurStrength == 35);
    client_settings_get_blur(&blurAverage, &averageHz, &screenBlur);
    assert(!blurAverage && averageHz == 60 && !screenBlur);
    client_settings_get_zoom(&zoom, &zoomKey, &zoomDefault, &zoomScroll);
    assert(!zoom && zoomKey == 67 && zoomDefault == 30 && zoomScroll == 5);
    auto_gg_update(true, true);
    FILE* migrated = std::fopen(configPath, "r"); assert(migrated);
    char version[16]{};
    assert(std::fgets(version, sizeof(version), migrated) && std::strcmp(version, "AUTOGG22\n") == 0);
    std::fclose(migrated);
    FILE* previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG5\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n", previous);
    std::fclose(previous); loadConfig();
    client_settings_get_zoom(&zoom, &zoomKey, &zoomDefault, &zoomScroll);
    assert(zoom && zoomKey == 90 && zoomDefault == 30 && zoomScroll == 5 && dirty);
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG6\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n", previous);
    std::fclose(previous); loadConfig();
    client_settings_get_zoom(&zoom, &zoomKey, &zoomDefault, &zoomScroll);
    assert(zoom && zoomKey == 90 && zoomDefault == 80 && zoomScroll == 5 && dirty);
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG8\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n", previous);
    std::fclose(previous); loadConfig();
    render = client_settings_get_render();
    assert(!render.enabled && !render.below && !render.above
           && render.belowDistance == 64 && render.aboveDistance == 128
           && render.aboveDistance == 128);
    assert(client_settings_get_center_cursor() && dirty);
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG9\n0\nBad render settings\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n1\n1\n1\n15\n256\n", previous);
    std::fclose(previous); loadConfig();
    assert(!client_settings_get_render().enabled && std::strcmp(trigger, "Victory!") == 0);
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG9\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n1\n1\n0\n16\n32\n", previous);
    std::fclose(previous); loadConfig();
    render = client_settings_get_render();
    assert(render.enabled && render.below && !render.above && render.belowDistance == 16
           && render.aboveDistance == 32 && dirty);
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG10\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n1\n1\n0\n16\n32\n1\n0\n", previous);
    std::fclose(previous); loadConfig();
    render = client_settings_get_render();
    assert(render.enabled && render.below && !render.above && render.belowDistance == 16
           && render.aboveDistance == 32 && dirty);
    auto_gg_update(true, true);
    migrated = std::fopen(configPath, "r"); assert(migrated);
    std::memset(version, 0, sizeof(version));
    assert(std::fgets(version, sizeof(version), migrated) && std::strcmp(version, "AUTOGG22\n") == 0);
    std::fclose(migrated);
    client_settings_set_tablist(false); saveConfig();
    savedTablist = true; loadConfig(); assert(!client_settings_get_tablist());
    client_settings_set_tablist(true); saveConfig();
    savedTablist = false; loadConfig(); assert(client_settings_get_tablist());
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG11\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n1\n1\n0\n16\n32\n", previous);
    std::fclose(previous); savedTablist = false; dirty = false; loadConfig();
    assert(client_settings_get_tablist() && dirty);
    // Version 12 retains tablist and render settings, and defaults Particles OFF.
    previous = std::fopen(configPath, "w"); assert(previous);
    std::fputs("AUTOGG12\n0\nVictory!\ngg\n0\n0\n30\n0\n120\n0\n60\n0\n1\n90\n80\n5\n1\n1\n1\n0\n16\n32\n0\n", previous);
    std::fclose(previous); savedParticles = true; dirty = false; loadConfig();
    assert(!client_settings_get_particles() && !client_settings_get_tablist() && dirty);
    client_settings_set_particles(true); saveConfig();
    savedParticles = false; loadConfig(); assert(client_settings_get_particles());
    previous = std::fopen(configPath, "a"); assert(previous);
    std::fputs("extra\n", previous); std::fclose(previous);
    savedParticles = false; loadConfig(); assert(!client_settings_get_particles());
    FILE* bad = std::fopen(configPath, "w"); assert(bad);
    std::fputs("AUTOGG1\n1\nVictory!\n/command\n", bad); std::fclose(bad);
    blacklist.enabled = blacklist.blacklist = true;
    copy(blacklist.keywords, "spam,\n buy now, C:\\path", sizeof(blacklist.keywords));
    client_settings_set_chat_mods(blacklist); saveConfig();
    savedChatMods = {}; loadConfig();
    assert(savedChatMods.enabled && savedChatMods.blacklist);
    assert(std::strcmp(savedChatMods.keywords, "spam,\n buy now, C:\\path") == 0);
    client_settings_set_chat_mods({}); saveConfig();

    reset(); loadConfig(); // Invalid persisted commands must not become responses.
    assert(std::strcmp(response, "gg") == 0);
    unlink(configPath); unlink(legacyConfigPath); rmdir(directory);
    configPath[0] = legacyConfigPath[0] = 0;

    // Exercise the actual build-ID gate and read-only vtable patch on a sparse
    // anonymous image. A wrong ID or occupied slot must never be overwritten.
    unsigned long size = buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(image != MAP_FAILED);
    unsigned long base = reinterpret_cast<unsigned long>(image);
    auto note = reinterpret_cast<unsigned int*>(image + buildNote);
    note[0] = 4; note[1] = 20; note[2] = 3; note[3] = 0x00554e47;
    auto slot = reinterpret_cast<Dispatch*>(image + dispatcherSlot);
    *slot = reinterpret_cast<Dispatch>(base + dispatchFunction);
    assert(!installHook(base));
    std::memcpy(image + buildNote + 16, minecraft_build::current::buildId, sizeof(minecraft_build::current::buildId));
    assert(!installHook(base)); // Exact dispatcher instructions are required too.
    std::memcpy(image + dispatchFunction, chat::dispatchSignature, sizeof(chat::dispatchSignature));
    *slot = original; assert(!installHook(base) && *slot == original);
    *slot = reinterpret_cast<Dispatch>(base + dispatchFunction);
    assert(!installHook(base)); // Sender getters must also match the verified build.
    auto table = reinterpret_cast<unsigned long*>(image + clientVtable);
    table[0x100 / 8] = base + 0xa7efaa0;
    table[chat::clientMinecraftSlot / 8] = base + chat::clientMinecraftGetter;
    const unsigned long functions[] = {chat::clientMinecraftGetter, chat::nativeCommandExecute, chat::playerUniqueIdGetter,
                                       chat::commandUuidGenerator, chat::commandOriginData, chat::clientPlayerGetter};
    const unsigned char* signatures[] = {chat::clientMinecraftGetterSignature, chat::nativeCommandExecuteSignature,
                                         chat::playerUniqueIdGetterSignature, chat::commandUuidGeneratorSignature,
                                         chat::commandOriginDataSignature, chat::clientPlayerGetterSignature};
    const unsigned long sizes[] = {sizeof(chat::clientMinecraftGetterSignature), sizeof(chat::nativeCommandExecuteSignature),
                                    sizeof(chat::playerUniqueIdGetterSignature), sizeof(chat::commandUuidGeneratorSignature),
                                    sizeof(chat::commandOriginDataSignature), sizeof(chat::clientPlayerGetterSignature)};
    for (int i = 0; i < 6; ++i) {
        std::memcpy(image + functions[i], signatures[i], sizes[i]);
        mprotect(image + (functions[i] & ~(hooks::page_size() - 1)), hooks::page_size(), PROT_READ | PROT_EXEC);
    }
    *reinterpret_cast<unsigned long*>(image + chat::playerCommandOriginVtable + 0xd0) = base + chat::commandOriginType;
    *reinterpret_cast<unsigned long*>(image + chat::playerCommandOriginVtable + 0xd8) = base + chat::commandOriginData;
    table[0x488 / 8] = base + 0xa7fe9c0;
    table[0x5a0 / 8] = base + 0xa800590;
    table[0x930 / 8] = base + 0xa80bc40;
    table[0x280 / 8] = base + 0xa7fb4d0;
    std::memcpy(image + chat::commandPacketConstructor, chat::commandPacketConstructorSignature,
                sizeof(chat::commandPacketConstructorSignature));
    std::memcpy(image + chat::nativeStringConstructor, chat::nativeStringConstructorSignature,
                sizeof(chat::nativeStringConstructorSignature));
    *reinterpret_cast<unsigned long*>(image + chat::commandPacketVtable + 0x10) = base + chat::commandPacketGetId;
    std::memcpy(image + chat::commandPacketGetId, chat::commandPacketGetIdSignature,
                sizeof(chat::commandPacketGetIdSignature));
    assert(!installHook(base) && *slot == reinterpret_cast<Dispatch>(base + dispatchFunction));
    *reinterpret_cast<unsigned long*>(image + handlerVtable + 0x120) = base + handleTextFunction;
    assert(!installHook(base)); // Both incoming handler paths must be verified.
    *reinterpret_cast<unsigned long*>(image + legacyHandlerVtable + 0x120) = base + handleTextFunction;
    assert(!installHook(base)); // Deferred dispatch paths are gated too.
    for (int i = 0; i < 3; ++i) {
        unsigned char signature[] = {0x48,0x89,0xd7,0x48,0x8b,0x11,0x48,0x8b,0x07,0x48,0x8b,0x80,0,0,0,0,0xff,0xe0};
        for (int j = 0; j < 4; ++j) signature[12 + j] = (chat::lobbyHandlerSlots[i] >> (j * 8)) & 255;
        std::memcpy(image + chat::lobbyDispatchFunctions[i], signature, sizeof(signature));
        *reinterpret_cast<unsigned long*>(image + chat::lobbyDispatchSlots[i]) = base + chat::lobbyDispatchFunctions[i];
        *reinterpret_cast<unsigned long*>(image + handlerVtable + chat::lobbyHandlerSlots[i]) = base + chat::lobbyBaseHandlers[i];
        *reinterpret_cast<unsigned long*>(image + legacyHandlerVtable + chat::lobbyHandlerSlots[i]) = base + chat::lobbyLegacyHandlers[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (unsigned long address : {chat::lobbyDispatchFunctions[i], chat::lobbyBaseHandlers[i], chat::lobbyLegacyHandlers[i]})
            assert(mprotect(image + (address & ~(hooks::page_size() - 1)), hooks::page_size(), PROT_READ | PROT_EXEC) == 0);
    }
    void* slotPage = image + (dispatcherSlot & ~(hooks::page_size() - 1));
    assert(mprotect(slotPage, hooks::page_size(), PROT_READ) == 0);
    assert(installHook(base) && *slot == dispatch);
    assert(!installHook(base)); // Do not chain/install twice.
    assert(munmap(image, size) == 0);
    std::puts("PASS: shared chat listeners/filtering, party keyword matching, popup answers/deferred commands/world reset, AutoGG cooldown/settings/sender ABI/local echo, Lobby Scanner confirmation, and native build gate");
}
