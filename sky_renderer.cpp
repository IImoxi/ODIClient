#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <pthread.h>
#include "client_modules.h"
#include "environment.h"
#include "fps_limiter.h"
#include "gpu_shader_services.h"
#include "render_gl_trace.h"
#include "launcher_api.h"
#include "sky_renderer.h"

namespace {
#ifdef ODI_SKY_TEST_OPTIONS
bool skyLookupEnabled = true, skyHalfResolutionEnabled = true, skyReducedSamplesEnabled = true;
#else
constexpr bool skyLookupEnabled = true, skyHalfResolutionEnabled = true, skyReducedSamplesEnabled = true;
#endif
// The replacement keeps the native interface when disabled. Its custom draw
// uses gl_VertexID and the live camera matrix, never the native dome's colors.
constexpr char skyVertexShader[] = R"GLSL(#version 310 es
uniform mat4 u_viewProj;
uniform mat4 u_model[4];
uniform vec4 SkyColor;
uniform vec4 FogColor;
uniform vec4 ODISkyVertex;
in vec4 a_color0;
in vec3 a_position;
in vec2 a_texcoord0;
out highp vec4 v_color0;
out highp vec2 v_texcoord0;
out highp vec3 v_worldPos;
void main() {
    v_texcoord0 = a_texcoord0;
    if (ODISkyVertex.x > 0.5) {
        vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                      (gl_VertexID == 2) ? 3.0 : -1.0);
        mat4 invViewProj = inverse(u_viewProj);
        vec4 nearPoint = invViewProj * vec4(p, -1.0, 1.0);
        vec4 farPoint = invViewProj * vec4(p, 1.0, 1.0);
        // Homogeneous difference also handles an infinite far projection (w=0).
        v_color0 = vec4(farPoint.xyz * nearPoint.w - nearPoint.xyz * farPoint.w, 2.0);
        v_worldPos = vec3(0.0);
        gl_Position = vec4(p, 1.0, 1.0);
    } else {
        vec4 world = u_model[0] * vec4(a_position, 1.0);
        v_color0 = mix(SkyColor, FogColor, vec4(a_color0.x));
        v_worldPos = world.xyz;
        gl_Position = u_viewProj * world;
    }
}
)GLSL";

constexpr char skyInstancedVertexShader[] = R"GLSL(#version 310 es
uniform mat4 u_viewProj;
uniform vec4 SkyColor;
uniform vec4 FogColor;
uniform vec4 ODISkyVertex;
in vec4 a_color0;
in vec3 a_position;
in vec2 a_texcoord0;
in vec4 i_data1;
in vec4 i_data2;
in vec4 i_data3;
out highp vec4 v_color0;
out highp vec2 v_texcoord0;
out highp vec3 v_worldPos;
void main() {
    v_texcoord0 = a_texcoord0;
    if (ODISkyVertex.x > 0.5) {
        vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                      (gl_VertexID == 2) ? 3.0 : -1.0);
        mat4 invViewProj = inverse(u_viewProj);
        vec4 nearPoint = invViewProj * vec4(p, -1.0, 1.0);
        vec4 farPoint = invViewProj * vec4(p, 1.0, 1.0);
        v_color0 = vec4(farPoint.xyz * nearPoint.w - nearPoint.xyz * farPoint.w, 2.0);
        v_worldPos = vec3(0.0);
        gl_Position = vec4(p, 1.0, 1.0);
    } else {
        mat4 model;
        model[0] = vec4(i_data1.x, i_data2.x, i_data3.x, 0.0);
        model[1] = vec4(i_data1.y, i_data2.y, i_data3.y, 0.0);
        model[2] = vec4(i_data1.z, i_data2.z, i_data3.z, 0.0);
        model[3] = vec4(i_data1.w, i_data2.w, i_data3.w, 1.0);
        vec4 world = model * vec4(a_position, 1.0);
        v_color0 = mix(SkyColor, FogColor, vec4(a_color0.x));
        v_worldPos = world.xyz;
        gl_Position = u_viewProj * world;
    }
}
)GLSL";

constexpr char skyFragmentShader[] = R"GLSL(#version 310 es
precision highp float;
precision highp int;
in highp vec4 v_color0;
uniform vec4 ODISkyFragment; // enabled, native celestial angle, unused, vanilla celestials
uniform vec4 ODISkyQuality; // reduced samples, source: direct/LUT/screen
uniform vec4 ODISkyViewport; // viewport origin and dimensions
uniform highp sampler2D ODISkyAtmosphere;
layout(location = 0) out highp vec4 bgfx_FragData0;
const float PI = 3.14159265359;
float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
vec3 atmosphere(vec3 ray, vec3 sun) {
    // Distances in km. Fixed single-scattering integration needs no textures.
    const float planet = 6360.0, top = 6460.0;
    const vec3 betaR = vec3(0.0058, 0.0135, 0.0331);
    const vec3 betaM = vec3(0.003);
    vec3 origin = vec3(0.0, planet + 0.2, 0.0);
    float b = dot(origin, ray);
    float distance = -b + sqrt(max(b*b + top*top - dot(origin, origin), 0.0));
    // Below the horizon, integrate to the ground instead of through the planet.
    float groundD = b*b + planet*planet - dot(origin, origin);
    if (ray.y < 0.0 && groundD > 0.0) distance = min(distance, -b - sqrt(groundD));
    int viewSamples = ODISkyQuality.x > 0.5 ? 6 : 8;
    int lightSamples = ODISkyQuality.x > 0.5 ? 3 : 4;
    float stepSize = max(distance, 0.0) / float(viewSamples);
    vec2 optical = vec2(0.0);
    vec3 scatterR = vec3(0.0), scatterM = vec3(0.0);
    for (int i = 0; i < viewSamples; ++i) {
        vec3 p = origin + ray * ((float(i) + 0.5) * stepSize);
        float height = max(length(p) - planet, 0.0);
        vec2 density = exp(-height / vec2(8.0, 1.2));
        optical += density * stepSize * 0.5;
        float sunB = dot(p, sun);
        float sunDistance = -sunB + sqrt(max(sunB*sunB + top*top - dot(p, p), 0.0));
        float sunStep = sunDistance / float(lightSamples);
        vec2 sunOptical = vec2(0.0);
        bool shadow = false;
        for (int j = 0; j < lightSamples; ++j) {
            float sunHeight = length(p + sun * ((float(j) + 0.5) * sunStep)) - planet;
            // A shadowed sample contributes no scattering; remaining sunlight
            // samples cannot change that result.
            if (sunHeight < 0.0) { shadow = true; break; }
            sunOptical += exp(-max(sunHeight, 0.0) / vec2(8.0, 1.2)) * sunStep;
        }
        if (!shadow) {
            vec3 attenuation = exp(-betaR * (optical.x + sunOptical.x)
                                  - betaM * (optical.y + sunOptical.y));
            scatterR += attenuation * density.x * stepSize;
            scatterM += attenuation * density.y * stepSize;
        }
        optical += density * stepSize * 0.5;
    }
    float mu = dot(ray, sun);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu*mu);
    const float g = 0.76;
    float phaseM = (1.0-g*g) / (4.0*PI * pow(max(1.0+g*g-2.0*g*mu, 0.01), 1.5));
    vec3 radiance = 22.0 * (scatterR * betaR * phaseR + scatterM * betaM * phaseM);
    radiance += vec3(0.002, 0.004, 0.012) * (0.4 + 0.6 * max(ray.y, 0.0));
    return 1.0 - exp(-radiance * 1.5);
}
vec3 directAtmosphere(vec3 ray, vec3 sun) {
    vec3 color;
    // Blend across the planet's apparent edge over one degree of elevation.
    const float horizonBottom = -0.01665, horizonTop = 0.00080;
    if (ray.y > horizonBottom && ray.y < horizonTop) {
        vec2 horizontal = normalize(ray.xz);
        vec3 bottomRay = vec3(horizontal.x, 0.0, horizontal.y) * sqrt(1.0 - horizonBottom*horizonBottom);
        vec3 topRay = vec3(horizontal.x, 0.0, horizontal.y) * sqrt(1.0 - horizonTop*horizonTop);
        bottomRay.y = horizonBottom;
        topRay.y = horizonTop;
        color = mix(atmosphere(bottomRay, sun), atmosphere(topRay, sun),
                    smoothstep(horizonBottom, horizonTop, ray.y));
    } else color = atmosphere(ray, sun);
    return color;
}
vec3 skyAtmosphere(vec3 ray, vec3 sun) {
    if (ODISkyQuality.y > 1.5)
        return texture(ODISkyAtmosphere, (gl_FragCoord.xy - ODISkyViewport.xy) / ODISkyViewport.zw).rgb;
    if (ODISkyQuality.y > 0.5) {
        // Atmosphere is symmetric around the sun's vertical plane. Squared
        // elevation spacing reserves more samples for the narrow horizon blend.
        float azimuth = ray.x * (sun.x < 0.0 ? -1.0 : 1.0) / max(length(ray.xz), 0.000001);
        vec2 uv = vec2(azimuth * 0.5 + 0.5, sign(ray.y) * sqrt(abs(ray.y)) * 0.5 + 0.5);
        return texture(ODISkyAtmosphere, (uv * 255.0 + 0.5) / 256.0).rgb;
    }
    return directAtmosphere(ray, sun);
}
vec3 sky(vec3 ray) {
    // Native angle 0 is noon, 0.5 is midnight (ticks/24000 - 0.25, smoothed).
    float rotation = ODISkyFragment.y * 2.0 * PI;
    vec3 sun = vec3(-sin(rotation), cos(rotation), 0.0);
    vec3 moon = -sun;
    float day = smoothstep(-0.12, 0.08, sun.y);
    vec3 color = skyAtmosphere(ray, sun);
    float nightBrightness = mix(0.55, 1.0, smoothstep(-0.35, -0.08, sun.y));
    color *= nightBrightness;
    float sunDot = dot(ray, sun), moonDot = dot(ray, moon);
    if (ODISkyFragment.w < 0.5) {
    // Angular glow gives the disk a bright core and soft bloom-like halo.
    float sunDistance = max(1.0 - sunDot, 0.0);
    float sunVisibility = smoothstep(-0.020, 0.004, sun.y);
    float horizonVisibility = smoothstep(-0.012, 0.002, ray.y);
    color += vec3(1.0, 0.78, 0.40)
             * (0.65 * exp(-sunDistance / 0.0018) + 0.12 * exp(-sunDistance / 0.018))
             * sunVisibility * horizonVisibility;
    color += vec3(3.0, 2.6, 1.9) * smoothstep(cos(0.012), cos(0.009), sunDot)
             * sunVisibility * horizonVisibility;
    float moonDisk = smoothstep(cos(0.017), cos(0.014), moonDot);
    color += vec3(0.50, 0.60, 0.80) * moonDisk * (1.0 - day);
    }
    // These regions have exactly zero star visibility in the original formula.
    if (day < 1.0 && ray.y > 0.0) {
        vec3 cell = floor(ray * 450.0);
        float star = hash(cell);
        if (star >= 0.992) {
            float starShape = pow(max(1.0 - length(fract(ray * 450.0) - 0.5) * 2.0, 0.0), 5.0);
            color += mix(vec3(0.65, 0.78, 1.0), vec3(1.0, 0.82, 0.62), hash(cell + 7.0))
                     * starShape * 2.5 * (1.0-day) * smoothstep(0.0, 0.18, ray.y);
        }
    }
    return pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
}
void main() {
#if defined(ODI_LUT_PASS)
    vec2 uv = (gl_FragCoord.xy - 0.5) / 255.0;
    float elevation = uv.y * 2.0 - 1.0;
    float y = sign(elevation) * elevation * elevation;
    float azimuth = uv.x * 2.0 - 1.0;
    float rotation = ODISkyFragment.y * 2.0 * PI;
    vec3 sun = vec3(-sin(rotation), cos(rotation), 0.0);
    vec3 ray = vec3(azimuth * (sun.x < 0.0 ? -1.0 : 1.0), 0.0,
                    sqrt(max(1.0 - azimuth * azimuth, 0.0))) * sqrt(max(1.0 - y*y, 0.0));
    ray.y = y;
    bgfx_FragData0 = vec4(directAtmosphere(ray, sun), 1.0);
#elif defined(ODI_ATMOSPHERE_PASS)
    float rotation = ODISkyFragment.y * 2.0 * PI;
    bgfx_FragData0 = vec4(skyAtmosphere(normalize(v_color0.xyz),
                                  vec3(-sin(rotation), cos(rotation), 0.0)), 1.0);
#else
    bgfx_FragData0 = (ODISkyFragment.x > 0.5 && v_color0.a > 1.5)
        ? vec4(sky(normalize(v_color0.xyz)), 1.0) : v_color0;
#endif
}
)GLSL";

using GetUniformLocation = GLint (*)(GLuint, const GLchar*);
GetUniformLocation getUniformLocation;
decltype(&glGetShaderiv) getShaderiv;
decltype(&glGetShaderInfoLog) getShaderInfoLog;
decltype(&glGetProgramiv) getProgramiv;
decltype(&glGetProgramInfoLog) getProgramInfoLog;
decltype(&glGetAttachedShaders) getAttachedShaders;
decltype(&glGetUniformfv) getUniform;
decltype(&glGetIntegerv) getInteger;
decltype(&glGetBooleanv) getBoolean;
decltype(&glIsEnabled) isEnabled;
decltype(&glEnable) enable;
decltype(&glDisable) disable;
decltype(&glDepthFunc) depthFunc;
decltype(&glDepthMask) depthMask;
decltype(&glUniform4fv) uniform4;
decltype(&glDrawArrays) drawArrays;
decltype(&glCreateShader) apiCreateShader;
decltype(&glShaderSource) apiShaderSource;
decltype(&glCompileShader) apiCompileShader;
decltype(&glDeleteShader) apiDeleteShader;
decltype(&glCreateProgram) apiCreateProgram;
decltype(&glAttachShader) apiAttachShader;
decltype(&glLinkProgram) apiLinkProgram;
decltype(&glDeleteProgram) apiDeleteProgram;
decltype(&glUseProgram) apiUseProgram;
decltype(&glUniform1i) apiUniform1i;
decltype(&glUniformMatrix4fv) apiUniformMatrix4fv;
decltype(&glGenTextures) apiGenTextures;
decltype(&glDeleteTextures) apiDeleteTextures;
decltype(&glActiveTexture) apiActiveTexture;
decltype(&glBindTexture) apiBindTexture;
decltype(&glTexImage2D) apiTexImage2D;
decltype(&glTexParameteri) apiTexParameteri;
decltype(&glBindSampler) apiBindSampler;
decltype(&glGenFramebuffers) apiGenFramebuffers;
decltype(&glDeleteFramebuffers) apiDeleteFramebuffers;
decltype(&glBindFramebuffer) apiBindFramebuffer;
decltype(&glFramebufferTexture2D) apiFramebufferTexture2D;
decltype(&glCheckFramebufferStatus) apiCheckFramebufferStatus;
decltype(&glViewport) apiViewport;
decltype(&glColorMask) apiColorMask;
decltype(&glGenVertexArrays) apiGenVertexArrays;
decltype(&glBindVertexArray) apiBindVertexArray;
decltype(&glBindBuffer) apiBindBuffer;
decltype(&eglGetCurrentContext) getContext;
decltype(&pthread_key_create) createKey;
decltype(&pthread_getspecific) getSpecific;
decltype(&pthread_setspecific) setSpecific;
void* (*allocate)(unsigned long);
void (*release)(void*);
pthread_key_t stateKey;
bool initialized, available, linkedObserved, drawObserved, matrixMissing, linkFailed;
unsigned int revision;
const char* error;
constexpr unsigned int standardVertexSeen = 1, fragmentSeen = 2, instancedVertexSeen = 4;
unsigned int shaderStages, shaderCompiledStages, shaderFailedStages;
struct ShaderRecord { unsigned int id, stage; } shaderRecords[64]{};
unsigned int failureClaimed;
char driverFailure[256]{};
enum class Kind { Other, Sky, NativeCelestial, NativeSunMoon };
struct ProgramUniforms { GLuint program; Kind kind; GLint vertex, fragment, matrix, quality, viewport, atmosphere; };
struct AtmosphereTarget { GLuint texture, framebuffer; int width, height; };
struct ContextTargets {
    EGLContext context;
    ProgramUniforms lookupProgram, halfProgram;
    GLuint vao;
    AtmosphereTarget lookup, half;
    float angle;
    bool reduced, lookupValid, failed;
    GLint textureUnit;
};
struct ThreadState {
    EGLContext context;
    unsigned int revision, next;
    ProgramUniforms programs[32];
    ProgramUniforms current;
    long long replacedFrame;
    ContextTargets targets[4];
};

bool active() {
    return client_environment_enabled() && client_environment_sky() && !environment_error();
}
bool loadApi() {
    auto egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
    auto libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!egl || !libc) return false;
    auto getProc = reinterpret_cast<decltype(&eglGetProcAddress)>(mcpelauncher_host_dlsym(egl, "eglGetProcAddress"));
    if (!getProc) return false;
#define GL(variable, name) variable = reinterpret_cast<decltype(variable)>(getProc(name)); if (!variable) return false;
    GL(getUniformLocation, "glGetUniformLocation"); GL(getShaderiv, "glGetShaderiv");
    GL(getShaderInfoLog, "glGetShaderInfoLog"); GL(getProgramiv, "glGetProgramiv");
    GL(getProgramInfoLog, "glGetProgramInfoLog"); GL(getInteger, "glGetIntegerv");
    GL(getAttachedShaders, "glGetAttachedShaders");
    GL(getUniform, "glGetUniformfv");
    GL(getBoolean, "glGetBooleanv"); GL(isEnabled, "glIsEnabled");
    GL(enable, "glEnable"); GL(disable, "glDisable");
    GL(depthFunc, "glDepthFunc"); GL(depthMask, "glDepthMask");
    GL(uniform4, "glUniform4fv"); GL(drawArrays, "glDrawArrays");
    GL(apiCreateShader, "glCreateShader");
    GL(apiShaderSource, "glShaderSource");
    GL(apiCompileShader, "glCompileShader");
    GL(apiDeleteShader, "glDeleteShader");
    GL(apiCreateProgram, "glCreateProgram");
    GL(apiAttachShader, "glAttachShader");
    GL(apiLinkProgram, "glLinkProgram");
    GL(apiDeleteProgram, "glDeleteProgram");
    GL(apiUseProgram, "glUseProgram");
    GL(apiUniform1i, "glUniform1i");
    GL(apiUniformMatrix4fv, "glUniformMatrix4fv");
    GL(apiGenTextures, "glGenTextures");
    GL(apiDeleteTextures, "glDeleteTextures");
    GL(apiActiveTexture, "glActiveTexture");
    GL(apiBindTexture, "glBindTexture");
    GL(apiTexImage2D, "glTexImage2D");
    GL(apiTexParameteri, "glTexParameteri");
    GL(apiBindSampler, "glBindSampler");
    GL(apiGenFramebuffers, "glGenFramebuffers");
    GL(apiDeleteFramebuffers, "glDeleteFramebuffers");
    GL(apiBindFramebuffer, "glBindFramebuffer");
    GL(apiFramebufferTexture2D, "glFramebufferTexture2D");
    GL(apiCheckFramebufferStatus, "glCheckFramebufferStatus");
    GL(apiViewport, "glViewport");
    GL(apiColorMask, "glColorMask");
    GL(apiGenVertexArrays, "glGenVertexArrays");
    GL(apiBindVertexArray, "glBindVertexArray");
    GL(apiBindBuffer, "glBindBuffer");
#undef GL
#define HOST(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(libc, name)); if (!variable) return false;
    HOST(createKey, "pthread_key_create"); HOST(getSpecific, "pthread_getspecific");
    HOST(setSpecific, "pthread_setspecific"); HOST(allocate, "malloc");
    HOST(release, "free");
#undef HOST
    getContext = reinterpret_cast<decltype(getContext)>(mcpelauncher_host_dlsym(egl, "eglGetCurrentContext"));
    return getContext && createKey(&stateKey, release) == 0;
}
ThreadState* state(bool create) {
    auto context = getContext();
    if (context == EGL_NO_CONTEXT) return nullptr;
    auto result = static_cast<ThreadState*>(getSpecific(stateKey));
    if (!result && create) {
        result = static_cast<ThreadState*>(allocate(sizeof(ThreadState)));
        if (!result) return nullptr;
        *result = {};
        if (setSpecific(stateKey, result) != 0) { release(result); return nullptr; }
    }
    if (!result) return nullptr;
    auto version = __atomic_load_n(&revision, __ATOMIC_ACQUIRE);
    if (result->context != context || result->revision != version) {
        result->next = 0; result->current = {}; result->replacedFrame = 0;
        for (auto& program : result->programs) program = {};
        result->context = context; result->revision = version;
    }
    return result;
}
void parameters(const ProgramUniforms& program, bool enabled) {
    GLfloat vertex[] = {enabled ? 1.0f : 0.0f, 0, 0, 0};
    float angle = environment_sky_angle();
    if (!(angle >= 0.0f && angle <= 1.0f)) angle = 0.0f;
    GLfloat fragment[] = {enabled ? 1.0f : 0.0f, angle, 0,
                          client_environment_vanilla_celestials() ? 1.0f : 0.0f};
    uniform4(program.vertex, 1, vertex);
    uniform4(program.fragment, 1, fragment);
    const GLfloat quality[] = {skyReducedSamplesEnabled ? 1.0f : 0.0f, 0, 0, 0};
    uniform4(program.quality, 1, quality);
}
void invalidated(unsigned int) { __atomic_fetch_add(&revision, 1, __ATOMIC_RELEASE); }
void rememberFailure(GLuint object, bool shader) {
    unsigned int expected = 0;
    if (!__atomic_compare_exchange_n(&failureClaimed, &expected, 1, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    GLsizei written = 0;
    if (shader) getShaderInfoLog(object, sizeof(driverFailure)-1, &written, driverFailure);
    else getProgramInfoLog(object, sizeof(driverFailure)-1, &written, driverFailure);
    if (written < 0) written = 0;
    if (written >= static_cast<GLsizei>(sizeof(driverFailure))) written = sizeof(driverFailure)-1;
    driverFailure[written] = 0;
    for (int i = 0; i < written; ++i) if (driverFailure[i] < 32) driverFailure[i] = ' ';
    __atomic_store_n(&failureClaimed, 2, __ATOMIC_RELEASE);
}
void programBound(unsigned int program) {
    if (!__atomic_load_n(&available, __ATOMIC_ACQUIRE)) return;
    auto s = state(true);
    if (!s) return;
    s->current = {};
    if (!program) return;
    ProgramUniforms* entry = nullptr;
    for (auto& cached : s->programs) if (cached.program == program) { entry = &cached; break; }
    if (!entry) {
        GLint linked = 0;
        getProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLuint attached[4]; GLsizei count = 0;
            getAttachedShaders(program, 4, &count, attached);
            for (GLsizei i = 0; i < count; ++i) {
                auto& record = shaderRecords[attached[i] % 64];
                if (__atomic_load_n(&record.id, __ATOMIC_ACQUIRE) == attached[i]
                    && (__atomic_load_n(&record.stage, __ATOMIC_RELAXED)
                        & (standardVertexSeen | instancedVertexSeen))) {
                    rememberFailure(program, false);
                    __atomic_store_n(&linkFailed, true, __ATOMIC_RELEASE);
                }
            }
            return;
        }
        auto& cached = s->programs[s->next++ % 32];
        cached = {program, Kind::Other, getUniformLocation(program, "ODISkyVertex"),
                  getUniformLocation(program, "ODISkyFragment"), getUniformLocation(program, "u_viewProj"),
                  getUniformLocation(program, "ODISkyQuality"), getUniformLocation(program, "ODISkyViewport"),
                  getUniformLocation(program, "ODISkyAtmosphere")};
        if (cached.vertex >= 0 && cached.fragment >= 0 && cached.matrix >= 0) cached.kind = Kind::Sky;
        else if (getUniformLocation(program, "SunMoonColor") >= 0) cached.kind = Kind::NativeSunMoon;
        else if (getUniformLocation(program, "CloudColor") >= 0
                 || getUniformLocation(program, "StarsColor") >= 0) cached.kind = Kind::NativeCelestial;
        // Clouds shares Sky's original fragment. A fragment match alone must
        // remain native; only our vertex+fragment marker pair permits replacement.
        if (cached.kind != Kind::Sky && cached.fragment >= 0) {
            const GLfloat off[] = {0, 0, 0, 0}; uniform4(cached.fragment, 1, off);
        }
        entry = &cached;
    }
    s->current = *entry;
    if (entry->kind == Kind::Sky) {
        // Only an intercepted draw may enable the full-screen vertex path.
        // A renderer path outside our draw coverage must remain native.
        parameters(*entry, false);
        __atomic_store_n(&linkedObserved, true, __ATOMIC_RELEASE);
    }
}
void capability(GLenum name, GLboolean value) { if (value) enable(name); else disable(name); }
bool validMatrix(const GLfloat* m) {
    for (int i = 0; i < 16; ++i) if (!(m[i] >= -1e12f && m[i] <= 1e12f)) return false;
    float a = m[0]*m[5]-m[1]*m[4], b = m[0]*m[6]-m[2]*m[4];
    float c = m[0]*m[7]-m[3]*m[4], d = m[1]*m[6]-m[2]*m[5];
    float e = m[1]*m[7]-m[3]*m[5], f = m[2]*m[7]-m[3]*m[6];
    float determinant = a*(m[10]*m[15]-m[11]*m[14])-b*(m[9]*m[15]-m[11]*m[13])
        + c*(m[9]*m[14]-m[10]*m[13])+d*(m[8]*m[15]-m[11]*m[12])
        - e*(m[8]*m[14]-m[10]*m[12])+f*(m[8]*m[13]-m[9]*m[12]);
    return (determinant > 1e-12f && determinant < 1e30f)
        || (determinant < -1e-12f && determinant > -1e30f);
}
constexpr char lookupVertexShader[] = R"GLSL(#version 310 es
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";
ProgramUniforms atmosphereProgram(bool lookup) {
    GLuint vertex = apiCreateShader(GL_VERTEX_SHADER), fragment = apiCreateShader(GL_FRAGMENT_SHADER);
    const char* vertexSource = lookup ? lookupVertexShader : skyVertexShader;
    // Compile the same atmosphere implementation for both offscreen passes.
    const char* sources[] = {"#version 310 es\n", lookup ? "#define ODI_LUT_PASS\n" : "#define ODI_ATMOSPHERE_PASS\n",
                            skyFragmentShader + sizeof("#version 310 es\n") - 1};
    apiShaderSource(vertex, 1, &vertexSource, nullptr); apiCompileShader(vertex);
    apiShaderSource(fragment, 3, sources, nullptr); apiCompileShader(fragment);
    GLint vertexOk = 0, fragmentOk = 0;
    getShaderiv(vertex, GL_COMPILE_STATUS, &vertexOk); getShaderiv(fragment, GL_COMPILE_STATUS, &fragmentOk);
    GLuint program = 0;
    if (vertexOk && fragmentOk) {
        program = apiCreateProgram(); apiAttachShader(program, vertex); apiAttachShader(program, fragment);
        apiLinkProgram(program); GLint linked = 0; getProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) { apiDeleteProgram(program); program = 0; }
    }
    apiDeleteShader(vertex); apiDeleteShader(fragment);
    if (!program) return {};
    return {program, Kind::Sky, getUniformLocation(program, "ODISkyVertex"),
            getUniformLocation(program, "ODISkyFragment"), getUniformLocation(program, "u_viewProj"),
            getUniformLocation(program, "ODISkyQuality"), getUniformLocation(program, "ODISkyViewport"),
            getUniformLocation(program, "ODISkyAtmosphere")};
}
bool resizeTarget(AtmosphereTarget& target, int width, int height) {
    if (target.width == width && target.height == height) return true;
    GLint maximum = 0; getInteger(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width < 1 || height < 1 || width > maximum || height > maximum || width > 8192 || height > 8192) return false;
    if (!target.texture) apiGenTextures(1, &target.texture);
    if (!target.framebuffer) apiGenFramebuffers(1, &target.framebuffer);
    apiBindTexture(GL_TEXTURE_2D, target.texture);
    apiTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    apiTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    apiTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    apiTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint unpack = 0; getInteger(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    apiBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    apiTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    apiBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack));
    apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
    apiFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    if (apiCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        apiDeleteTextures(1, &target.texture); apiDeleteFramebuffers(1, &target.framebuffer);
        target = {}; return false;
    }
    target.width = width; target.height = height; return true;
}
struct TextureBinding {
    GLint active = 0, texture = 0, sampler = 0, unit = -1;
    void save(GLint index) {
        unit = index; getInteger(GL_ACTIVE_TEXTURE, &active);
        apiActiveTexture(GL_TEXTURE0 + unit);
        getInteger(GL_TEXTURE_BINDING_2D, &texture); getInteger(GL_SAMPLER_BINDING, &sampler);
        apiBindSampler(unit, 0);
    }
    ~TextureBinding() {
        if (unit < 0) return;
        apiActiveTexture(GL_TEXTURE0 + unit);
        apiBindTexture(GL_TEXTURE_2D, texture); apiBindSampler(unit, sampler);
        apiActiveTexture(active);
    }
};
struct PassState {
    GLint framebuffer = 0, viewport[4]{}, vao = 0, program = 0;
    GLboolean color[4]{}, scissor, stencil, dither, depth, coverage, alphaCoverage, sampleMask;
    PassState() {
        getInteger(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer); getInteger(GL_VIEWPORT, viewport);
        getInteger(GL_VERTEX_ARRAY_BINDING, &vao); getInteger(GL_CURRENT_PROGRAM, &program);
        getBoolean(GL_COLOR_WRITEMASK, color);
        scissor = isEnabled(GL_SCISSOR_TEST); stencil = isEnabled(GL_STENCIL_TEST);
        dither = isEnabled(GL_DITHER); depth = isEnabled(GL_DEPTH_TEST);
        coverage = isEnabled(GL_SAMPLE_COVERAGE); alphaCoverage = isEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
        sampleMask = isEnabled(GL_SAMPLE_MASK);
        disable(GL_SCISSOR_TEST); disable(GL_STENCIL_TEST); disable(GL_DITHER); disable(GL_DEPTH_TEST);
        disable(GL_SAMPLE_COVERAGE); disable(GL_SAMPLE_ALPHA_TO_COVERAGE); disable(GL_SAMPLE_MASK);
        apiColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    ~PassState() {
        apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        apiViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        apiBindVertexArray(vao); apiUseProgram(program);
        apiColorMask(color[0], color[1], color[2], color[3]);
        capability(GL_SCISSOR_TEST, scissor); capability(GL_STENCIL_TEST, stencil);
        capability(GL_DITHER, dither); capability(GL_DEPTH_TEST, depth);
        capability(GL_SAMPLE_COVERAGE, coverage); capability(GL_SAMPLE_ALPHA_TO_COVERAGE, alphaCoverage);
        capability(GL_SAMPLE_MASK, sampleMask);
    }
};
bool optimizationFallback;
void prepareAtmosphere(ThreadState& s, const GLfloat* matrix, TextureBinding& binding) {
    bool lookup = skyLookupEnabled, half = skyHalfResolutionEnabled;
    if (!lookup && !half) {
        __atomic_store_n(&optimizationFallback, false, __ATOMIC_RELEASE); return;
    }
    ContextTargets* targets = nullptr;
    for (auto& cached : s.targets) if (cached.context == s.context) { targets = &cached; break; }
    if (!targets) for (auto& cached : s.targets) if (!cached.context) {
        cached.context = s.context;
        GLint units = 0; getInteger(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        cached.textureUnit = units - 1; targets = &cached; break;
    }
    if (!targets || targets->failed || targets->textureUnit < 0) {
        __atomic_store_n(&optimizationFallback, true, __ATOMIC_RELEASE); return;
    }
    auto& t = *targets;
    binding.save(t.textureUnit);
    GLfloat angle = environment_sky_angle();
    if (!(angle >= 0.0f && angle <= 1.0f)) angle = 0;
    bool reduced = skyReducedSamplesEnabled;
    GLuint texture = lookup ? t.lookup.texture : 0;
    int source = lookup ? 1 : 0;
    GLint viewport[4]; getInteger(GL_VIEWPORT, viewport);
    // Reuse the lookup within 1/4096 of a day (~0.088 degrees). This avoids
    // regenerating it for every partial tick while celestial positions stay live.
    float angleDelta = angle > t.angle ? angle - t.angle : t.angle - angle;
    if (angleDelta > 0.5f) angleDelta = 1.0f - angleDelta;
    bool refresh = !t.lookupValid || angleDelta > 1.0f / 4096.0f || reduced != t.reduced;
    // An unchanged lookup needs no offscreen draw or framebuffer/state switches.
    if (half || refresh) {
        PassState saved;
        if (!t.vao) apiGenVertexArrays(1, &t.vao);
        apiBindVertexArray(t.vao);
        if (lookup) {
            if (!t.lookupProgram.program) t.lookupProgram = atmosphereProgram(true);
            if (!t.lookupProgram.program || !resizeTarget(t.lookup, 256, 256)) t.failed = true;
            else {
                if (refresh) {
                    apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.lookup.framebuffer); apiViewport(0, 0, 256, 256);
                    apiUseProgram(t.lookupProgram.program); parameters(t.lookupProgram, true);
                    drawArrays(GL_TRIANGLES, 0, 3);
                    t.angle = angle; t.reduced = reduced; t.lookupValid = true;
                }
                texture = t.lookup.texture; source = 1;
            }
        }
        if (half && !t.failed) {
            if (!t.halfProgram.program) t.halfProgram = atmosphereProgram(false);
            if (!t.halfProgram.program || !resizeTarget(t.half, (viewport[2] + 1) / 2, (viewport[3] + 1) / 2)) t.failed = true;
            else {
                apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.half.framebuffer);
                apiViewport(0, 0, t.half.width, t.half.height);
                apiUseProgram(t.halfProgram.program); parameters(t.halfProgram, true);
                apiUniformMatrix4fv(t.halfProgram.matrix, 1, GL_FALSE, matrix);
                const GLfloat quality[] = {reduced ? 1.0f : 0.0f, static_cast<float>(source), 0, 0};
                uniform4(t.halfProgram.quality, 1, quality);
                apiBindTexture(GL_TEXTURE_2D, texture);
                apiUniform1i(t.halfProgram.atmosphere, t.textureUnit);
                drawArrays(GL_TRIANGLES, 0, 3);
                texture = t.half.texture; source = 2;
            }
        }
    }
    __atomic_store_n(&optimizationFallback, t.failed, __ATOMIC_RELEASE);
    if (t.failed) return;
    apiBindTexture(GL_TEXTURE_2D, texture);
    const GLfloat quality[] = {reduced ? 1.0f : 0.0f, static_cast<float>(source), 0, 0};
    const GLfloat bounds[] = {static_cast<float>(viewport[0]), static_cast<float>(viewport[1]),
                              static_cast<float>(viewport[2]), static_cast<float>(viewport[3])};
    uniform4(s.current.quality, 1, quality); uniform4(s.current.viewport, 1, bounds);
    apiUniform1i(s.current.atmosphere, t.textureUnit);
    apiActiveTexture(binding.active);
}
bool replaceDraw() {
    if (!__atomic_load_n(&available, __ATOMIC_ACQUIRE)) return false;
    auto s = static_cast<ThreadState*>(getSpecific(stateKey));
    if (!s || s->current.kind == Kind::Other) return false;
    if (s->context != getContext() || s->revision != __atomic_load_n(&revision, __ATOMIC_ACQUIRE)) return false;
    bool enabled = active();
    auto frame = fps_limiter_frame_timestamp_ns();
    if ((s->current.kind == Kind::NativeCelestial || s->current.kind == Kind::NativeSunMoon)
        && (!enabled || frame <= 0 || frame != s->replacedFrame)) return false;
    GLint current = 0;
    getInteger(GL_CURRENT_PROGRAM, &current);
    if (current != static_cast<GLint>(s->current.program)) return false;
    if (s->current.kind == Kind::NativeCelestial) return true;
    if (s->current.kind == Kind::NativeSunMoon) return !client_environment_vanilla_celestials();
    if (!enabled) { parameters(s->current, false); s->replacedFrame = 0; return false; }
    GLfloat matrix[16];
    getUniform(s->current.program, s->current.matrix, matrix);
    if (!validMatrix(matrix)) {
        __atomic_store_n(&matrixMissing, true, __ATOMIC_RELEASE);
        return false;
    }
    GLboolean feedback = GL_FALSE, paused = GL_FALSE;
    getBoolean(GL_TRANSFORM_FEEDBACK_ACTIVE, &feedback);
    getBoolean(GL_TRANSFORM_FEEDBACK_PAUSED, &paused);
    if ((feedback && !paused) || isEnabled(GL_RASTERIZER_DISCARD)) return false;
    parameters(s->current, true);
    GLboolean cull = isEnabled(GL_CULL_FACE), depth = isEnabled(GL_DEPTH_TEST), blend = isEnabled(GL_BLEND);
    GLint function = 0;
    GLboolean write = GL_FALSE;
    getInteger(GL_DEPTH_FUNC, &function); getBoolean(GL_DEPTH_WRITEMASK, &write);
    disable(GL_CULL_FACE); disable(GL_BLEND); enable(GL_DEPTH_TEST);
    depthFunc(GL_LEQUAL); depthMask(GL_FALSE);
    TextureBinding textureBinding;
    prepareAtmosphere(*s, matrix, textureBinding);
    drawArrays(GL_TRIANGLES, 0, 3);
    depthFunc(static_cast<GLenum>(function)); depthMask(write);
    capability(GL_CULL_FACE, cull); capability(GL_DEPTH_TEST, depth); capability(GL_BLEND, blend);
    parameters(s->current, false);
    s->replacedFrame = frame;
    __atomic_store_n(&matrixMissing, false, __ATOMIC_RELEASE);
    __atomic_store_n(&drawObserved, true, __ATOMIC_RELEASE);
    return true;
}
unsigned int sourceLength(const char* source, int length) {
    if (length >= 0) return static_cast<unsigned int>(length > 65536 ? 65536 : length);
    unsigned int result = 0;
    while (result < 65536 && source[result]) ++result;
    return result;
}
bool contains(int count, const char* const* sources, const int* lengths, const char* token) {
    unsigned int tokenLength = 0;
    while (token[tokenLength]) ++tokenLength;
    for (int i = 0; i < count; ++i) {
        if (!sources[i]) continue;
        unsigned int length = sourceLength(sources[i], lengths ? lengths[i] : -1);
        if (!tokenLength || tokenLength > length) continue;
        for (unsigned int at = 0; at <= length - tokenLength; ++at) {
            unsigned int j = 0;
            while (j < tokenLength && sources[i][at+j] == token[j]) ++j;
            if (j == tokenLength) return true;
        }
    }
    return false;
}
const char* transformShaderSource(int count, const char* const* sources, const int* lengths, int* length) {
    if (count <= 0 || !sources || !length) return nullptr;
    const char* replacement = nullptr;
    unsigned int stage = 0;
    bool vertex = contains(count, sources, lengths, "uniform vec4 SkyColor;")
        && contains(count, sources, lengths, "uniform vec4 FogColor;")
        && contains(count, sources, lengths, "in vec4 a_color0;")
        && contains(count, sources, lengths, "in vec3 a_position;")
        && contains(count, sources, lengths, "v_color0 = mix(SkyColor, FogColor");
    if (vertex && contains(count, sources, lengths, "in vec4 i_data1;")) {
        replacement = skyInstancedVertexShader; *length = sizeof(skyInstancedVertexShader)-1; stage = instancedVertexSeen;
    } else if (vertex && contains(count, sources, lengths, "uniform mat4 u_model[4];")) {
        replacement = skyVertexShader; *length = sizeof(skyVertexShader)-1; stage = standardVertexSeen;
    } else if (contains(count, sources, lengths, "in highp vec4 v_color0;")
               && contains(count, sources, lengths, "bgfx_FragData0 = vec4(v_color0.xyz,")) {
        replacement = skyFragmentShader; *length = sizeof(skyFragmentShader)-1; stage = fragmentSeen;
    }
    if (stage) __atomic_fetch_or(&shaderStages, stage, __ATOMIC_RELEASE);
    return replacement;
}
void noteShaderSource(unsigned int shader, const char* replacement) {
    unsigned int stage = replacement == skyVertexShader ? standardVertexSeen
        : replacement == skyInstancedVertexShader ? instancedVertexSeen
        : replacement == skyFragmentShader ? fragmentSeen : 0;
    if (!stage || !shader) return;
    auto& record = shaderRecords[shader % 64];
    __atomic_store_n(&record.stage, stage, __ATOMIC_RELAXED);
    __atomic_store_n(&record.id, shader, __ATOMIC_RELEASE);
}
void noteShaderCompile(unsigned int shader) {
    auto& record = shaderRecords[shader % 64];
    if (__atomic_load_n(&record.id, __ATOMIC_ACQUIRE) != shader) return;
    auto stage = __atomic_load_n(&record.stage, __ATOMIC_RELAXED);
    GLint compiled = 0;
    getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled) __atomic_fetch_or(&shaderCompiledStages, stage, __ATOMIC_RELEASE);
    else {
        rememberFailure(shader, true);
        __atomic_fetch_or(&shaderFailedStages, stage, __ATOMIC_RELEASE);
    }
}
}
void sky_renderer_init() {
    if (initialized) return;
    initialized = true;
    if (!loadApi()) error = "Custom sky: GL/thread adapter unavailable";
    else if (!gpu_shader_services_set_shader_source_transform(transformShaderSource)) error = "Custom sky: shader-source hook unavailable";
    else if (!gpu_shader_services_set_shader_diagnostics(noteShaderSource, noteShaderCompile)) error = "Custom sky: shader-compile hook unavailable";
    else if (!gpu_shader_services_set_program_observer(programBound, invalidated)) error = "Custom sky: program hook unavailable";
    else if (!render_gl_trace_set_draw_observer(replaceDraw)) error = "Custom sky: draw hook unavailable";
    else __atomic_store_n(&available, true, __ATOMIC_RELEASE);
}
const char* sky_renderer_error() { return error; }
const char* sky_renderer_status() {
    if (error) return error;
    if (__atomic_load_n(&matrixMissing, __ATOMIC_ACQUIRE)) return "Custom sky: native camera matrix unavailable";
    if (__atomic_load_n(&optimizationFallback, __ATOMIC_ACQUIRE)) return "Sky texture optimization unavailable; using direct atmosphere";
    if (__atomic_load_n(&drawObserved, __ATOMIC_ACQUIRE)) return "Full-screen custom sky draw observed";
    if (__atomic_load_n(&linkedObserved, __ATOMIC_ACQUIRE)) return "Custom sky linked; waiting for sky draw";
    if (__atomic_load_n(&linkFailed, __ATOMIC_ACQUIRE))
        return __atomic_load_n(&failureClaimed, __ATOMIC_ACQUIRE) == 2 && driverFailure[0]
            ? driverFailure : "Custom sky program linking failed";
    if (__atomic_load_n(&shaderFailedStages, __ATOMIC_ACQUIRE))
        return __atomic_load_n(&failureClaimed, __ATOMIC_ACQUIRE) == 2 && driverFailure[0]
            ? driverFailure : "Custom sky shader compilation failed";
    auto compiled = __atomic_load_n(&shaderCompiledStages, __ATOMIC_ACQUIRE);
    if ((compiled & fragmentSeen) && (compiled & (standardVertexSeen | instancedVertexSeen)))
        return "Custom sky shaders compiled; waiting for active program";
    auto seen = __atomic_load_n(&shaderStages, __ATOMIC_ACQUIRE);
    return seen ? "Custom sky source matched; waiting for compilation" : "Waiting for native sky shader source";
}
