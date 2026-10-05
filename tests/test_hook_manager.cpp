#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include "../hook_manager.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* path, int flags) { return dlopen(path, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }

static unsigned int protectCalls, failAt, failAgain;
static int faultProtect(void* address, size_t size, int permissions) noexcept {
    ++protectCalls;
    if (protectCalls == failAt || protectCalls == failAgain) return -1;
    return mprotect(address, size, permissions);
}

int main() {
    using hooks::InstallResult;
    assert(hooks::initialize() && hooks::initialize());
    unsigned long pageSize = hooks::page_size();
    unsigned long size = minecraft_build::current::buildNote + pageSize;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    unsigned long base = reinterpret_cast<unsigned long>(image);
    const unsigned char note[] = {4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    hooks::Patch pair[] = {{pageSize, 11, 111}, {pageSize * 2, 22, 222}};
    auto first = reinterpret_cast<unsigned long*>(image + pair[0].offset);
    auto second = reinterpret_cast<unsigned long*>(image + pair[1].offset);
    *first = 11; *second = 22;
    assert(hooks::install("Test", base, pair, 2) == InstallResult::Rejected);
    assert(*first == 11 && *second == 22);
    std::memcpy(image + minecraft_build::current::buildNote + 16,
                minecraft_build::current::buildId, sizeof(minecraft_build::current::buildId));
    assert(hooks::supported(base));
    // Discover a file-backed game image through the same maps path as production.
    char directory[] = "/tmp/odi-hooks-XXXXXX";
    assert(mkdtemp(directory));
    char path[128];
    std::snprintf(path, sizeof(path), "%s/libminecraftpe.so", directory);
    FILE* file = std::fopen(path, "w+"); assert(file);
    assert(ftruncate(fileno(file), size) == 0);
    auto mapped = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_SHARED, fileno(file), 0));
    assert(mapped != MAP_FAILED && hooks::find_game() == 0);
    std::memcpy(mapped + minecraft_build::current::buildNote,
                image + minecraft_build::current::buildNote, 36);
    assert(hooks::find_game() == reinterpret_cast<unsigned long>(mapped));
    assert(munmap(mapped, size) == 0);
    assert(std::fclose(file) == 0 && unlink(path) == 0 && rmdir(directory) == 0);
    assert(!hooks::supported(1) && !hooks::readable(~0UL - 7, 16, 8));
    assert(!hooks::matches(base, size + pageSize, note, sizeof(note)));
    assert(hooks::install("Empty", base, pair, 0) == InstallResult::Rejected);
    assert(hooks::install(nullptr, base, pair, 2) == InstallResult::Rejected);
    assert(hooks::install("Null", base, nullptr, 2) == InstallResult::Rejected);
    assert(hooks::install("Too many", base, pair, 65) == InstallResult::Rejected);
    hooks::Patch overlapping[] = {pair[0], pair[0]};
    assert(hooks::install("Overlap", base, overlapping, 2) == InstallResult::Rejected);
    overlapping[1].offset += 1;
    std::memcpy(&overlapping[1].expected, image + overlapping[1].offset, 8);
    assert(hooks::install("Partial overlap", base, overlapping, 2) == InstallResult::Rejected);
    hooks::Patch split{pageSize * 3 - 4, 0, 1};
    assert(hooks::install("Split", base, &split, 1) == InstallResult::Rejected);
    hooks::Patch wrong = pair[1]; wrong.expected = 99;
    hooks::Patch mismatched[] = {pair[0], wrong};
    assert(hooks::install("Mismatch", base, mismatched, 2) == InstallResult::Rejected);
    assert(*first == 11 && *second == 22); // No partial writes during preflight.

    assert(mprotect(image, size, PROT_READ) == 0);
    assert(mprotect(image + pageSize * 2, pageSize, PROT_READ | PROT_EXEC) == 0);
    auto originalProtect = hooks::protect;
    hooks::protect = faultProtect;
    protectCalls = 0; failAt = 1;
    assert(hooks::install("No write", base, pair, 2) == InstallResult::RolledBack);
    assert(*first == 11 && *second == 22 && hooks::used == 0);
    // Fail enabling the second page; the first patch and its permissions restore.
    protectCalls = 0; failAt = 3;
    assert(hooks::install("Rollback", base, pair, 2) == InstallResult::RolledBack);
    assert(*first == 11 && *second == 22 && hooks::used == 0);
    int permissions;
    assert(hooks::region(base + pageSize, 8, &permissions) && permissions == PROT_READ);
    assert(hooks::region(base + pageSize * 2, 8, &permissions)
           && permissions == (PROT_READ | PROT_EXEC));
    // Fail restoring the last page: both patches must still roll back.
    protectCalls = 0; failAt = 4;
    assert(hooks::install("Rollback", base, pair, 2) == InstallResult::RolledBack);
    assert(*first == 11 && *second == 22 && hooks::used == 0);
    assert(hooks::region(base + pageSize * 2, 8, &permissions)
           && permissions == (PROT_READ | PROT_EXEC));

    protectCalls = 0; failAt = 0;
    assert(hooks::install("Shared", base, pair, 2) == InstallResult::Installed);
    assert(*first == 111 && *second == 222);
    assert(hooks::region(base + pageSize, 8, &permissions) && permissions == PROT_READ);
    assert(hooks::region(base + pageSize * 2, 8, &permissions)
           && permissions == (PROT_READ | PROT_EXEC));
    hooks::Patch takeover{pageSize, 111, 999};
    assert(hooks::install("Competitor", base, &takeover, 1) == InstallResult::Rejected);
    hooks::Patch another{pageSize * 4, 0, 1};
    assert(hooks::install("Shared", base, &another, 1) == InstallResult::Rejected);
    assert(*first == 111); // Existing owners cannot be chained or overwritten.

    hooks::Patch retained[] = {{pageSize * 4, 0, 1}, {pageSize * 5, 0, 2}};
    protectCalls = 0; failAt = 3; failAgain = 4;
    assert(hooks::install("Retained", base, retained, 2) == InstallResult::Retained);
    assert(*reinterpret_cast<unsigned long*>(image + pageSize * 4) == 1);
    hooks::Patch occupied{pageSize * 4, 1, 3};
    assert(hooks::install("Competitor", base, &occupied, 1) == InstallResult::Rejected);
    assert(hooks::used == 4); // Incomplete rollback keeps the group reserved.
    hooks::protect = originalProtect;

    // Relay code and data have separate RX/RW pages and stay in relative-call range.
    auto relay = hooks::allocate_near(base + pageSize, 2);
    assert(relay);
    relay[0] = 0xc3;
    assert(hooks::make_executable(relay, pageSize));
    assert(hooks::region(reinterpret_cast<unsigned long>(relay), 1, &permissions)
           && permissions == (PROT_READ | PROT_EXEC));
    assert(hooks::region(reinterpret_cast<unsigned long>(relay + pageSize), 1, &permissions)
           && permissions == (PROT_READ | PROT_WRITE));
    assert(reinterpret_cast<unsigned long>(relay) - (base + pageSize) < 0x40000000);
    hooks::release(relay, pageSize * 2);
    assert(munmap(image, size) == 0);
    std::puts("PASS: shared hook build/mapping gates, ownership, overlap rejection, RX/RW preservation, batch rollback, retained hooks, and near relays");
}
