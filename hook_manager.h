#pragma once

// Android x86_64 hooks. Install serially at mod_init, before gameplay;
// installed hooks and their owner names live until process exit.
namespace hooks {
struct Patch {
    unsigned long offset;
    unsigned long expected;
    unsigned long replacement;
};
enum class InstallResult { Installed, Rejected, RolledBack, Retained };

bool initialize();
unsigned long page_size();
unsigned long find_game();
bool supported(unsigned long base);
bool readable(unsigned long base, unsigned long offset, unsigned long size,
              bool executable = false);
bool matches(unsigned long base, unsigned long offset, const unsigned char* bytes,
             unsigned long size);
bool matches_pointer(unsigned long base, unsigned long offset, unsigned long targetOffset);

// Verifies the build, mapping permissions, expected words and patch ownership
// before any writes. Rejects duplicate owners, overlaps, and cache-line/page
// crossing eight-byte patches. Capacity: 64 patches across all modules.
// Rejected means no writes; RolledBack means all bytes/permissions restored.
// Retained means rollback was incomplete: keep any referenced relay alive.
InstallResult install(const char* owner, unsigned long base, const Patch* patches,
                      unsigned int count);

// Writable near memory for module-specific relay code/data. Seal code RX;
// keep code and writable data on separate pages. Release only when no hook
// references the allocation (Rejected or RolledBack installation).
unsigned char* allocate_near(unsigned long site, unsigned int pages);
bool make_executable(void* address, unsigned long size);
void release(void* address, unsigned long size);
}
