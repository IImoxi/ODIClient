#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "launcher_api.h"
#include "auto_gg.h"
#include "client_settings.h"
#include "ui_scale.h"
#include "minecraft_build.h"
#include "hook_manager.h"
#include "popup.h"
#include "chat.h"

namespace {
namespace chat = minecraft_build::current::chat;
constexpr auto dispatcherSlot = chat::dispatcherSlot, dispatchFunction = chat::dispatchFunction;
constexpr auto chatConstructor = chat::constructor, stringDestructor = chat::stringDestructor;
constexpr auto clientVtable = chat::clientVtable, handlerVtable = chat::handlerVtable;
constexpr auto legacyHandlerVtable = chat::legacyHandlerVtable;
constexpr auto handleTextFunction = chat::handleTextFunction;
using Dispatch = void (*)(void*, const void*, void*, void*);
Dispatch originalDispatch, lobbyOriginals[3];
void* (*originalPlayerGetter)(void*);
unsigned char liveDispatchBusy;
struct LobbyConfirmation {
    enum State { Idle, Waiting, Approved } state = Idle;
    unsigned long ticket = 0, handlerIdentity = 0, clientIdentity = 0;
    bool publicText = false;
    char text[256]{};
};
LobbyConfirmation lobbyConfirmation;
unsigned long lobbyTicket;
struct PartyInvite {
    enum State { Idle, Waiting, Approved } state = Idle;
    unsigned long ticket = 0, handlerIdentity = 0, clientIdentity = 0;
    char player[65]{};
};
PartyInvite partyInvite;
unsigned long partyTicket;
decltype(&fopen) openFile;
decltype(&fgets) getLine;
decltype(&fclose) closeFile;
decltype(&fwrite) writeFile;
decltype(&rename) renameFile;
decltype(&sscanf) scan;
decltype(&clock_gettime) getTime;
bool ready, enabled, dirty, canSend;
bool savedCenterCursor = false;
bool savedTablist = true;
bool savedTablistMojangles;
bool savedParticles = false;
RenderSettings savedRender;
EnvironmentSettings savedEnvironment;
FpsDisplaySettings savedFpsDisplay;
ChatModsSettings savedChatMods;
LobbyWatchSettings savedLobbyWatch;
CCUtilsSettings savedCCUtils;
bool savedZoom;
int savedZoomKey = 67, savedZoomDefault = 30, savedZoomScroll = 5;
bool savedSprint, savedBlur;
int savedBlurStrength = 30;
bool savedBlurFpsAverage, savedScreenBlur;
int savedBlurAverageHz = 60;
bool savedFpsLimitEnabled;
int savedFpsLimit = 120;
const char* blockedReason = "AutoGG: waiting for gameplay";
unsigned long gameBase;
using CreateChat = void* (*)(void*, const void*, const void*, const void*, const void*, const void*);
CreateChat createChat;
using ConstructNativeString = void (*)(void*, const char*, unsigned long);
ConstructNativeString constructNativeString;
struct CommandUuid { unsigned long low, high; };
CommandUuid (*newCommandUuid)(int);
int (*executeCommand)(void*, const void*, bool);
const unsigned long* (*playerUniqueId)(void*);
void (*destroyString)(void*);
unsigned char stateLock;
char trigger[256] = "You won the game", response[256] = "gg";
char configPath[4096], legacyConfigPath[4096];
double lastMatch = -30;
const char* status = "";
const char* error = "AutoGG: initializing";

struct Lock {
    Lock() { while (__atomic_test_and_set(&stateLock, __ATOMIC_ACQUIRE)) {} }
    ~Lock() { __atomic_clear(&stateLock, __ATOMIC_RELEASE); }
};
unsigned long length(const char* s) { unsigned long n = 0; while (s[n]) ++n; return n; }
bool sameText(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
void copy(char* out, const char* in, unsigned long capacity) {
    unsigned long i = 0;
    for (; i + 1 < capacity && in[i]; ++i) out[i] = in[i];
    out[i] = 0;
}
bool contains(const char* haystack, const char* needle) {
    if (!*needle) return false;
    for (; *haystack; ++haystack) {
        unsigned long i = 0;
        while (needle[i] && haystack[i] == needle[i]) ++i;
        if (!needle[i]) return true;
    }
    return false;
}
// ASCII case folding leaves UTF-8 keywords intact.
unsigned char chatLower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool blacklisted(const char* text) {
    if (!savedChatMods.enabled || !savedChatMods.blacklist) return false;
    const char* entry = savedChatMods.keywords;
    while (*entry) {
        const char* end = entry;
        while (*end && *end != ',') ++end;
        const char* start = entry;
        while (start < end && (*start == ' ' || *start == '\n')) ++start;
        const char* trimmedEnd = end;
        while (trimmedEnd > start && (trimmedEnd[-1] == ' ' || trimmedEnd[-1] == '\n')) --trimmedEnd;
        if (start < trimmedEnd) for (const char* at = text; *at; ++at) {
            const char* keyword = start;
            const char* message = at;
            while (keyword < trimmedEnd && *message && chatLower(*message) == chatLower(*keyword)) {
                ++message; ++keyword;
            }
            if (keyword == trimmedEnd) return true;
        }
        entry = *end ? end + 1 : end;
    }
    return false;
}
double now() {
    timespec t{};
    getTime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}
// Single-line UTF-8 only. Reject incomplete/oversized input instead of silently
// cutting a character. Empty trigger/response intentionally suppress sending.
bool validText(const char* text, bool multiline = false) {
    if (!text) return false;
    unsigned long n = 0;
    while (text[n]) {
        unsigned char c = text[n++];
        if (n > 255 || (c < 32 && !(multiline && c == '\n')) || c == 127) return false;
        if (c < 128) continue;
        int extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2
                    : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
        if (extra < 0) return false;
        unsigned char first = text[n];
        if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0)
            || (c == 0xf0 && first < 0x90) || (c == 0xf4 && first >= 0x90)) return false;
        while (extra--) {
            unsigned char continuation = text[n++];
            if (n > 255 || continuation < 0x80 || continuation > 0xbf) return false;
        }
    }
    return true;
}
int parseRange(const char* text, int minimum, int maximum) {
    unsigned long n = length(text);
    if (n < 1 || n > 5) return -1;
    int value = 0;
    for (unsigned long i = 0; i < n; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        value = value * 10 + text[i] - '0';
    }
    return value >= minimum && value <= maximum ? value : -1;
}
int parseFpsLimit(const char* text) { return parseRange(text, 30, 480); }
unsigned long formatIntLine(int value, char* line) {
    unsigned long size = 0;
    if (value >= 10000) line[size++] = static_cast<char>('0' + value / 10000);
    if (size || value >= 1000) line[size++] = static_cast<char>('0' + value / 1000 % 10);
    if (size || value >= 100) line[size++] = static_cast<char>('0' + value / 100 % 10);
    if (size || value >= 10) line[size++] = static_cast<char>('0' + value / 10 % 10);
    line[size++] = static_cast<char>('0' + value % 10);
    line[size++] = '\n';
    return size;
}
void setStatus(const char* value) { __atomic_store_n(&status, value, __ATOMIC_RELEASE); }
void cancelPartyInvite();
void confirmedActions(const ChatLiveContext& context);
void* observePlayer(void* client);
bool handlePartyInvite(void* handler, const char* text);

// Minecraft 1.26.52.3 Android x86_64: libc++ strings have a 24-byte layout.
// TextPacketPayload is a variant at +0x88, with index at +0xc0. Disassembly of
// ClientNetworkHandler::handle(TextPacket&) confirms the message offsets below.
bool packetMessage(const unsigned char* packet, char* result) {
    unsigned int index = *reinterpret_cast<const unsigned int*>(packet + chat::textPayloadVariant);
    if (index > 2) return false;
    const unsigned char* string = packet + (index == 1 ? chat::textPayloadLong : chat::textPayloadShort);
    unsigned long size = string[0] >> 1;
    const unsigned char* data = string + 1;
    if (string[0] & 1) {
        size = reinterpret_cast<const unsigned long*>(string)[1];
        data = reinterpret_cast<const unsigned char* const*>(string)[2];
    }
    if (!data || size > 4096 || (!(string[0] & 1) && size > 22)) return false;
    unsigned long used = 0;
    for (unsigned long i = 0; i < size; ++i) {
        // Minecraft formatting: UTF-8 section sign followed by a format code.
        if (data[i] == 0xc2 && i + 2 < size && data[i + 1] == 0xa7) { i += 2; continue; }
        if (used == 4096 || !data[i]) return false;
        result[used++] = static_cast<char>(data[i]);
    }
    result[used] = 0;
    return true;
}
// Verified from ChatScreenController's submit path -> screen model at
// 0xb56b076..0xb56b168. Only use live objects from the incoming dispatcher; never retain
// a ClientInstance/LocalPlayer across frames or world changes.
template<class F> F method(void* object, unsigned long offset) {
    return reinterpret_cast<F>((*static_cast<void***>(object))[offset / 8]);
}
void* clientFromHandler(void* handler) {
    if (!handler) return nullptr;
    unsigned long table = *static_cast<unsigned long*>(handler);
    if (table != gameBase + handlerVtable && table != gameBase + legacyHandlerVtable) return nullptr;
    void* client = *reinterpret_cast<void**>(static_cast<unsigned char*>(handler) + chat::handlerClient);
    return client && *static_cast<unsigned long*>(client) == gameBase + clientVtable ? client : nullptr;
}
bool sendChatClient(void* client, const char* message) {
    if (!client || *static_cast<unsigned long*>(client) != gameBase + clientVtable) {
        setStatus("AutoGG: client instance bridge unavailable"); return false;
    }
    using Get = void* (*)(void*);
    void* player = method<Get>(client, chat::clientPlayerSlot)(client);
    void* sender = method<Get>(client, chat::clientSenderSlot)(client);
    if (!player) { setStatus("AutoGG: local player unavailable"); return false; }
    if (!sender) { setStatus("AutoGG: packet sender unavailable"); return false; }

    // libc++ string/optional ABI, distinct from the launcher's libstdc++ ABI.
    // The constructor copies these borrowed views; Minecraft owns the packet's
    // strings and its own destructors release them after synchronous send.
    alignas(8) unsigned char xuid[24]{}, empty[24]{}, filtered[32]{}, packet[chat::textPacketSize]{};
    struct StringView { unsigned long capacity, size; const char* data; };
    StringView text{257, length(message), message};
    method<void (*)(void*, void*)>(client, chat::clientXuidSlot)(xuid, client); // string return uses sret.
    const void* platform = empty;
    auto identity = static_cast<void**>(method<Get>(client, chat::clientIdentitySlot)(client));
    if (identity && identity[0]) {
        void* account = identity[0];
        if (!method<bool (*)(void*)>(account, chat::accountLoggedInSlot)(account))
            platform = method<const void* (*)(void*, int)>(account, chat::accountPlatformSlot)(account, 1);
    }
    createChat(packet, static_cast<unsigned char*>(player) + chat::playerName, &text,
               filtered, platform ? platform : empty, xuid);
    destroyString(xuid);
    method<void (*)(void*, const void*)>(sender, chat::senderSendSlot)(sender, packet);
    // The native submit path (0xb56b16b..0xb56b227) displays local chat
    // when IMinecraftGame::isConnectedToServer() is false. The packet sender
    // alone does not supply that local echo in an integrated-server world.
    void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(client) + chat::clientGame);
    if (game && !method<bool (*)(void*)>(game, chat::gameConnectedSlot)(game)) {
        void* level = method<Get>(player, chat::playerLevelSlot)(player);
        if (level) method<void (*)(void*, const void*, const void*)>(level, chat::levelChatSlot)
            (level, static_cast<unsigned char*>(player) + chat::playerName, &text);
    }
    method<void (*)(void*)>(packet, 0)(packet); // non-deleting TextPacket destructor.
    return true;
}
bool sendChat(void* handler, const char* message) {
    return sendChatClient(clientFromHandler(handler), message);
}
bool sendCommandClient(void* client, const char* command) {
    if (!client || !command) return false;
    // CommandRequestPacket carries the slash, just like chat's command input.
    // Normalize here so every module uses the same wire format.
    if (*command == '/') ++command;
    unsigned long size = 0;
    while (command[size] && size < 255) ++size;
    if (!size || size > 254) return false;
    char text[256] = "/";
    for (unsigned long i = 0; i < size; ++i) {
        if (command[i] == '\n' || command[i] == '\r') return false;
        text[i + 1] = command[i];
    }
    if (*static_cast<unsigned long*>(client) != gameBase + clientVtable) return false;
    using Get = void* (*)(void*);
    void* player = method<Get>(client, chat::clientPlayerSlot)(client);
    if (!player || !hooks::readable(reinterpret_cast<unsigned long>(player), chat::playerLevel, 8)) return false;
    void* level = *reinterpret_cast<void**>(static_cast<unsigned char*>(player) + chat::playerLevel);
    if (!level || !hooks::readable(reinterpret_cast<unsigned long>(client), chat::clientMinecraft, 8)) return false;
    void* minecraft = *reinterpret_cast<void**>(static_cast<unsigned char*>(client) + chat::clientMinecraft);
    if (!minecraft || !hooks::readable(reinterpret_cast<unsigned long>(minecraft), chat::minecraftCommands, 8)) return false;
    void* commands = *reinterpret_cast<void**>(static_cast<unsigned char*>(minecraft) + chat::minecraftCommands);
    if (!commands || !hooks::readable(reinterpret_cast<unsigned long>(commands), 0, 0x58)) return false;
    const unsigned long* uniqueId = playerUniqueId(player);
    if (!uniqueId || !hooks::readable(reinterpret_cast<unsigned long>(uniqueId), 0, 8)) return false;
    // Match the 40-byte PlayerCommandOrigin built at 0xb56a662..0xb56a69b.
    // The executor borrows this context synchronously, then builds a command
    // packet for a remote server or performs native local execution/feedback.
    struct PlayerOrigin { unsigned long table; CommandUuid uuid; unsigned long actor; void* level; };
    static_assert(sizeof(PlayerOrigin) == 40, "PlayerCommandOrigin ABI");
    PlayerOrigin origin{gameBase + chat::playerCommandOriginVtable, newCommandUuid(0), *uniqueId, level};
    struct CommandContext { alignas(8) unsigned char text[24]; void* origin; unsigned int version; };
    static_assert(sizeof(CommandContext) == 40, "CommandContext ABI");
    CommandContext context{};
    context.origin = &origin;
    context.version = chat::commandVersion;
    constructNativeString(context.text, text, size + 1);
    executeCommand(commands, &context, false);
    destroyString(context.text);
    return true;
}
bool blacklistMessage(const ChatMessage& message) {
    Lock lock;
    return ready && blacklisted(message.text);
}
void autoGGMessage(const ChatMessage& message) {
    char reply[256]{};
    {
        Lock lock;
        if (!ready || !enabled || !*response || contains(response, trigger)
            || !contains(message.text, trigger)) return;
        double time = now();
        if (!__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) {
            setStatus(__atomic_load_n(&blockedReason, __ATOMIC_ACQUIRE));
        } else if (time - lastMatch < 15) {
            setStatus("AutoGG: duplicate match ignored (15 seconds)");
        } else {
            lastMatch = time;
            copy(reply, response, sizeof(reply));
        }
    }
    if (*reply && __atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) {
        bool sent = sendChat(message.handler, reply);
        if (sent) setStatus("AutoGG: response sent");
        if (!sent) { Lock lock; lastMatch = -30; }
    }
}
bool partyMessage(const ChatMessage& message) {
    return ready && handlePartyInvite(message.handler, message.text);
}
void dispatch(void* dispatcher, const void* network, void* handler, void* sharedPacket) {
    auto_gg_lobby_dispatch(handler);
    auto packet = sharedPacket ? *static_cast<unsigned char**>(sharedPacket) : nullptr;
    char text[4097];
    bool decoded = ready && packet && packetMessage(packet, text);
    ChatMessage message{text, handler};
    bool hidden = decoded && chat_notify(message);
    if (!hidden) originalDispatch(dispatcher, network, handler, sharedPacket);
    if (decoded) chat_notify_after(message);
}

template<int Index> void lobbyDispatch(void* dispatcher, const void* network, void* handler, void* packet) {
    auto_gg_lobby_dispatch(handler);
    lobbyOriginals[Index](dispatcher, network, handler, packet);
}
void lobbyAnswer(PopupAnswer answer, void* context) {
    Lock lock;
    if (lobbyConfirmation.state != LobbyConfirmation::Waiting
        || lobbyConfirmation.ticket != reinterpret_cast<unsigned long>(context)) return;
    if (answer == PopupAnswer::Yes && savedLobbyWatch.enabled) {
        lobbyConfirmation.state = LobbyConfirmation::Approved;
        setStatus("Lobby Scanner: confirmed, queued for gameplay update");
    } else {
        lobbyConfirmation.state = LobbyConfirmation::Idle;
        setStatus("Lobby Scanner: action declined");
    }
}
void partyAnswer(PopupAnswer answer, void* context) {
    Lock lock;
    if (partyInvite.state != PartyInvite::Waiting
        || partyInvite.ticket != reinterpret_cast<unsigned long>(context)) return;
    if (answer == PopupAnswer::Yes && savedCCUtils.enabled && savedCCUtils.partyInvites) {
        partyInvite.state = PartyInvite::Approved;
    } else {
        partyInvite.state = PartyInvite::Idle;
    }
}
void cancelPartyInvite() {
    unsigned long ticket;
    {
        Lock lock;
        ticket = partyInvite.ticket;
        partyInvite.state = PartyInvite::Idle;
    }
    popup_cancel_if(partyAnswer, reinterpret_cast<void*>(ticket));
}
bool partyPlayer(const char* text, char* player) {
    static const char phrase[] = "party invite from";
    for (const char* at = text; *at; ++at) {
        unsigned int i = 0;
        while (phrase[i] && at[i] && chatLower(at[i]) == phrase[i]) ++i;
        if (phrase[i]) continue;
        const char* start = at + i;
        if (*start != ' ' && *start != '\t') continue;
        while (*start == ' ' || *start == '\t') ++start;
        const char* period = start;
        while (*period && *period != '.') ++period;
        if (*period != '.') continue;
        const char* end = period;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
        unsigned long size = end - start;
        if (!size || size > 64) continue;
        bool valid = true;
        for (const char* c = start; c < end; ++c)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == ' ' || *c == '\t')) valid = false;
        if (!valid) continue;
        for (unsigned long j = 0; j < size; ++j) player[j] = start[j];
        player[size] = 0;
        return true;
    }
    return false;
}

bool handlePartyInvite(void* handler, const char* text) {
    {
        Lock lock;
        if (!savedCCUtils.enabled || !savedCCUtils.partyInvites) return false;
    }
    char player[65];
    if (!handler || !partyPlayer(text, player)) return false;
    unsigned long ticket;
    {
        Lock lock;
        if (!savedCCUtils.enabled || !savedCCUtils.partyInvites) return false;
        if (partyInvite.state != PartyInvite::Idle)
            return partyInvite.handlerIdentity == reinterpret_cast<unsigned long>(handler)
                && sameText(partyInvite.player, player);
        ticket = ++partyTicket;
        partyInvite.state = PartyInvite::Waiting;
        partyInvite.ticket = ticket;
        partyInvite.handlerIdentity = reinterpret_cast<unsigned long>(handler);
        partyInvite.clientIdentity = reinterpret_cast<unsigned long>(clientFromHandler(handler));
        copy(partyInvite.player, player, sizeof(partyInvite.player));
    }
    char message[96] = "Accept party invite from ";
    copy(message + length(message), player, sizeof(message) - length(message));
    if (popup_show("Party invite", message, partyAnswer, reinterpret_cast<void*>(ticket))) return true;
    {
        Lock lock;
        if (partyInvite.ticket == ticket) partyInvite.state = PartyInvite::Idle;
    }
    return false;
}
void lobbySendStatus(bool sent, bool publicText) {
    setStatus(sent ? (publicText ? "Lobby Scanner: public message sent" : "Lobby Scanner: command requested")
                   : (publicText ? "Lobby Scanner: public message send failed" : "Lobby Scanner: command request failed"));
}

void confirmedActions(const ChatLiveContext& context) {
    PartyInvite party;
    LobbyConfirmation lobby;
    {
        Lock lock;
        if (!ready || !__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) return;
        const unsigned long identity = reinterpret_cast<unsigned long>(context.client);
        if (partyInvite.state == PartyInvite::Approved && partyInvite.clientIdentity == identity
            && savedCCUtils.enabled && savedCCUtils.partyInvites) {
            party = partyInvite;
            partyInvite.state = PartyInvite::Idle;
        }
        if (lobbyConfirmation.state == LobbyConfirmation::Approved && lobbyConfirmation.clientIdentity == identity
            && savedLobbyWatch.enabled) {
            lobby = lobbyConfirmation;
            lobbyConfirmation.state = LobbyConfirmation::Idle;
        }
    }
    // Consume before native calls, which may reenter client getters.
    if (party.state == PartyInvite::Approved) {
        char command[80] = "p accept ";
        const unsigned long prefix = length(command);
        copy(command + prefix, party.player, sizeof(command) - prefix);
        for (char* at = command + prefix; *at; ++at)
            if (*at == ' ' || *at == '\t') *at = '_';
        setStatus(chat_send_command(context, command) ? "CC Utils: party acceptance requested"
                                                     : "CC Utils: party invite command failed");
    }
    if (lobby.state == LobbyConfirmation::Approved)
        lobbySendStatus(lobby.publicText ? chat_send_text(context, lobby.text) : chat_send_command(context, lobby.text), lobby.publicText);
}

unsigned long findGame() {
    unsigned long base = hooks::find_game();
    return hooks::readable(base, chatConstructor, 1, true)
        && hooks::readable(base, stringDestructor, 1, true)
        && hooks::readable(base, chat::commandPacketConstructor,
                           sizeof(chat::commandPacketConstructorSignature), true)
        && hooks::readable(base, chat::nativeStringConstructor,
                           sizeof(chat::nativeStringConstructorSignature), true)
        && hooks::readable(base, clientVtable, chat::clientVtableSize)
        && hooks::readable(base, handlerVtable, chat::handlerVtableSize)
        && hooks::readable(base, legacyHandlerVtable, chat::handlerVtableSize) ? base : 0;
}
bool installHook(unsigned long base) {
    // GNU build ID, not a version-name guess. Unsupported binaries stay untouched.
    if (!hooks::supported(base)
        || !hooks::matches(base, chat::commandPacketConstructor,
                           chat::commandPacketConstructorSignature,
                           sizeof(chat::commandPacketConstructorSignature))
        || !hooks::matches(base, chat::nativeStringConstructor,
                           chat::nativeStringConstructorSignature,
                           sizeof(chat::nativeStringConstructorSignature))
        || !hooks::matches_pointer(base, chat::commandPacketVtable + 0x10,
                                  chat::commandPacketGetId)
        || !hooks::matches(base, dispatchFunction, chat::dispatchSignature, sizeof(chat::dispatchSignature))
        || !hooks::matches_pointer(base, dispatcherSlot, dispatchFunction)
        || !hooks::matches_pointer(base, handlerVtable + chat::textHandlerSlot, handleTextFunction)
        || !hooks::matches_pointer(base, legacyHandlerVtable + chat::textHandlerSlot, handleTextFunction)) return false;
    if (!hooks::readable(base, chat::commandUuidGenerator, sizeof(chat::commandUuidGeneratorSignature), true)
        || !hooks::matches(base, chat::commandUuidGenerator, chat::commandUuidGeneratorSignature,
                           sizeof(chat::commandUuidGeneratorSignature))) return false;
    const unsigned long commandFunctions[] = {chat::clientMinecraftGetter, chat::nativeCommandExecute,
                                             chat::playerUniqueIdGetter, chat::commandOriginData};
    const unsigned char* commandSignatures[] = {chat::clientMinecraftGetterSignature, chat::nativeCommandExecuteSignature,
                                               chat::playerUniqueIdGetterSignature, chat::commandOriginDataSignature};
    const unsigned long commandSizes[] = {sizeof(chat::clientMinecraftGetterSignature), sizeof(chat::nativeCommandExecuteSignature),
                                          sizeof(chat::playerUniqueIdGetterSignature), sizeof(chat::commandOriginDataSignature)};
    for (int i = 0; i < 4; ++i)
        if (!hooks::readable(base, commandFunctions[i], commandSizes[i], true)
            || !hooks::matches(base, commandFunctions[i], commandSignatures[i], commandSizes[i])) return false;
    if (!hooks::matches_pointer(base, chat::playerCommandOriginVtable + 0xd0, chat::commandOriginType)
        || !hooks::matches_pointer(base, chat::playerCommandOriginVtable + 0xd8, chat::commandOriginData)) return false;
    for (int i = 0; i < 3; ++i) {
        unsigned char signature[] = {0x48,0x89,0xd7,0x48,0x8b,0x11,0x48,0x8b,0x07,
                                     0x48,0x8b,0x80,0,0,0,0,0xff,0xe0};
        for (int j = 0; j < 4; ++j) signature[12 + j] = (chat::lobbyHandlerSlots[i] >> (j * 8)) & 255;
        if (!hooks::readable(base, chat::lobbyBaseHandlers[i], 1, true)
            || !hooks::readable(base, chat::lobbyLegacyHandlers[i], 1, true)
            || !hooks::readable(base, chat::lobbyDispatchFunctions[i], sizeof(signature), true)
            || !hooks::matches(base, chat::lobbyDispatchFunctions[i], signature, sizeof(signature))
            || !hooks::matches_pointer(base, chat::lobbyDispatchSlots[i], chat::lobbyDispatchFunctions[i])
            || !hooks::matches_pointer(base, handlerVtable + chat::lobbyHandlerSlots[i], chat::lobbyBaseHandlers[i])
            || !hooks::matches_pointer(base, legacyHandlerVtable + chat::lobbyHandlerSlots[i], chat::lobbyLegacyHandlers[i])) return false;
    }
    if (!hooks::readable(base, chat::clientPlayerGetter, sizeof(chat::clientPlayerGetterSignature), true)
        || !hooks::matches(base, chat::clientPlayerGetter, chat::clientPlayerGetterSignature,
                           sizeof(chat::clientPlayerGetterSignature))) return false;
    // Fail before patching if another mod replaced an ABI-dependent getter.
    const unsigned long getters[][2] = {
        {chat::clientPlayerSlot, chat::clientPlayerGetter},
        {chat::clientMinecraftSlot, chat::clientMinecraftGetter},
        {chat::clientXuidSlot, chat::clientXuidGetter},
        {chat::clientIdentitySlot, chat::clientIdentityGetter},
        {chat::clientSenderSlot, chat::clientSenderGetter},
        {chat::clientGameGetterSlot, chat::clientGameGetter}
    };
    for (const auto& getter : getters)
        if (!hooks::matches_pointer(base, clientVtable + getter[0], getter[1])) return false;
    originalDispatch = reinterpret_cast<Dispatch>(base + dispatchFunction);
    gameBase = base;
    createChat = reinterpret_cast<CreateChat>(base + chatConstructor);
    constructNativeString = reinterpret_cast<ConstructNativeString>(base + chat::nativeStringConstructor);
    destroyString = reinterpret_cast<void (*)(void*)>(base + stringDestructor);
    executeCommand = reinterpret_cast<int (*)(void*, const void*, bool)>(base + chat::nativeCommandExecute);
    playerUniqueId = reinterpret_cast<const unsigned long* (*)(void*)>(base + chat::playerUniqueIdGetter);
    newCommandUuid = reinterpret_cast<CommandUuid (*)(int)>(base + chat::commandUuidGenerator);
    originalPlayerGetter = reinterpret_cast<void* (*)(void*)>(base + chat::clientPlayerGetter);
    hooks::Patch patches[5] = {{dispatcherSlot, base + dispatchFunction, reinterpret_cast<unsigned long>(&dispatch)}};
    Dispatch callbacks[] = {lobbyDispatch<0>, lobbyDispatch<1>, lobbyDispatch<2>};
    for (int i = 0; i < 3; ++i) {
        lobbyOriginals[i] = reinterpret_cast<Dispatch>(base + chat::lobbyDispatchFunctions[i]);
        patches[i + 1] = {chat::lobbyDispatchSlots[i], base + chat::lobbyDispatchFunctions[i],
                          reinterpret_cast<unsigned long>(callbacks[i])};
    }
    patches[4] = {clientVtable + chat::clientPlayerSlot, base + chat::clientPlayerGetter,
                  reinterpret_cast<unsigned long>(&observePlayer)};
    return hooks::install("AutoGG", base, patches, 5) == hooks::InstallResult::Installed;
}

void* observePlayer(void* client) {
    void* player = originalPlayerGetter(client);
    // The camera calls this getter every gameplay update; preserve its result
    // and borrow its live client instead of retaining a dispatcher handler.
    if (player && ready && __atomic_load_n(&canSend, __ATOMIC_ACQUIRE)
        && !__atomic_test_and_set(&liveDispatchBusy, __ATOMIC_ACQUIRE)) {
        chat_notify_live({client});
        __atomic_clear(&liveDispatchBusy, __ATOMIC_RELEASE);
    }
    return player;
}

void findConfigPath() {
    FILE* maps = openFile("/proc/self/maps", "r");
    if (!maps) return;
    char line[8192];
    unsigned long self = reinterpret_cast<unsigned long>(&auto_gg_init);
    while (getLine(line, sizeof(line), maps)) {
        unsigned long start, end;
        if (scan(line, "%lx-%lx", &start, &end) != 2 || self < start || self >= end) continue;
        char* path = line;
        while (*path && *path != '/') ++path;
        if (!*path || length(path) >= sizeof(configPath) - 20) break;
        copy(configPath, path, sizeof(configPath));
        unsigned long n = length(configPath);
        while (n && configPath[n - 1] != '/') --n;
        copy(configPath + n, "odiclient.conf", sizeof(configPath) - n);
        copy(legacyConfigPath, configPath, sizeof(legacyConfigPath));
        copy(legacyConfigPath + n, "autogg.conf", sizeof(legacyConfigPath) - n);
        break;
    }
    closeFile(maps);
}
void loadConfig() {
    if (!*configPath) return;
    FILE* file = openFile(configPath, "r");
    bool migrating = false;
    if (!file && *legacyConfigPath) {
        file = openFile(legacyConfigPath, "r");
        migrating = file != nullptr;
    }
    if (!file) return;
    char lines[46][512]{};
    bool complete = true;
    for (int i = 0; i < 4; ++i) {
        auto& line = lines[i];
        if (!getLine(line, sizeof(line), file)) { complete = false; break; }
        unsigned long n = length(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
    }
    bool version2 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG2");
    bool version3 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG3");
    bool version9 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG9");
    bool version10 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG10");
    bool version16 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG16");
    bool version22 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG22");
    bool version21 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG21");
    version21 = version21 || version22;
    bool version20 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG20");
    bool version19 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG19");
    bool version18 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG18");
    bool version17 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG17");
    version20 = version20 || version21;
    version19 = version19 || version20;
    version18 = version18 || version19;
    version17 = version17 || version18;
    version16 = version16 || version17;
    bool version15 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG15");
    version15 = version15 || version16;
    bool version14 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG14");
    version14 = version14 || version15;
    bool version13 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG13");
    version13 = version13 || version14;
    bool version12 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG12");
    version12 = version12 || version13;
    bool version11 = complete && length(lines[0]) == 8 && contains(lines[0], "AUTOGG11");
    bool version8 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG8");
    bool version7 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG7");
    bool version6 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG6");
    bool version5 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG5");
    bool version4 = complete && length(lines[0]) == 7 && contains(lines[0], "AUTOGG4");
    bool hasRenderSettings = version9 || version10 || version11 || version12;
    version9 = version9 || version10 || version11 || version12;
    version8 = version8 || version9;
    version7 = version7 || version8;
    version6 = version6 || version7;
    version5 = version5 || version6;
    version4 = version4 || version5;
    bool hasModuleSettings = version2 || version3 || version4;
    if (hasModuleSettings) for (int i = 4; i < 7; ++i) {
        auto& line = lines[i];
        if (!getLine(line, sizeof(line), file)) { complete = false; break; }
        unsigned long n = length(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
    }
    if (version3 || version4) for (int i = 7; i < 9; ++i) {
        auto& line = lines[i];
        if (!getLine(line, sizeof(line), file)) { complete = false; break; }
        unsigned long n = length(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
    }
    if (version4) for (int i = 9; i < 12; ++i) {
        auto& line = lines[i];
        if (!getLine(line, sizeof(line), file)) { complete = false; break; }
        unsigned long n = length(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
    }
    if (version5) for (int i = 12; i < 14; ++i) {
        auto& line = lines[i];
        if (!getLine(line, sizeof(line), file)) { complete = false; break; }
        unsigned long n = length(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
    }
    if (version6) {
        if (!getLine(lines[14], sizeof(lines[14]), file)) complete = false;
        unsigned long n = length(lines[14]);
        if (n && lines[14][n - 1] == '\n') lines[14][--n] = 0;
    }
    if (version7) {
        if (!getLine(lines[15], sizeof(lines[15]), file)) complete = false;
        unsigned long n = length(lines[15]);
        if (n && lines[15][n - 1] == '\n') lines[15][--n] = 0;
    }
    if (version8) {
        if (!getLine(lines[16], sizeof(lines[16]), file)) complete = false;
        unsigned long n = length(lines[16]);
        if (n && lines[16][n - 1] == '\n') lines[16][--n] = 0;
        if (length(lines[16]) != 1 || (lines[16][0] != '0' && lines[16][0] != '1')) complete = false;
    }
    if (version9) for (int i = 17; i < 22; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (i < 20 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
            complete = false;
    }
    if (version10) for (int i = 22; i < 24; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
    }
    if (version12) {
        if (!getLine(lines[22], sizeof(lines[22]), file)) complete = false;
        unsigned long n = length(lines[22]);
        if (n && lines[22][n - 1] == '\n') lines[22][--n] = 0;
        if (length(lines[22]) != 1 || (lines[22][0] != '0' && lines[22][0] != '1')) complete = false;
    }
    if (version13) {
        if (!getLine(lines[23], sizeof(lines[23]), file)) complete = false;
        unsigned long n = length(lines[23]);
        if (n && lines[23][n - 1] == '\n') lines[23][--n] = 0;
        if (length(lines[23]) != 1 || (lines[23][0] != '0' && lines[23][0] != '1')) complete = false;
    }
    if (version14) for (int i = 24; i < 27; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (i < 26 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
            complete = false;
        if (i == 26) {
            if (version15) {
                unsigned int out = 0;
                for (unsigned int at = 0; lines[i][at]; ++at) {
                    char c = lines[i][at];
                    if (c == '\\') {
                        c = lines[i][++at];
                        if (c == 'n') c = '\n';
                        else if (c != '\\') { complete = false; break; }
                    }
                    lines[i][out++] = c;
                }
                lines[i][out] = 0;
            }
            if (!validText(lines[i], true)) complete = false;
        }
    }
    if (version16) {
        if (!getLine(lines[27], sizeof(lines[27]), file)) complete = false;
        unsigned long n = length(lines[27]);
        if (n && lines[27][n - 1] == '\n') lines[27][--n] = 0;
        if (length(lines[27]) != 1 || (lines[27][0] != '0' && lines[27][0] != '1')) complete = false;
    }
    if (version17) for (int i = 28; i < 30; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (i == 28 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
            complete = false;
        if (i == 29 && !validText(lines[i])) complete = false;
    }
    if (version18) for (int i = 30; i < 32; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (i == 30 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
            complete = false;
    }
    if (version19) for (int i = 32; i < 35; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (i < 34 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
            complete = false;
    }
    if (version20) for (int i = 35; i < 37; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
    }
    EnvironmentSettings environment;
    if (version21) {
        for (int i = 37; i < 44; ++i) {
            if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
            unsigned long n = length(lines[i]);
            if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
            if (i < 40 && (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')))
                complete = false;
        }
        environment = {lines[37][0] == '1', lines[38][0] == '1', lines[39][0] == '1',
                       parseRange(lines[40], 0, 23999), parseRange(lines[41], 0, 360),
                       parseRange(lines[42], 0, 100), parseRange(lines[43], 0, 100)};
        if (environment.ticks < 0 || environment.hue < 0 || environment.saturation < 0 || environment.value < 0)
            complete = false;
    }
    if (version22) for (int i = 44; i < 46; ++i) {
        if (!getLine(lines[i], sizeof(lines[i]), file)) { complete = false; break; }
        unsigned long n = length(lines[i]);
        if (n && lines[i][n - 1] == '\n') lines[i][--n] = 0;
        if (length(lines[i]) != 1 || (lines[i][0] != '0' && lines[i][0] != '1')) complete = false;
    }
    int fpsInterval = version19 ? parseRange(lines[34], version20 ? 250 : 100, version20 ? 2000 : 5000) : 1000;
    int fpsScale = version20 ? parseRange(lines[35], 0, ui_scale::count - 1) : 2;
    int fpsAnchor = version20 ? parseRange(lines[36], 0, 3) : 0;
    int renderRadius = version18 ? parseRange(lines[31], 16, 256) : 128;
    int renderBelow = version9 ? parseRange(lines[20], 16, 256) : 64;
    int renderAbove = version9 ? parseRange(lines[21], 16, 256) : 128;
    int zoomScroll = version7 ? parseRange(lines[15], 1, 50) : 5;
    int zoomDefault = version6 ? parseRange(lines[14], 15, 300) : 30;
    int zoomKey = version5 ? parseRange(lines[13], 32, 511) : 67;
    bool validZoom = !version5 || (length(lines[12]) == 1
        && (lines[12][0] == '0' || lines[12][0] == '1')
        && zoomKey >= 32 && zoomKey != 76 && zoomKey != 256);
    char extra[2];
    int strength = length(lines[6]) == 1 && lines[6][0] >= '0' && lines[6][0] <= '9'
        ? lines[6][0] - '0'
        : length(lines[6]) == 2 && lines[6][0] >= '0' && lines[6][0] <= '9'
          && lines[6][1] >= '0' && lines[6][1] <= '9'
          ? (lines[6][0] - '0') * 10 + lines[6][1] - '0' : -1;
    bool validModuleSettings = !hasModuleSettings ||
        (length(lines[4]) == 1 && (lines[4][0] == '0' || lines[4][0] == '1')
         && length(lines[5]) == 1 && (lines[5][0] == '0' || lines[5][0] == '1')
         && strength >= 0 && strength <= 80);
    int fpsLimit = (version3 || version4) ? parseFpsLimit(lines[8]) : -1;
    bool validFpsSettings = !(version3 || version4)
        || (length(lines[7]) == 1 && (lines[7][0] == '0' || lines[7][0] == '1')
            && fpsLimit >= 30);
    int averageHz = version4 ? parseRange(lines[10], 30, 500) : 60;
    bool validBlurSettings = !version4
        || ((length(lines[9]) == 1 && (lines[9][0] == '0' || lines[9][0] == '1'))
            && averageHz >= 30
            && length(lines[11]) == 1 && (lines[11][0] == '0' || lines[11][0] == '1'));
    if (complete && !getLine(extra, sizeof(extra), file)
        && ((length(lines[0]) == 7 && contains(lines[0], "AUTOGG1")) || hasModuleSettings)
        && fpsInterval >= 100 && (!version20 || fpsInterval % 250 == 0) && fpsScale >= 0 && fpsAnchor >= 0
        && renderBelow >= 16 && renderAbove >= 16 && renderRadius >= 16
        && zoomScroll >= 1 && zoomDefault >= 15 && validZoom && validModuleSettings && validFpsSettings && validBlurSettings && length(lines[1]) == 1
        && (lines[1][0] == '0' || lines[1][0] == '1')
        && validText(lines[2]) && validText(lines[3]) && lines[3][0] != '/') {
        enabled = lines[1][0] == '1';
        copy(trigger, lines[2], sizeof(trigger)); copy(response, lines[3], sizeof(response));
        if (hasModuleSettings) {
            savedSprint = lines[4][0] == '1'; savedBlur = lines[5][0] == '1';
            savedBlurStrength = strength;
        } else { savedSprint = savedBlur = false; savedBlurStrength = 30; }
        if (version3 || version4) {
            savedFpsLimitEnabled = lines[7][0] == '1'; savedFpsLimit = fpsLimit;
        } else { savedFpsLimitEnabled = false; savedFpsLimit = 120; }
        if (version4) {
            savedBlurFpsAverage = lines[9][0] == '1';
            savedBlurAverageHz = averageHz;
            savedScreenBlur = lines[11][0] == '1';
        } else {
            savedBlurFpsAverage = savedScreenBlur = false;
            savedBlurAverageHz = 60;
        }
        savedZoom = version5 && lines[12][0] == '1';
        savedZoomKey = zoomKey; savedZoomDefault = zoomDefault; savedZoomScroll = zoomScroll;
        savedCenterCursor = version8 && lines[16][0] == '1';
        savedRender = {hasRenderSettings && lines[17][0] == '1', hasRenderSettings && lines[18][0] == '1',
                       hasRenderSettings && lines[19][0] == '1', renderBelow, renderAbove, version18 && lines[30][0] == '1', renderRadius};
        savedTablistMojangles = version16 && lines[27][0] == '1';
        savedLobbyWatch = {};
        if (version17) {
            savedLobbyWatch.enabled = lines[28][0] == '1';
            copy(savedLobbyWatch.rules, lines[29], sizeof(savedLobbyWatch.rules));
        }
        savedTablist = !version12 || lines[22][0] == '1';
        savedParticles = version13 && lines[23][0] == '1';
        savedChatMods = {};
        if (version14) {
            savedChatMods.enabled = lines[24][0] == '1';
            savedChatMods.blacklist = lines[25][0] == '1';
            copy(savedChatMods.keywords, lines[26], sizeof(savedChatMods.keywords));
        }
        if (fpsInterval < 250) fpsInterval = 250;
        if (fpsInterval > 2000) fpsInterval = 2000;
        fpsInterval = (fpsInterval + 125) / 250 * 250;
        savedFpsDisplay = {version19 && lines[32][0] == '1', version19 && lines[33][0] == '1', fpsInterval, fpsScale, fpsAnchor};
        savedEnvironment = environment;
        savedCCUtils = {version22 && lines[44][0] == '1', version22 && lines[45][0] == '1'};
        if (migrating || !version22) dirty = true;
    } else setStatus("AutoGG settings invalid; using defaults");
    closeFile(file);
}
void saveConfig() {
    char savedTrigger[256], savedResponse[256];
    bool centerCursor, tablist, particles, tablistMojangles;
    bool savedEnabled, sprint, blur, fpsLimitEnabled, blurFpsAverage, screenBlur;
    int strength, fpsLimit, averageHz, zoomKey, zoomDefault, zoomScroll;
    bool zoom;
    RenderSettings render;
    EnvironmentSettings environment;
    FpsDisplaySettings fpsDisplay;
    ChatModsSettings chatMods;
    LobbyWatchSettings lobbyWatch;
    CCUtilsSettings ccUtils;
    {
        Lock lock;
        if (!dirty) return;
        dirty = false;
        savedEnabled = enabled;
        tablistMojangles = savedTablistMojangles;
        centerCursor = savedCenterCursor; tablist = savedTablist; particles = savedParticles;
        environment = savedEnvironment;
        render = savedRender; chatMods = savedChatMods; fpsDisplay = savedFpsDisplay;
        lobbyWatch = savedLobbyWatch;
        ccUtils = savedCCUtils;
        zoom = savedZoom; zoomKey = savedZoomKey; zoomDefault = savedZoomDefault; zoomScroll = savedZoomScroll;
        sprint = savedSprint; blur = savedBlur; strength = savedBlurStrength;
        fpsLimitEnabled = savedFpsLimitEnabled; fpsLimit = savedFpsLimit;
        blurFpsAverage = savedBlurFpsAverage; averageHz = savedBlurAverageHz;
        screenBlur = savedScreenBlur;
        copy(savedTrigger, trigger, sizeof(savedTrigger)); copy(savedResponse, response, sizeof(savedResponse));
    }
    if (!*configPath) { setStatus("AutoGG settings path unavailable"); return; }
    char temporary[4112]; copy(temporary, configPath, sizeof(temporary));
    copy(temporary + length(temporary), ".tmp", 5);
    FILE* file = openFile(temporary, "w");
    if (!file) { setStatus("AutoGG settings could not be saved"); return; }
    char flag[] = {savedEnabled ? '1' : '0', '\n'};
    bool ok = writeFile("AUTOGG22\n", 1, 9, file) == 9 && writeFile(flag, 1, 2, file) == 2;
    const char* texts[] = {savedTrigger, savedResponse};
    for (const char* text : texts) {
        unsigned long n = length(text);
        ok = writeFile(text, 1, n, file) == n && writeFile("\n", 1, 1, file) == 1 && ok;
    }
    char moduleFlags[] = {sprint ? '1' : '0', '\n', blur ? '1' : '0', '\n'};
    char strengthLine[4];
    unsigned long strengthLength = strength >= 10 ? 2 : 1;
    strengthLine[0] = static_cast<char>('0' + strength / 10);
    if (strengthLength == 2) strengthLine[1] = static_cast<char>('0' + strength % 10);
    strengthLine[strengthLength] = '\n';
    ok = writeFile(moduleFlags, 1, sizeof(moduleFlags), file) == sizeof(moduleFlags)
        && writeFile(strengthLine, 1, strengthLength + 1, file) == strengthLength + 1 && ok;
    char fpsFlag[] = {fpsLimitEnabled ? '1' : '0', '\n'};
    char fpsLimitLine[4];
    unsigned long fpsLimitLength = 0;
    if (fpsLimit >= 100) fpsLimitLine[fpsLimitLength++] = static_cast<char>('0' + fpsLimit / 100);
    if (fpsLimitLength) fpsLimit %= 100;
    if (fpsLimitLength || fpsLimit >= 10)
        fpsLimitLine[fpsLimitLength++] = static_cast<char>('0' + fpsLimit / 10);
    fpsLimitLine[fpsLimitLength++] = static_cast<char>('0' + fpsLimit % 10);
    fpsLimitLine[fpsLimitLength++] = '\n';
    ok = writeFile(fpsFlag, 1, sizeof(fpsFlag), file) == sizeof(fpsFlag)
        && writeFile(fpsLimitLine, 1, fpsLimitLength, file) == fpsLimitLength && ok;
    char blurFlags[] = {blurFpsAverage ? '1' : '0', '\n', screenBlur ? '1' : '0', '\n'};
    char averageLine[4];
    unsigned long averageLength = formatIntLine(averageHz, averageLine);
    ok = writeFile(blurFlags, 1, 2, file) == 2
        && writeFile(averageLine, 1, averageLength, file) == averageLength
        && writeFile(blurFlags + 2, 1, 2, file) == 2 && ok;
    char zoomFlag[] = {zoom ? '1' : '0', '\n'}, zoomLine[5];
    unsigned long zoomLength = formatIntLine(zoomKey, zoomLine);
    ok = writeFile(zoomFlag, 1, 2, file) == 2
        && writeFile(zoomLine, 1, zoomLength, file) == zoomLength && ok;
    unsigned long defaultLength = formatIntLine(zoomDefault, zoomLine);
    ok = writeFile(zoomLine, 1, defaultLength, file) == defaultLength && ok;
    unsigned long scrollLength = formatIntLine(zoomScroll, zoomLine);
    ok = writeFile(zoomLine, 1, scrollLength, file) == scrollLength && ok;
    char centerFlag[] = {centerCursor ? '1' : '0', '\n'};
    ok = writeFile(centerFlag, 1, 2, file) == 2 && ok;
    char renderFlags[] = {render.enabled ? '1' : '0', '\n', render.below ? '1' : '0', '\n',
                          render.above ? '1' : '0', '\n'};
    ok = writeFile(renderFlags, 1, sizeof(renderFlags), file) == sizeof(renderFlags) && ok;
    const int renderDistances[] = {render.belowDistance, render.aboveDistance};
    for (int value : renderDistances) {
        unsigned long n = formatIntLine(value, zoomLine);
        ok = writeFile(zoomLine, 1, n, file) == n && ok;
    }
    char particlesFlag[] = {particles ? '1' : '0', '\n'};
    char tablistFlag[] = {tablist ? '1' : '0', '\n'};
    ok = writeFile(tablistFlag, 1, 2, file) == 2 && ok;
    ok = writeFile(particlesFlag, 1, 2, file) == 2 && ok;
    char chatFlags[] = {chatMods.enabled ? '1' : '0', '\n', chatMods.blacklist ? '1' : '0', '\n'};
    char encodedKeywords[511];
    unsigned long keywordLength = 0;
    for (const char* c = chatMods.keywords; *c; ++c) {
        if (*c == '\n' || *c == '\\') {
            encodedKeywords[keywordLength++] = '\\';
            encodedKeywords[keywordLength++] = *c == '\n' ? 'n' : '\\';
        } else encodedKeywords[keywordLength++] = *c;
    }
    ok = writeFile(chatFlags, 1, sizeof(chatFlags), file) == sizeof(chatFlags) && ok;
    ok = writeFile(encodedKeywords, 1, keywordLength, file) == keywordLength
        && writeFile("\n", 1, 1, file) == 1 && ok;
    char fontFlag[] = {tablistMojangles ? '1' : '0', '\n'};
    ok = writeFile(fontFlag, 1, 2, file) == 2 && ok;
    char lobbyFlag[] = {lobbyWatch.enabled ? '1' : '0', '\n'};
    unsigned long lobbyRulesLength = length(lobbyWatch.rules);
    ok = writeFile(lobbyFlag, 1, sizeof(lobbyFlag), file) == sizeof(lobbyFlag) && ok;
    ok = writeFile(lobbyWatch.rules, 1, lobbyRulesLength, file) == lobbyRulesLength
        && writeFile("\n", 1, 1, file) == 1 && ok;
    char horizontalFlag[] = {render.horizontal ? '1' : '0', '\n'};
    unsigned long radiusLength = formatIntLine(render.radius, zoomLine);
    ok = writeFile(horizontalFlag, 1, 2, file) == 2
        && writeFile(zoomLine, 1, radiusLength, file) == radiusLength && ok;
    char displayFlags[] = {fpsDisplay.enabled ? '1' : '0', '\n', fpsDisplay.low ? '1' : '0', '\n'};
    unsigned long intervalLength = formatIntLine(fpsDisplay.intervalMs, zoomLine);
    ok = writeFile(displayFlags, 1, sizeof(displayFlags), file) == sizeof(displayFlags)
        && writeFile(zoomLine, 1, intervalLength, file) == intervalLength && ok;
    int displayValues[] = {fpsDisplay.fontScale, fpsDisplay.anchor};
    for (int value : displayValues) {
        unsigned long n = formatIntLine(value, zoomLine);
        ok = writeFile(zoomLine, 1, n, file) == n && ok;
    }
    const int environmentValues[] = {environment.enabled, environment.time, environment.fog,
                                     environment.ticks, environment.hue, environment.saturation, environment.value};
    for (int value : environmentValues) {
        char line[7];
        unsigned long n = formatIntLine(value, line);
        ok = writeFile(line, 1, n, file) == n && ok;
    }
    char ccFlags[] = {ccUtils.enabled ? '1' : '0', '\n', ccUtils.partyInvites ? '1' : '0', '\n'};
    ok = writeFile(ccFlags, 1, sizeof(ccFlags), file) == sizeof(ccFlags) && ok;
    ok = closeFile(file) == 0 && ok;
    if (!ok || renameFile(temporary, configPath) != 0) setStatus("AutoGG settings could not be saved");

}

}

const char* chat_error() { return error; }
bool chat_send_command(void* handler, const char* command) {
    if (!ready || !__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) return false;
    return sendCommandClient(clientFromHandler(handler), command);
}

bool chat_send_command(const ChatLiveContext& context, const char* command) {
    if (!ready || !__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) return false;
    return sendCommandClient(context.client, command);
}
bool chat_send_text(const ChatLiveContext& context, const char* text) {
    if (!ready || !text || !validText(text) || !__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) return false;
    return sendChatClient(context.client, text);
}

void auto_gg_init() {
    if (!chat_listen(blacklistMessage) || !chat_listen(partyMessage)
        || !chat_listen(nullptr, autoGGMessage) || !chat_listen_live(confirmedActions)) {
        error = "Chat: listener limit reached"; return;
    }
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) { error = "AutoGG: host API unavailable"; return; }
#define LOAD(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(libc, name)); if (!variable) { error = "AutoGG: host API unavailable"; return; }
    LOAD(openFile, "fopen"); LOAD(getLine, "fgets"); LOAD(closeFile, "fclose");
    LOAD(writeFile, "fwrite"); LOAD(renameFile, "rename"); LOAD(scan, "sscanf");
    LOAD(getTime, "clock_gettime");
#undef LOAD
    findConfigPath(); loadConfig();
    if (!hooks::initialize()) { error = "AutoGG: hook manager unavailable"; return; }
    unsigned long base = findGame();
    if (!base || !installHook(base)) { error = "AutoGG: unsupported Minecraft build"; return; }
    ready = true; error = nullptr;
    if (!*auto_gg_status()) setStatus("AutoGG: waiting for matching chat");
}
const char* auto_gg_label() {
    Lock lock;
    return error ? error : enabled ? "AutoGG: ON" : "AutoGG: OFF";
}
const char* auto_gg_status() { return __atomic_load_n(&status, __ATOMIC_ACQUIRE); }
void auto_gg_get_text(char* outTrigger, char* outResponse) {
    Lock lock; copy(outTrigger, trigger, 256); copy(outResponse, response, 256);
}
void client_settings_get_zoom(bool* zoom, int* key, int* defaultLevel, int* scrollStep) {
    Lock lock; *zoom = savedZoom; *key = savedZoomKey; *defaultLevel = savedZoomDefault; *scrollStep = savedZoomScroll;
}
void client_settings_set_zoom(bool zoom, int key, int defaultLevel, int scrollStep) {
    if (key < 32 || key >= 512 || key == 76 || key == 256) return;
    if (defaultLevel < 15) defaultLevel = 15;
    if (defaultLevel > 300) defaultLevel = 300;
    if (scrollStep < 1) scrollStep = 1;
    if (scrollStep > 50) scrollStep = 50;
    Lock lock;
    if (savedZoomScroll != scrollStep) dirty = true;
    savedZoomScroll = scrollStep;
    if (savedZoomDefault != defaultLevel) dirty = true;
    savedZoomDefault = defaultLevel;
    if (savedZoom != zoom || savedZoomKey != key) dirty = true;
    savedZoom = zoom; savedZoomKey = key;
}
void client_settings_get_modules(bool* sprint, bool* blur, int* strength) {
    Lock lock;
    *sprint = savedSprint; *blur = savedBlur; *strength = savedBlurStrength;
}
void client_settings_set_modules(bool sprint, bool blur, int strength) {
    if (strength < 0) strength = 0;
    if (strength > 80) strength = 80;
    Lock lock;
    if (savedSprint != sprint || savedBlur != blur || savedBlurStrength != strength) dirty = true;
    savedSprint = sprint; savedBlur = blur; savedBlurStrength = strength;
}
void client_settings_get_fps_limit(bool* fpsEnabled, int* fpsLimit) {
    Lock lock;
    *fpsEnabled = savedFpsLimitEnabled;
    *fpsLimit = savedFpsLimit;
}
void client_settings_set_fps_limit(bool fpsEnabled, int fpsLimit) {
    if (fpsLimit < 30) fpsLimit = 30;
    if (fpsLimit > 480) fpsLimit = 480;
    Lock lock;
    if (savedFpsLimitEnabled != fpsEnabled || savedFpsLimit != fpsLimit) dirty = true;
    savedFpsLimitEnabled = fpsEnabled;
    savedFpsLimit = fpsLimit;
}
void client_settings_get_blur(bool* fpsAverage, int* averageHz, bool* screenBlur) {
    Lock lock;
    *fpsAverage = savedBlurFpsAverage;
    *averageHz = savedBlurAverageHz;
    *screenBlur = savedScreenBlur;
}
void client_settings_set_blur(bool fpsAverage, int averageHz, bool screenBlur) {
    if (averageHz < 30) averageHz = 30;
    if (averageHz > 500) averageHz = 500;
    Lock lock;
    if (savedBlurFpsAverage != fpsAverage || savedBlurAverageHz != averageHz
        || savedScreenBlur != screenBlur) dirty = true;
    savedBlurFpsAverage = fpsAverage;
    savedBlurAverageHz = averageHz;
    savedScreenBlur = screenBlur;
}
bool auto_gg_is_enabled() { Lock lock; return enabled; }
void auto_gg_set_enabled(bool value) {
    Lock lock;
    if (enabled != value) {
        enabled = value;
        dirty = true;
        __atomic_store_n(&canSend, false, __ATOMIC_RELEASE);
    }
}
void auto_gg_toggle() { Lock lock; enabled = !enabled; dirty = true; __atomic_store_n(&canSend, false, __ATOMIC_RELEASE); }
void auto_gg_set_trigger(const char* value) {
    if (!validText(value)) { setStatus("AutoGG: use one line, at most 255 UTF-8 bytes"); return; }
    Lock lock; copy(trigger, value, sizeof(trigger)); dirty = true; __atomic_store_n(&canSend, false, __ATOMIC_RELEASE);
}
void auto_gg_set_response(const char* value) {
    if (!validText(value) || (value && value[0] == '/')) {
        setStatus("AutoGG: enter chat text, not a slash command (max 255 bytes)"); return;
    }
    Lock lock; copy(response, value, sizeof(response)); dirty = true; __atomic_store_n(&canSend, false, __ATOMIC_RELEASE);
}
void auto_gg_on_keyboard(int action) {
    if (action == 0) {
        const char* reason = "AutoGG: match skipped during keyboard input";
        __atomic_store_n(&blockedReason, reason, __ATOMIC_RELEASE);
        __atomic_store_n(&canSend, false, __ATOMIC_RELEASE);
    }
}
void auto_gg_update(bool gameplay, bool focused) {
    if (!getTime) return;
    saveConfig();
    const char* reason = !focused ? "AutoGG: match skipped while game was unfocused"
        : "AutoGG: match skipped while chat or a game menu was open";
    __atomic_store_n(&blockedReason, reason, __ATOMIC_RELEASE);
    __atomic_store_n(&canSend, gameplay && focused, __ATOMIC_RELEASE);
}

void auto_gg_world_reset() {
    cancelPartyInvite();
    auto_gg_lobby_reset();
}

void auto_gg_lobby_reset() {
    unsigned long ticket;
    {
        Lock lock;
        ticket = lobbyConfirmation.ticket;
        lobbyConfirmation.state = LobbyConfirmation::Idle;
    }
    popup_cancel_if(lobbyAnswer, reinterpret_cast<void*>(ticket));
}

void auto_gg_lobby_dispatch(void* handler) {
    if (!ready || !handler) return;
    const unsigned long identity = reinterpret_cast<unsigned long>(handler);
    bool resetParty, resetLobby;
    {
        Lock lock;
        resetParty = partyInvite.state != PartyInvite::Idle && partyInvite.handlerIdentity != identity;
        resetLobby = lobbyConfirmation.state != LobbyConfirmation::Idle && lobbyConfirmation.handlerIdentity != identity;
    }
    if (resetParty) cancelPartyInvite();
    if (resetLobby) auto_gg_lobby_reset();
}

void auto_gg_lobby_player(void* handler, const char* name) {
    if (!ready || !handler || !name || !*name || !__atomic_load_n(&canSend, __ATOMIC_ACQUIRE)) return;
    LobbyWatchSettings settings;
    {
        Lock lock;
        settings = savedLobbyWatch;
    }
    if (!settings.enabled) return;
    const char* entry = settings.rules;
    while (*entry) {
        const char* end = entry;
        while (*end && *end != ',') ++end;
        const char* start = entry;
        while (start < end && (*start == ' ' || *start == '\t')) ++start;
        const char* stop = end;
        while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) --stop;
        const char* separator = start;
        while (separator < stop && *separator != '/' && *separator != '#') ++separator;
        const char* nameEnd = separator;
        while (nameEnd > start && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) --nameEnd;
        bool confirm = nameEnd > start && nameEnd[-1] == '?';
        if (confirm) {
            --nameEnd;
            while (nameEnd > start && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) --nameEnd;
        }
        const char* command = separator < stop ? separator + 1 : stop;
        while (command < stop && (*command == ' ' || *command == '\t')) ++command;
        const char* commandEnd = stop;
        while (commandEnd > command && (commandEnd[-1] == ' ' || commandEnd[-1] == '\t')) --commandEnd;
        bool same = false;
        if (start < nameEnd) for (const char* at = name; *at && !same; ++at) {
            const char* a = start;
            const char* b = at;
            while (a < nameEnd && *b && chatLower(*a) == chatLower(*b)) { ++a; ++b; }
            same = a == nameEnd;
        }
        if (same && separator < stop && command < commandEnd) {
            char text[256];
            unsigned long size = commandEnd - command;
            bool publicText = *separator == '#';
            if (size < sizeof(text)) {
                unsigned long at = 0;
                if (!publicText && *command == '/') ++command;
                if (command == commandEnd) return;
                while (command < commandEnd) text[at++] = *command++;
                text[at] = 0;
                if (confirm) {
                    unsigned long ticket;
                    {
                        Lock lock;
                        if (lobbyConfirmation.state != LobbyConfirmation::Idle) {
                            setStatus("Lobby Scanner: another confirmation is pending"); return;
                        }
                        ticket = ++lobbyTicket;
                        lobbyConfirmation.state = LobbyConfirmation::Waiting;
                        lobbyConfirmation.ticket = ticket;
                        lobbyConfirmation.handlerIdentity = reinterpret_cast<unsigned long>(handler);
                        lobbyConfirmation.clientIdentity = reinterpret_cast<unsigned long>(clientFromHandler(handler));
                        lobbyConfirmation.publicText = publicText;
                        copy(lobbyConfirmation.text, text, sizeof(lobbyConfirmation.text));
                    }
                    char title[96];
                    copy(title, publicText ? "Lobby Scanner chat: " : "Lobby Scanner command: ", sizeof(title));
                    copy(title + length(title), name, sizeof(title) - length(title));
                    char question[256];
                    copy(question, publicText ? "" : "/", sizeof(question));
                    copy(question + length(question), text, sizeof(question) - length(question));
                    if (!popup_show(title, question, lobbyAnswer, reinterpret_cast<void*>(ticket))) {
                        auto_gg_lobby_reset(); setStatus("Lobby Scanner: popup busy, action skipped");
                    } else setStatus("Lobby Scanner: awaiting confirmation");
                } else lobbySendStatus(publicText ? sendChat(handler, text) : chat_send_command(handler, text), publicText);
            }
            return;
        }
        entry = *end ? end + 1 : end;
    }
}

void client_settings_set_center_cursor(bool value) {
    Lock lock;
    if (savedCenterCursor != value) dirty = true;
    savedCenterCursor = value;
}
bool client_settings_get_center_cursor() { Lock lock; return savedCenterCursor; }

RenderSettings client_settings_get_render() { Lock lock; return savedRender; }
void client_settings_set_render(const RenderSettings& settings) {
    Lock lock;
    if (savedRender.enabled != settings.enabled || savedRender.below != settings.below
        || savedRender.above != settings.above || savedRender.belowDistance != settings.belowDistance
        || savedRender.aboveDistance != settings.aboveDistance
        || savedRender.horizontal != settings.horizontal || savedRender.radius != settings.radius) dirty = true;
    savedRender = settings;
}

bool client_settings_get_tablist() { Lock lock; return savedTablist; }
void client_settings_set_tablist(bool value) {
    Lock lock;
    if (savedTablist != value) { savedTablist = value; dirty = true; }
}

bool client_settings_get_particles() { Lock lock; return savedParticles; }
void client_settings_set_particles(bool value) {
    Lock lock;
    if (savedParticles != value) { savedParticles = value; dirty = true; }
}

ChatModsSettings client_settings_get_chat_mods() { Lock lock; return savedChatMods; }
void client_settings_set_chat_mods(const ChatModsSettings& settings) {
    if (!validText(settings.keywords, true)) return;
    Lock lock; savedChatMods = settings; dirty = true;
}

LobbyWatchSettings client_settings_get_lobby_watch() { Lock lock; return savedLobbyWatch; }
void client_settings_set_lobby_watch(const LobbyWatchSettings& settings) {
    if (!validText(settings.rules)) return;
    bool changed;
    {
        Lock lock;
        changed = savedLobbyWatch.enabled != settings.enabled || !sameText(savedLobbyWatch.rules, settings.rules);
        if (changed) dirty = true;
        savedLobbyWatch = settings;
    }
    if (changed) auto_gg_lobby_reset();
}
CCUtilsSettings client_settings_get_cc_utils() { Lock lock; return savedCCUtils; }
void client_settings_set_cc_utils(CCUtilsSettings settings) {
    bool cancel = false;
    {
        Lock lock;
        if (savedCCUtils.enabled != settings.enabled || savedCCUtils.partyInvites != settings.partyInvites)
            dirty = true;
        savedCCUtils = settings;
        cancel = !settings.enabled || !settings.partyInvites;
    }
    if (cancel) cancelPartyInvite();
}

bool client_settings_get_tablist_mojangles() { Lock lock; return savedTablistMojangles; }
void client_settings_set_tablist_mojangles(bool value) {
    Lock lock;
    if (savedTablistMojangles != value) { savedTablistMojangles = value; dirty = true; }
}

FpsDisplaySettings client_settings_get_fps_display() { Lock lock; return savedFpsDisplay; }
void client_settings_set_fps_display(FpsDisplaySettings settings) {
    if (settings.intervalMs < 250) settings.intervalMs = 250;
    if (settings.intervalMs > 2000) settings.intervalMs = 2000;
    settings.intervalMs = (settings.intervalMs + 125) / 250 * 250;
    if (settings.fontScale < 0) settings.fontScale = 0;
    if (settings.fontScale >= ui_scale::count) settings.fontScale = ui_scale::count - 1;
    if (settings.anchor < 0 || settings.anchor > 3) settings.anchor = 0;
    Lock lock;
    if (savedFpsDisplay.enabled != settings.enabled || savedFpsDisplay.low != settings.low
        || savedFpsDisplay.intervalMs != settings.intervalMs || savedFpsDisplay.fontScale != settings.fontScale
        || savedFpsDisplay.anchor != settings.anchor) dirty = true;
    savedFpsDisplay = settings;
}

EnvironmentSettings client_settings_get_environment() { Lock lock; return savedEnvironment; }
void client_settings_set_environment(EnvironmentSettings settings) {
    if (settings.ticks < 0) settings.ticks = 0;
    if (settings.ticks > 23999) settings.ticks = 23999;
    if (settings.hue < 0) settings.hue = 0;
    if (settings.hue > 360) settings.hue = 360;
    if (settings.saturation < 0) settings.saturation = 0;
    if (settings.saturation > 100) settings.saturation = 100;
    if (settings.value < 0) settings.value = 0;
    if (settings.value > 100) settings.value = 100;
    Lock lock;
    if (savedEnvironment.enabled != settings.enabled || savedEnvironment.time != settings.time
        || savedEnvironment.fog != settings.fog || savedEnvironment.ticks != settings.ticks
        || savedEnvironment.hue != settings.hue || savedEnvironment.saturation != settings.saturation
        || savedEnvironment.value != settings.value) dirty = true;
    savedEnvironment = settings;
}
