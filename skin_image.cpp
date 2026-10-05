#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <zlib.h>
#include "launcher_api.h"
#include "skin_image.h"

namespace {
#define HOST(name) decltype(&name) host_##name
void* (*host_malloc)(size_t);
void (*host_free)(void*); HOST(fopen); HOST(fwrite); HOST(fclose); HOST(remove);
HOST(mkdir); HOST(clock_gettime); HOST(pthread_create); HOST(pthread_detach);
HOST(fgets); HOST(sscanf);
HOST(compress2); HOST(compressBound); HOST(crc32);
bool ready;
int status;
char directory[4096];
struct Save { unsigned int width, height; char path[4352]; unsigned char pixels[skin_image_max_bytes]; } job;
void findDirectory() {
    // Android-loaded mods are absent from the host loader's dladdr registry.
    // Resolve our mapped ELF path as the font/config adapters already do.
    FILE* maps = host_fopen("/proc/self/maps", "r");
    if (!maps) return;
    char line[8192];
    unsigned long self = reinterpret_cast<unsigned long>(&skin_image_init);
    while (host_fgets(line,sizeof(line),maps)) {
        unsigned long begin, end;
        if (host_sscanf(line,"%lx-%lx",&begin,&end) != 2 || self < begin || self >= end) continue;
        const char* path = line;
        while (*path && *path != '/') ++path;
        if (!*path) break;
        unsigned long length = 0;
        while (path[length] && path[length] != '\n' && path[length] != '\r') ++length;
        while (length && path[length-1] != '/') --length;
        if (!length || length >= sizeof(directory)-6) break;
        const char* package = "1.0.0/x86_64/";
        unsigned long suffix = 0; while (package[suffix]) ++suffix;
        bool packaged = length >= suffix;
        for (unsigned long i = 0; packaged && i < suffix; ++i)
            if (path[length-suffix+i] != package[i]) packaged = false;
        if (packaged) length -= suffix;
        for (unsigned long i = 0; i < length; ++i) directory[i] = path[i];
        const char* folder = "skins";
        for (int i = 0; i < 6; ++i) directory[length+i] = folder[i];
        break;
    }
    host_fclose(maps);
}
void bigEndian(unsigned char* out, unsigned int n) {
    for (int i = 0; i < 4; ++i) out[i] = static_cast<unsigned char>(n >> (24 - i * 8));
}
bool chunk(FILE* file, const char* kind, const unsigned char* data, unsigned int bytes) {
    unsigned char header[8], crc[4]; bigEndian(header, bytes);
    for (int i = 0; i < 4; ++i) header[4+i] = kind[i];
    unsigned long check = host_crc32(0, header + 4, 4);
    if (bytes) check = host_crc32(check, data, bytes);
    bigEndian(crc, check);
    return host_fwrite(header, 1, 8, file) == 8
        && (!bytes || host_fwrite(data, 1, bytes, file) == bytes)
        && host_fwrite(crc, 1, 4, file) == 4;
}
void* save(void*) {
    unsigned long stride = job.width * 4 + 1, size = stride * job.height;
    auto* raw = static_cast<unsigned char*>(host_malloc(size));
    unsigned long packedSize = host_compressBound(size);
    auto* packed = static_cast<unsigned char*>(host_malloc(packedSize));
    bool ok = raw && packed;
    if (ok) {
        for (unsigned int y = 0; y < job.height; ++y) {
            raw[y * stride] = 0;
            for (unsigned int x = 0; x < job.width * 4; ++x)
                raw[y * stride + 1 + x] = job.pixels[y * job.width * 4 + x];
        }
        ok = host_compress2(packed, &packedSize, raw, size, 6) == Z_OK;
    }
    if (ok) {
        host_mkdir(directory, 0755);
        // Exclusive create preserves earlier saves, even if the clock repeats.
        FILE* file = host_fopen(job.path, "wbx");
        ok = file != nullptr;
        if (file) {
            unsigned char header[13]{}; bigEndian(header, job.width); bigEndian(header + 4, job.height);
            header[8] = 8; header[9] = 6;
            const unsigned char magic[] = {137,80,78,71,13,10,26,10};
            ok = host_fwrite(magic, 1, 8, file) == 8 && chunk(file, "IHDR", header, 13)
                && chunk(file, "IDAT", packed, packedSize) && chunk(file, "IEND", nullptr, 0);
            ok = host_fclose(file) == 0 && ok;
            if (!ok) host_remove(job.path);
        }
    }
    host_free(raw); host_free(packed);
    __atomic_store_n(&status, ok ? 2 : -1, __ATOMIC_RELEASE);
    return nullptr;
}
}

bool skin_image_init() {
    if (ready) return true;
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) return false;
#define LOAD(lib, name) host_##name = reinterpret_cast<decltype(host_##name)>(mcpelauncher_host_dlsym(lib, #name)); if (!host_##name) return false
    // Capture needs only allocation; optional export dependencies must not hide skins.
    LOAD(libc, malloc); LOAD(libc, free);
    void* zlib = mcpelauncher_host_dlopen("libz.so.1", 2);
    if (!zlib) return false;
    LOAD(libc, fopen); LOAD(libc, fwrite); LOAD(libc, fgets); LOAD(libc, sscanf);
    LOAD(libc, fclose); LOAD(libc, remove); LOAD(libc, mkdir); LOAD(libc, clock_gettime);
    LOAD(libc, pthread_create); LOAD(libc, pthread_detach);
    LOAD(zlib, compress2); LOAD(zlib, compressBound); LOAD(zlib, crc32);
#undef LOAD
    findDirectory();
    ready = true; return true;
}
unsigned char* skin_image_allocate(unsigned long bytes) {
    return host_malloc && host_free && bytes <= skin_image_max_bytes ? static_cast<unsigned char*>(host_malloc(bytes)) : nullptr;
}
void skin_image_release(unsigned char* pixels) { if (pixels && host_free) host_free(pixels); }
int skin_image_save_status() { return __atomic_load_n(&status, __ATOMIC_ACQUIRE); }
bool skin_image_save(const unsigned char* pixels, unsigned int width, unsigned int height, const char* name) {
    if (!ready || !*directory || !pixels || (width != 64 && width != 128 && width != 256)
        || (height != width && height != width / 2)) return false;
    int old = skin_image_save_status();
    do { if (old == 1) return false; }
    while (!__atomic_compare_exchange_n(&status, &old, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    job.width = width; job.height = height;
    for (unsigned long i = 0; i < width * height * 4ul; ++i) job.pixels[i] = pixels[i];
    unsigned long n = 0;
    while (directory[n]) { job.path[n] = directory[n]; ++n; } job.path[n++] = '/';
    int chars = 0;
    for (; *name && chars < 60; ++name, ++chars) {
        char c = *name;
        job.path[n++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' ? c : '_';
    }
    if (!chars) { job.path[n++] = 's'; job.path[n++] = 'k'; job.path[n++] = 'i'; job.path[n++] = 'n'; }
    job.path[n++] = '-';
    timespec now{};
    if (host_clock_gettime(CLOCK_REALTIME, &now) != 0) {
        __atomic_store_n(&status, -1, __ATOMIC_RELEASE); return false;
    }
    unsigned long stamp = static_cast<unsigned long>(now.tv_sec) * 1000000000ul + now.tv_nsec;
    char digits[24]; int count = 0;
    do { digits[count++] = '0' + stamp % 10; stamp /= 10; } while (stamp);
    while (count) job.path[n++] = digits[--count];
    const char* ext = ".png"; for (int i = 0; i < 5; ++i) job.path[n++] = ext[i];
    pthread_t thread;
    if (host_pthread_create(&thread, nullptr, save, nullptr) != 0) {
        __atomic_store_n(&status, -1, __ATOMIC_RELEASE); return false;
    }
    host_pthread_detach(thread); return true;
}
