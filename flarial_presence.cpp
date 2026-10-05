#include "flarial_presence.h"
#include "launcher_api.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>

namespace {
constexpr int capacity = 16384, nameSize = 64;
constexpr long long interval = 180000000000LL;
char buffers[2][capacity][nameSize];
char (*names)[nameSize] = buffers[0], (*pending)[nameSize] = buffers[1];
char response[1024 * 1024 + 1];
size_t responseSize;
int count;
unsigned char cacheLock;
bool ready, busy;
const char* status = "Flarial: waiting for lookup";
const char* httpError(long code) {
    switch (code) {
        case 301: return "Flarial: HTTP 301 (redirect)";
        case 302: return "Flarial: HTTP 302 (redirect)";
        case 307: return "Flarial: HTTP 307 (redirect)";
        case 308: return "Flarial: HTTP 308 (redirect)";
        case 400: return "Flarial: HTTP 400 (bad request)";
        case 401: return "Flarial: HTTP 401 (unauthorized)";
        case 403: return "Flarial: HTTP 403 (forbidden)";
        case 404: return "Flarial: HTTP 404 (not found)";
        case 405: return "Flarial: HTTP 405 (method rejected)";
        case 429: return "Flarial: HTTP 429 (rate limited)";
        case 500: return "Flarial: HTTP 500 (server error)";
        case 502: return "Flarial: HTTP 502 (gateway error)";
        case 503: return "Flarial: HTTP 503 (unavailable)";
        case 504: return "Flarial: HTTP 504 (gateway timeout)";
        default: return "Flarial: unexpected HTTP response";
    }
}
void setStatus(const char* value) { __atomic_store_n(&status, value, __ATOMIC_RELEASE); }
long long lastAttempt, fetchedAt;
struct Lock {
    Lock() { while (__atomic_test_and_set(&cacheLock, __ATOMIC_ACQUIRE)) {} }
    ~Lock() { __atomic_clear(&cacheLock, __ATOMIC_RELEASE); }
};
#define FUNCTION(name) decltype(&name) host_##name
FUNCTION(curl_global_init); FUNCTION(curl_easy_init); FUNCTION(curl_easy_setopt);
FUNCTION(curl_easy_perform); FUNCTION(curl_easy_getinfo); FUNCTION(curl_easy_cleanup);
FUNCTION(curl_slist_append); FUNCTION(curl_slist_free_all);
FUNCTION(json_tokener_new); FUNCTION(json_tokener_parse_ex); FUNCTION(json_tokener_get_error);
FUNCTION(json_tokener_get_parse_end); FUNCTION(json_tokener_free); FUNCTION(json_object_put);
FUNCTION(json_object_object_get_ex); FUNCTION(json_object_is_type);
FUNCTION(json_object_array_length); FUNCTION(json_object_array_get_idx);
FUNCTION(json_object_get_string); FUNCTION(json_object_get_string_len);
FUNCTION(pthread_create); FUNCTION(pthread_detach);
void (*host_qsort)(void*, size_t, size_t, int (*)(const void*, const void*));
#undef FUNCTION
int compare(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
bool normalize(const char* in, char* out) {
    int n = 0;
    for (; *in; ++in) {
        unsigned char c = *in;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (n == nameSize - 1) return false;
            out[n++] = c;
        }
    }
    out[n] = 0;
    return n != 0;
}
size_t receive(char* data, size_t size, size_t n, void*) {
    if (size && n > (sizeof(response) - 1 - responseSize) / size) return 0;
    size_t bytes = size * n;
    for (size_t i = 0; i < bytes; ++i) response[responseSize++] = data[i];
    response[responseSize] = 0;
    return bytes;
}
bool parse() {
    auto* parser = host_json_tokener_new();
    if (!parser) return false;
    auto* root = host_json_tokener_parse_ex(parser, response, static_cast<int>(responseSize));
    bool valid = host_json_tokener_get_error(parser) == json_tokener_success;
    size_t end = host_json_tokener_get_parse_end(parser);
    while (end < responseSize && (response[end] == ' ' || response[end] == '\n' || response[end] == '\r' || response[end] == '\t')) ++end;
    valid = valid && end == responseSize;
    host_json_tokener_free(parser);
    json_object* list = nullptr;
    if (valid && root && host_json_object_is_type(root, json_type_array)) list = root;
    else if (valid && root && host_json_object_is_type(root, json_type_object))
        host_json_object_object_get_ex(root, "players", &list);
    valid = valid && list && host_json_object_is_type(list, json_type_array);
    size_t length = valid ? host_json_object_array_length(list) : 0;
    valid = valid && length <= capacity;
    int used = 0;
    for (size_t i = 0; valid && i < length; ++i) {
        auto* entry = host_json_object_array_get_idx(list, i);
        if (!host_json_object_is_type(entry, json_type_string)) { valid = false; break; }
        const char* text = host_json_object_get_string(entry);
        int bytes = host_json_object_get_string_len(entry);
        // An empty cache only needs additions. Empty/removal commands and
        // names outside our ASCII atlas must not discard other valid players.
        if (!bytes) continue;
        if (text[0] != '+' && text[0] != '-') { valid = false; break; }
        for (int j = 0; j < bytes; ++j) if (!text[j]) valid = false;
        if (!valid) break;
        if (text[0] == '-' || !normalize(text + 1, pending[used])) continue;
        ++used;
    }
    if (root) host_json_object_put(root);
    if (!valid) return false;
    host_qsort(pending, used, nameSize, [](const void* a, const void* b) {
        return compare(static_cast<const char*>(a), static_cast<const char*>(b));
    });
    {
        Lock lock;
        auto* previous = names; names = pending; pending = previous;
        count = used; fetchedAt = lastAttempt;
    }
    return true;
}
void* fetch(void*) {
    responseSize = 0;
    CURL* curl = host_curl_easy_init();
    if (curl) {
        auto* headers = host_curl_slist_append(nullptr, "Content-Type: application/json");
        bool ok = headers;
#define OPTION(key, value) ok = (host_curl_easy_setopt(curl, key, value) == CURLE_OK) && ok
        OPTION(CURLOPT_URL, "https://api.flarial.xyz/allOnlineUsers");
        OPTION(CURLOPT_POSTFIELDS, "{\"players\":[]}");
        OPTION(CURLOPT_HTTPHEADER, headers); OPTION(CURLOPT_USERAGENT, "Samsung Smart Fridge");
        OPTION(CURLOPT_ACCEPT_ENCODING, "gzip");
        OPTION(CURLOPT_FOLLOWLOCATION, 1L); OPTION(CURLOPT_MAXREDIRS, 3L);
        OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "https");
        OPTION(CURLOPT_POSTREDIR, static_cast<long>(CURL_REDIR_POST_ALL));
        OPTION(CURLOPT_WRITEFUNCTION, receive); OPTION(CURLOPT_NOSIGNAL, 1L);
        OPTION(CURLOPT_TIMEOUT, 10L); OPTION(CURLOPT_CONNECTTIMEOUT, 5L);
        OPTION(CURLOPT_SSL_VERIFYPEER, 1L); OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
#undef OPTION
        long status = 0;
        CURLcode result = ok ? host_curl_easy_perform(curl) : CURLE_FAILED_INIT;
        if (result == CURLE_OK) {
            if (host_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
                setStatus("Flarial: HTTP status unavailable");
            else if (status != 200) setStatus(httpError(status));
            else if (!parse()) setStatus("Flarial: invalid API response");
            else setStatus(count ? "Flarial: online list loaded" : "Flarial: no online users reported");
        } else if (result == CURLE_COULDNT_RESOLVE_HOST || result == CURLE_COULDNT_RESOLVE_PROXY)
            setStatus("Flarial: DNS lookup failed");
        else if (result == CURLE_OPERATION_TIMEDOUT) setStatus("Flarial: request timed out");
        else if (result == CURLE_PEER_FAILED_VERIFICATION) setStatus("Flarial: certificate check failed");
        else setStatus("Flarial: connection failed");
        host_curl_slist_free_all(headers); host_curl_easy_cleanup(curl);
    } else setStatus("Flarial: lookup initialization failed");
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    return nullptr;
}
}
void flarial_presence_init() {
    void* curl = mcpelauncher_host_dlopen("libcurl.so.4", 2);
    void* json = mcpelauncher_host_dlopen("libjson-c.so.5", 2);
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!curl) { setStatus("Flarial: libcurl missing"); return; }
    if (!json) { setStatus("Flarial: json-c missing"); return; }
    if (!libc) { setStatus("Flarial: host runtime unavailable"); return; }
#define LOAD(lib, name) host_##name = reinterpret_cast<decltype(host_##name)>(mcpelauncher_host_dlsym(lib, #name)); if (!host_##name) { setStatus("Flarial: host library incompatible"); return; }
    LOAD(curl, curl_global_init); LOAD(curl, curl_easy_init); LOAD(curl, curl_easy_setopt);
    LOAD(curl, curl_easy_perform); LOAD(curl, curl_easy_getinfo); LOAD(curl, curl_easy_cleanup);
    LOAD(curl, curl_slist_append); LOAD(curl, curl_slist_free_all);
    LOAD(json, json_tokener_new); LOAD(json, json_tokener_parse_ex); LOAD(json, json_tokener_get_error);
    LOAD(json, json_tokener_get_parse_end); LOAD(json, json_tokener_free); LOAD(json, json_object_put);
    LOAD(json, json_object_object_get_ex); LOAD(json, json_object_is_type);
    LOAD(json, json_object_array_length); LOAD(json, json_object_array_get_idx);
    LOAD(json, json_object_get_string); LOAD(json, json_object_get_string_len);
    LOAD(libc, pthread_create); LOAD(libc, pthread_detach); LOAD(libc, qsort);
#undef LOAD
    ready = host_curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!ready) setStatus("Flarial: lookup initialization failed");
}
void flarial_presence_refresh(long long now) {
    if (!ready || __atomic_load_n(&busy, __ATOMIC_ACQUIRE)
        || (lastAttempt && now - lastAttempt < interval)) return;
    setStatus("Flarial: fetching online users");
    lastAttempt = now;
    __atomic_store_n(&busy, true, __ATOMIC_RELEASE);
    pthread_t thread;
    if (host_pthread_create(&thread, nullptr, fetch, nullptr) == 0) host_pthread_detach(thread);
    else { setStatus("Flarial: worker could not start"); __atomic_store_n(&busy, false, __ATOMIC_RELEASE); }
}
bool flarial_presence_matches(const char* name, long long now) {
    char key[nameSize];
    if (!normalize(name, key)) return false;
    Lock lock;
    if (!fetchedAt || now < fetchedAt || now - fetchedAt > interval * 2) return false;
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2, order = compare(names[mid], key);
        if (!order) return true;
        if (order < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

const char* flarial_presence_status() { return __atomic_load_n(&status, __ATOMIC_ACQUIRE); }

void flarial_presence_match_many(const char* records, unsigned long stride, unsigned long nameOffset,
                                 int size, bool* matches, long long now) {
    Lock lock;
    bool current = fetchedAt && now >= fetchedAt && now - fetchedAt <= interval * 2;
    for (int i = 0; i < size; ++i) {
        matches[i] = false;
        if (!current) continue;
        char key[nameSize];
        if (!normalize(records + static_cast<unsigned long>(i) * stride + nameOffset, key)) continue;
        int lo = 0, hi = count;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2, order = compare(names[mid], key);
            if (!order) { matches[i] = true; break; }
            if (order < 0) lo = mid + 1; else hi = mid;
        }
    }
}
