#include <GLES3/gl3.h>
#include "gpu_shader_services.h"
#include "minecraft_build.h"
#include "hook_manager.h"

namespace {
namespace profile = minecraft_build::current::render::shaderPrograms;
namespace shaderProfile = minecraft_build::current::render::shaderSource;
namespace shaderCompileProfile = minecraft_build::current::render::shaderCompile;
using Program = void (*)(GLuint);
using ShaderSource = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using CompileShader = void (*)(GLuint);
unsigned long originals[3], shaderSourceOriginal, compileShaderOriginal;
bool installed, attempted, shaderSourceInstalled, shaderSourceAttempted;
bool shaderCompileInstalled, shaderCompileAttempted;
GpuProgramInvalidated lifecycleObserver;
GpuProgramBound boundObserver;
GpuShaderSourceTransform shaderSourceTransform;
GpuShaderSourceMatched shaderSourceMatched;
GpuShaderCompileObserver shaderCompileObserver;
void shaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    auto transform = __atomic_load_n(&shaderSourceTransform, __ATOMIC_ACQUIRE);
    if (transform && count > 0 && strings) {
        GLint replacementLength = -1;
        auto replacement = transform(count, strings, lengths, &replacementLength);
        if (replacement && replacementLength >= 0) {
            auto matched = __atomic_load_n(&shaderSourceMatched, __ATOMIC_ACQUIRE);
            if (matched) matched(shader, replacement);
            const GLchar* source[] = {replacement};
            auto original = __atomic_load_n(&shaderSourceOriginal, __ATOMIC_ACQUIRE);
            reinterpret_cast<ShaderSource>(original)(shader, 1, source, &replacementLength);
            return;
        }
    }
    auto original = __atomic_load_n(&shaderSourceOriginal, __ATOMIC_ACQUIRE);
    reinterpret_cast<ShaderSource>(original)(shader, count, strings, lengths);
}
void compileShader(GLuint shader) {
    auto original = __atomic_load_n(&compileShaderOriginal, __ATOMIC_ACQUIRE);
    reinterpret_cast<CompileShader>(original)(shader);
    auto observer = __atomic_load_n(&shaderCompileObserver, __ATOMIC_ACQUIRE);
    if (observer) observer(shader);
}
void useProgram(GLuint program) {
    reinterpret_cast<Program>(originals[2])(program);
    auto observer = __atomic_load_n(&boundObserver, __ATOMIC_ACQUIRE);
    if (observer) observer(program);
}
void invalidateProgram(GLuint program) {
    auto observer = __atomic_load_n(&lifecycleObserver, __ATOMIC_ACQUIRE);
    if (observer) observer(program);
}
void linkProgram(GLuint program) {
    reinterpret_cast<Program>(originals[0])(program);
    invalidateProgram(program);
}
void deleteProgram(GLuint program) {
    reinterpret_cast<Program>(originals[1])(program);
    invalidateProgram(program);
}
const unsigned long replacements[] = {reinterpret_cast<unsigned long>(linkProgram),
    reinterpret_cast<unsigned long>(deleteProgram), reinterpret_cast<unsigned long>(useProgram)};

bool installShaderSource(unsigned long base) {
    if (shaderSourceAttempted || !hooks::supported(base)
        || !hooks::readable(base, shaderProfile::slot, 8)
        || !hooks::readable(base, shaderProfile::plt, 16, true)
        || !hooks::matches(base, shaderProfile::plt, shaderProfile::pltSignature,
                           sizeof(shaderProfile::pltSignature))) return false;
    auto original = *reinterpret_cast<const unsigned long*>(base + shaderProfile::slot);
    if (!hooks::readable(original, 0, 1, true)
        || (original >= base + profile::pltBegin && original < base + profile::pltEnd)
        || original == reinterpret_cast<unsigned long>(&shaderSource)) return false;
    __atomic_store_n(&shaderSourceOriginal, original, __ATOMIC_RELEASE);
    hooks::Patch patch{shaderProfile::slot, original, reinterpret_cast<unsigned long>(&shaderSource)};
    shaderSourceAttempted = true;
    auto result = hooks::install("GL shader source", base, &patch, 1)
        == hooks::InstallResult::Installed;
    __atomic_store_n(&shaderSourceInstalled, result, __ATOMIC_RELEASE);
    return result;
}
bool installShaderCompile(unsigned long base) {
    if (shaderCompileAttempted || !hooks::supported(base)
        || !hooks::readable(base, shaderCompileProfile::slot, 8)
        || !hooks::readable(base, shaderCompileProfile::plt, 16, true)
        || !hooks::matches(base, shaderCompileProfile::plt, shaderCompileProfile::pltSignature,
                           sizeof(shaderCompileProfile::pltSignature))) return false;
    auto original = *reinterpret_cast<const unsigned long*>(base + shaderCompileProfile::slot);
    if (!hooks::readable(original, 0, 1, true)
        || (original >= base + profile::pltBegin && original < base + profile::pltEnd)
        || original == reinterpret_cast<unsigned long>(&compileShader)) return false;
    __atomic_store_n(&compileShaderOriginal, original, __ATOMIC_RELEASE);
    hooks::Patch patch{shaderCompileProfile::slot, original,
                       reinterpret_cast<unsigned long>(&compileShader)};
    shaderCompileAttempted = true;
    shaderCompileInstalled = hooks::install("GL shader compile", base, &patch, 1)
        == hooks::InstallResult::Installed;
    return shaderCompileInstalled;
}
}
bool gpu_shader_services_set_shader_source_transform(GpuShaderSourceTransform transform) {
    __atomic_store_n(&shaderSourceTransform, transform, __ATOMIC_RELEASE);
    return __atomic_load_n(&shaderSourceInstalled, __ATOMIC_ACQUIRE);
}
bool gpu_shader_services_set_shader_diagnostics(GpuShaderSourceMatched matched,
                                              GpuShaderCompileObserver compiled) {
    __atomic_store_n(&shaderSourceMatched, matched, __ATOMIC_RELEASE);
    __atomic_store_n(&shaderCompileObserver, compiled, __ATOMIC_RELEASE);
    return __atomic_load_n(&shaderSourceInstalled, __ATOMIC_ACQUIRE)
        && __atomic_load_n(&shaderCompileInstalled, __ATOMIC_ACQUIRE);
}
bool gpu_shader_services_install(unsigned long base) {
    if (attempted || !hooks::supported(base)) return false;
    hooks::Patch patches[3]{};
    for (unsigned int i = 0; i < 3; ++i) {
        if (!hooks::readable(base, profile::slots[i], 8)
            || !hooks::readable(base, profile::plt[i], 16, true)
            || !hooks::matches(base, profile::plt[i], profile::pltSignatures[i], 6)) return false;
        auto pointer = *reinterpret_cast<const unsigned long*>(base + profile::slots[i]);
        if (!hooks::readable(pointer, 0, 1, true)
            || (pointer >= base + profile::pltBegin && pointer < base + profile::pltEnd)) return false;
        for (auto replacement : replacements) if (pointer == replacement) return false;
        originals[i] = pointer;
        patches[i] = {profile::slots[i], pointer, replacements[i]};
    }
    attempted = true;
    installed = hooks::install("GL shader/program services", base, patches, 3)
        == hooks::InstallResult::Installed;
    return installed;
}
void gpu_shader_services_init() {
    if (!hooks::initialize()) return;
    auto base = hooks::find_game();
    gpu_shader_services_install(base);
    installShaderSource(base);
    installShaderCompile(base);
}

bool gpu_shader_services_set_program_observer(GpuProgramBound bound, GpuProgramInvalidated invalidated) {
    __atomic_store_n(&boundObserver, bound, __ATOMIC_RELEASE);
    __atomic_store_n(&lifecycleObserver, invalidated, __ATOMIC_RELEASE);
    return installed;
}
