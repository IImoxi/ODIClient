#include <cassert>
#include <initializer_list>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../gpu_shader_services.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
static int links, deletes, binds, invalidations, boundCalls, sourceCalls, compileCalls;
static GLuint currentProgram;
static const char* receivedSource;
static GLint receivedLength;
static void nativeLink(GLuint) { ++links; }
static void nativeDelete(GLuint) { ++deletes; }
static void nativeUse(GLuint program) { ++binds; currentProgram = program; }
static void observeBound(GLuint program) { assert(currentProgram == program); ++boundCalls; }
static void observeInvalidated(GLuint) { ++invalidations; }
static void nativeSource(GLuint, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    assert(count == 1); ++sourceCalls; receivedSource = strings[0]; receivedLength = lengths ? lengths[0] : -1;
}
static void nativeCompile(GLuint) { ++compileCalls; }
static void observeCompile(GLuint) { assert(compileCalls == 1); }
static const char* transform(GLsizei, const char* const*, const GLint*, GLint* length) {
    *length = 11; return "replacement";
}
static void observeSource(GLuint, const char* replacement) { assert(std::strcmp(replacement, "replacement") == 0); }

int main() {
    assert(hooks::initialize());
    auto size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(image != MAP_FAILED);
    auto base = reinterpret_cast<unsigned long>(image);
    const unsigned int note[] = {4, 20, 3, 0x00554e47};
    std::memcpy(image + minecraft_build::current::buildNote, note, sizeof(note));
    std::memcpy(image + minecraft_build::current::buildNote + 16, minecraft_build::current::buildId, 20);
    const unsigned long native[] = {reinterpret_cast<unsigned long>(nativeLink),
        reinterpret_cast<unsigned long>(nativeDelete), reinterpret_cast<unsigned long>(nativeUse)};
    for (unsigned int i = 0; i < 3; ++i) {
        std::memcpy(image + profile::plt[i], profile::pltSignatures[i], 6);
        *reinterpret_cast<unsigned long*>(image + profile::slots[i]) = native[i];
    }
    // Unsupported signatures fail without patching anything.
    image[profile::plt[2]] ^= 1;
    assert(mprotect(image, size, PROT_READ | PROT_EXEC) == 0);
    assert(!gpu_shader_services_install(base));
    assert(*reinterpret_cast<unsigned long*>(image + profile::slots[0]) == native[0]);
    assert(mprotect(image, size, PROT_READ | PROT_WRITE) == 0);
    image[profile::plt[2]] ^= 1;
    std::memcpy(image + shaderProfile::plt, shaderProfile::pltSignature, sizeof(shaderProfile::pltSignature));
    std::memcpy(image + shaderCompileProfile::plt, shaderCompileProfile::pltSignature, sizeof(shaderCompileProfile::pltSignature));
    *reinterpret_cast<unsigned long*>(image + shaderProfile::slot) = reinterpret_cast<unsigned long>(nativeSource);
    *reinterpret_cast<unsigned long*>(image + shaderCompileProfile::slot) = reinterpret_cast<unsigned long>(nativeCompile);
    assert(mprotect(image, size, PROT_READ | PROT_EXEC) == 0);
    assert(gpu_shader_services_install(base) && !gpu_shader_services_install(base));
    for (unsigned int i = 0; i < 3; ++i)
        assert((*reinterpret_cast<unsigned long*>(image + profile::slots[i]) != native[i]));
    assert(gpu_shader_services_set_program_observer(observeBound, observeInvalidated));
    for (unsigned int slot : {0u, 1u, 2u})
        reinterpret_cast<Program>(*reinterpret_cast<unsigned long*>(image + profile::slots[slot]))(27);
    assert(links == 1 && deletes == 1 && binds == 1 && invalidations == 2 && boundCalls == 1);
    assert(installShaderSource(base) && installShaderCompile(base));
    const char* source[] = {"native"};
    shaderSource(3, 1, source, nullptr);
    assert(sourceCalls == 1 && receivedSource == source[0] && receivedLength == -1);
    assert(gpu_shader_services_set_shader_source_transform(transform));
    assert(gpu_shader_services_set_shader_diagnostics(observeSource, observeCompile));
    shaderSource(3, 1, source, nullptr);
    assert(sourceCalls == 2 && receivedLength == 11 && std::strcmp(receivedSource, "replacement") == 0);
    compileShader(3);
    assert(compileCalls == 1);
    assert(gpu_shader_services_set_shader_source_transform(nullptr));
    shaderSource(3, 1, source, nullptr);
    assert(sourceCalls == 3 && receivedSource == source[0]);
    assert(munmap(image, size) == 0);
    puts("Shader/program service forwarding, observers and exact-build gates passed");
}
