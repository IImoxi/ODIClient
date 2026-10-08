#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "projection_jitter.h"
#include "launcher_api.h"
#include "hook_manager.h"
#include "minecraft_build.h"
#include "client_modules.h"

namespace {
namespace profile = minecraft_build::current::projectionJitter;
using Upload = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
Upload original;
decltype(&glGetIntegerv) getInteger;
decltype(&glGetUniformLocation) getLocation;
unsigned long long dimensions;
unsigned int frame;
unsigned int sampleState;
// Offsets in sixteenths of one pixel. Every complete cycle is centered.
constexpr int offsets[3][8][2] = {
    {{-4,-4}, {4,4}},
    {{-4,-4}, {4,4}, {4,-4}, {-4,4}},
    {{1,-3}, {-1,3}, {5,1}, {-3,-5}, {-5,5}, {-7,-1}, {3,7}, {7,-7}}
};
bool installed;
const char* error = "Jitter Anti-Aliasing: projection hook unavailable";

bool perspective(const float* m) {
    for (int i = 0; i < 16; ++i) if (!__builtin_isfinite(m[i])) return false;
    // Affine/orthographic HUD, model and view matrices have a constant clip w.
    return m[3] != 0 || m[7] != 0 || m[11] != 0;
}
void upload(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (!__atomic_load_n(&installed, __ATOMIC_ACQUIRE) || location < 0 || count != 1
        || transpose || !value || !client_jitter_active() || !perspective(value)) {
        original(location, count, transpose, value);
        return;
    }
    auto size = __atomic_load_n(&dimensions, __ATOMIC_RELAXED);
    int width = static_cast<int>(size >> 32), height = static_cast<int>(size & 0xffffffffu);
    GLint viewport[4]{}, framebuffer = -1, program = 0;
    getInteger(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    getInteger(GL_VIEWPORT, viewport);
    // ponytail: only full-window default-framebuffer draws; add offscreen pass
    // identification if a native renderer uses a separate main-world target.
    if (framebuffer != 0 || width <= 0 || height <= 0 || viewport[0] != 0 || viewport[1] != 0
        || viewport[2] != width || viewport[3] != height) {
        original(location, count, transpose, value);
        return;
    }
    getInteger(GL_CURRENT_PROGRAM, &program);
    // Select exactly one projection-bearing uniform to avoid double jitter.
    // Query live locations so relinking/program-ID reuse requires no cache.
    GLint projection = program ? getLocation(program, "u_proj") : -1;
    if (projection < 0 && program) projection = getLocation(program, "u_viewProj");
    if (projection < 0 && program) projection = getLocation(program, "u_modelViewProj");
    if (location != projection || projection < 0) {
        original(location, count, transpose, value);
        return;
    }
    const auto sample = __atomic_load_n(&sampleState, __ATOMIC_RELAXED);
    const auto mode = sample >> 3;
    const auto index = (sample & 7) % (2u << mode);
    const float x = offsets[mode][index][0] / (8.f * width);
    const float y = offsets[mode][index][1] / (8.f * height);
    float shifted[16];
    for (int col = 0; col < 4; ++col) {
        const int i = col * 4;
        // Left-multiply a clip-space translation: xy += offset * w.
        shifted[i] = value[i] + x * value[i + 3];
        shifted[i + 1] = value[i + 1] + y * value[i + 3];
        shifted[i + 2] = value[i + 2];
        shifted[i + 3] = value[i + 3];
    }
    original(location, count, transpose, shifted);
}
bool install(unsigned long base) {
    namespace programs = minecraft_build::current::render::shaderPrograms;
    if (!hooks::supported(base) || !hooks::readable(base, profile::slot, 8)
        || !hooks::matches(base, profile::plt, profile::pltSignature, sizeof(profile::pltSignature))) return false;
    auto pointer = *reinterpret_cast<const unsigned long*>(base + profile::slot);
    if (!hooks::readable(pointer, 0, 1, true)
        || (pointer >= base + programs::pltBegin && pointer < base + programs::pltEnd)
        || pointer == reinterpret_cast<unsigned long>(&upload)) return false;
    original = reinterpret_cast<Upload>(pointer);
    hooks::Patch patch{profile::slot, pointer, reinterpret_cast<unsigned long>(&upload)};
    bool result = hooks::install("Projection jitter AA", base, &patch, 1) == hooks::InstallResult::Installed;
    __atomic_store_n(&installed, result, __ATOMIC_RELEASE);
    return result;
}
}
void projection_jitter_init() {
    auto egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
    auto getProc = egl ? reinterpret_cast<decltype(&eglGetProcAddress)>(
        mcpelauncher_host_dlsym(egl, "eglGetProcAddress")) : nullptr;
    if (!getProc) return;
    getInteger = reinterpret_cast<decltype(getInteger)>(getProc("glGetIntegerv"));
    getLocation = reinterpret_cast<decltype(getLocation)>(getProc("glGetUniformLocation"));
    if (getInteger && getLocation && hooks::initialize() && install(hooks::find_game()))
        error = nullptr;
}
void projection_jitter_frame(int width, int height) {
    unsigned long long size = width > 0 && height > 0
        ? (static_cast<unsigned long long>(width) << 32) | static_cast<unsigned int>(height) : 0;
    __atomic_store_n(&dimensions, size, __ATOMIC_RELAXED);
    auto nextFrame = __atomic_add_fetch(&frame, 1u, __ATOMIC_RELAXED);
    auto mode = static_cast<unsigned int>(client_jitter_sample_mode());
    // Publish the mode and phase together, once per frame rather than per draw.
    __atomic_store_n(&sampleState, (mode << 3) | (nextFrame & 7), __ATOMIC_RELAXED);
}
const char* projection_jitter_error() { return error; }
