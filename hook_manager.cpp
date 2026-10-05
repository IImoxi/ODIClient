#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "launcher_api.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace hooks {
namespace {
static_assert(sizeof(unsigned long) == 8, "Hooks require x86_64 words");
decltype(&fopen) openFile;
decltype(&fgets) getLine;
decltype(&fclose) closeFile;
decltype(&sscanf) scan;
decltype(&mprotect) protect;
decltype(&mmap) map;
decltype(&munmap) unmap;
unsigned long pageSize;
struct Record {
    const char* owner;
    unsigned long address, original, replacement;
    int permissions;
};
Record records[64];
unsigned int used;

bool equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool gameMapping(const char* line) {
    const char* path = line;
    while (*path && *path != '/') ++path;
    const char* name = path;
    for (const char* p = path; *p; ++p) if (*p == '/') name = p + 1;
    const char expected[] = "libminecraftpe.so";
    unsigned int i = 0;
    while (expected[i] && name[i] == expected[i]) ++i;
    return !expected[i] && (name[i] == '\n' || !name[i]);
}
bool region(unsigned long address, unsigned long size, int* permissions) {
    if (!pageSize || !address || !size || address + size < address) return false;
    FILE* file = openFile("/proc/self/maps", "r");
    if (!file) return false;
    char line[8192];
    bool found = false;
    while (getLine(line, sizeof(line), file)) {
        unsigned long start, end;
        char perms[5]{};
        if (scan(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;
        if (start <= address && address + size <= end) {
            *permissions = (perms[0] == 'r' ? PROT_READ : 0)
                         | (perms[1] == 'w' ? PROT_WRITE : 0)
                         | (perms[2] == 'x' ? PROT_EXEC : 0);
            found = true;
            break;
        }
    }
    closeFile(file);
    return found;
}
unsigned long word(unsigned long address) {
    // The version profile can contain unaligned instruction addresses.
    unsigned long result = 0;
    auto bytes = reinterpret_cast<unsigned char*>(&result);
    for (unsigned int i = 0; i < 8; ++i)
        bytes[i] = reinterpret_cast<const unsigned char*>(address)[i];
    return result;
}
void* page(unsigned long address) {
    return reinterpret_cast<void*>(address & ~(pageSize - 1));
}
}

bool initialize() {
    if (pageSize) return true;
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) return false;
#define LOAD(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(libc, name)); if (!variable) return false
    LOAD(openFile, "fopen"); LOAD(getLine, "fgets"); LOAD(closeFile, "fclose"); LOAD(scan, "sscanf");
    LOAD(protect, "mprotect"); LOAD(map, "mmap"); LOAD(unmap, "munmap");
    auto systemConfig = reinterpret_cast<decltype(&sysconf)>(mcpelauncher_host_dlsym(libc, "sysconf"));
#undef LOAD
    if (!systemConfig) return false;
    long size = systemConfig(_SC_PAGESIZE);
    if (size < 64 || (size & (size - 1))) return false;
    pageSize = static_cast<unsigned long>(size);
    return true;
}
unsigned long page_size() { return pageSize; }
bool readable(unsigned long base, unsigned long offset, unsigned long size, bool executable) {
    if (!base || base + offset < base) return false;
    int permissions;
    return region(base + offset, size, &permissions) && (permissions & PROT_READ)
        && (!executable || (permissions & PROT_EXEC));
}
bool supported(unsigned long base) {
    constexpr auto offset = minecraft_build::current::buildNote;
    return readable(base, offset, 36)
        && minecraft_build_matches(reinterpret_cast<const unsigned char*>(base + offset));
}
bool matches(unsigned long base, unsigned long offset, const unsigned char* bytes, unsigned long size) {
    if (!bytes || !readable(base, offset, size)) return false;
    auto address = reinterpret_cast<const unsigned char*>(base + offset);
    for (unsigned long i = 0; i < size; ++i) if (address[i] != bytes[i]) return false;
    return true;
}
bool matches_pointer(unsigned long base, unsigned long offset, unsigned long targetOffset) {
    return base + targetOffset >= base && readable(base, offset, 8)
        && word(base + offset) == base + targetOffset;
}
unsigned long find_game() {
    if (!initialize()) return 0;
    FILE* file = openFile("/proc/self/maps", "r");
    if (!file) return 0;
    char line[8192];
    unsigned long base = 0;
    while (getLine(line, sizeof(line), file)) {
        unsigned long start, end, offset;
        char perms[5]{};
        if (scan(line, "%lx-%lx %4s %lx", &start, &end, perms, &offset) != 4) continue;
        if (!offset && perms[0] == 'r' && gameMapping(line)) { base = start; break; }
    }
    closeFile(file);
    return supported(base) ? base : 0;
}

InstallResult install(const char* owner, unsigned long base, const Patch* patches, unsigned int count) {
    if (!owner || !*owner || !patches || !count || count > 64 - used || !supported(base))
        return InstallResult::Rejected;
    // Preflight the entire batch before making a page writable.
    for (unsigned int i = 0; i < count; ++i) {
        const auto& patch = patches[i];
        unsigned long address = base + patch.offset;
        int permissions;
        if (address < base || (address & 63) > 56 || patch.expected == patch.replacement
            || !region(address, 8, &permissions) || !(permissions & PROT_READ)
            || word(address) != patch.expected) return InstallResult::Rejected;
        for (unsigned int j = 0; j < used + i; ++j) {
            const auto& other = records[j];
            if ((j < used && equal(other.owner, owner))
                || (address < other.address + 8 && other.address < address + 8))
                return InstallResult::Rejected;
        }
        records[used + i] = {owner, address, patch.expected, patch.replacement, permissions};
    }
    unsigned int changed = 0;
    bool failed = false;
    for (; changed < count; ++changed) {
        auto& record = records[used + changed];
        if (!(record.permissions & PROT_WRITE)
            && protect(page(record.address), pageSize, record.permissions | PROT_WRITE) != 0) {
            failed = true; break;
        }
        // Another native writer must not be silently overwritten after preflight.
        unsigned long expected = record.original;
        bool replaced = __atomic_compare_exchange_n(reinterpret_cast<unsigned long*>(record.address),
            &expected, record.replacement, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        bool protectedAgain = (record.permissions & PROT_WRITE)
            || protect(page(record.address), pageSize, record.permissions) == 0;
        if (!replaced || !protectedAgain) { failed = true; ++changed; break; }
    }
    // A failure restoring the last page must also roll back that last patch.
    if (!failed && changed == count) { used += count; return InstallResult::Installed; }
    bool restored = true;
    for (unsigned int i = changed; i > 0; --i) {
        const auto& record = records[used + i - 1];
        if (protect(page(record.address), pageSize, record.permissions | PROT_WRITE) != 0) {
            restored = false; continue;
        }
        unsigned long expected = record.replacement;
        bool reverted = __atomic_compare_exchange_n(reinterpret_cast<unsigned long*>(record.address),
            &expected, record.original, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        if (!reverted && expected != record.original) restored = false;
        if (protect(page(record.address), pageSize, record.permissions) != 0) restored = false;
    }
    if (restored) return InstallResult::RolledBack;
    // Reserve the whole group if any bytes/permissions remain uncertain.
    // The caller must retain its relay and keep feature behavior inactive.
    used += count;
    return InstallResult::Retained;
}

unsigned char* allocate_near(unsigned long site, unsigned int pages) {
    if (!pageSize || !pages || pages > (~0UL / pageSize)) return nullptr;
    unsigned long centre = site & ~(pageSize - 1);
    for (unsigned long distance = 0x10000; distance < 0x40000000; distance += 0x10000) {
        if (centre + distance < centre) break;
        void* candidate = map(reinterpret_cast<void*>(centre + distance), pageSize * pages,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (candidate != MAP_FAILED) return static_cast<unsigned char*>(candidate);
    }
    return nullptr;
}
bool make_executable(void* address, unsigned long size) {
    return pageSize && address && size && protect(address, size, PROT_READ | PROT_EXEC) == 0;
}
void release(void* address, unsigned long size) {
    if (pageSize && address && size) unmap(address, size);
}
}
