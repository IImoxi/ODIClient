#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include "flarial_presence.cpp"
#ifndef TABLIST_PRESENCE_FIXTURE
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* lib, const char* name) { return dlsym(lib, name); }
#endif
static bool accept(const char* text) {
    responseSize = std::strlen(text);
    std::memcpy(response, text, responseSize + 1);
    return parse();
}
#ifdef TABLIST_PRESENCE_FIXTURE
void test_presence_load(const char* text, long long now) {
    flarial_presence_init();
    assert(ready);
    lastAttempt = now;
    assert(accept(text));
}
#else
int main() {
    flarial_presence_init();
    assert(ready);
    assert(std::strstr(httpError(403), "403"));
    assert(std::strstr(httpError(429), "429"));
    assert(std::strstr(httpError(503), "503"));
    lastAttempt = 100;
    // Actual POST response is a top-level array of +name commands.
    assert(accept("[\"+RIAN MAAFLAG\",\"+Rosemberg15\",\"+Angel MC 879755\"]"));
    assert(flarial_presence_matches("RIAN MAAFLAG", 100));
    assert(flarial_presence_matches("Rosemberg15", 100));
    assert(flarial_presence_matches("Angel MC 879755", 100));
    struct PlayerRecord { unsigned long uuid[2]; char name[64]; } records[3]{};
    std::strcpy(records[0].name, "Other");
    std::strcpy(records[1].name, "RIAN MAAFLAG");
    std::strcpy(records[2].name, "Angel MC 879755");
    bool found[3]{};
    flarial_presence_match_many(reinterpret_cast<const char*>(records), sizeof(PlayerRecord),
                                __builtin_offsetof(PlayerRecord, name), 3, found, 100);
    assert(!found[0] && found[1] && found[2]);
    assert(accept("[\"+Rosemberg15\",\"-Other\",\"+\\u4e00\",\"+\",\"\"]"));
    assert(flarial_presence_matches("Rosemberg15", 100));
    assert(!accept("[23]"));
    assert(!accept("[\"+Zulu\\u0000Fake\"]"));
    assert(!accept("[\"+Zulu\"]garbage"));
    assert(flarial_presence_matches("Rosemberg15", 100));
    assert(accept("[]"));
    assert(!flarial_presence_matches("Rosemberg15", 100));
    assert(accept("{\"players\":[\"+Zulu\",\"+A Player\"]}"));
    assert(flarial_presence_matches("A Player", 100));
    assert(flarial_presence_matches("zULu", 100));
    assert(!flarial_presence_matches("Someone", 100));
    assert(!flarial_presence_matches("Zulu", 101 + interval * 2));
    assert(accept("{\"players\":[\"+Zulu\",\"-Other\"]}"));
    assert(!accept("{\"players\":[23]}"));
    assert(!accept("{\"players\":[\"+Zulu\\u0000Fake\"]}"));
    assert(!accept("{\"players\":[]}garbage"));
    assert(!accept("{\"error\":\"unavailable\"}"));
    assert(flarial_presence_matches("Zulu", 100));
    assert(accept("{\"players\":[]} \n"));
    assert(!flarial_presence_matches("Zulu", 100));
    responseSize = sizeof(response) - 2;
    char data[] = "123";
    assert(receive(data, 1, 3, nullptr) == 0);
}

#endif
