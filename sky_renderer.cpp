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
bool skyCloudTemporalEnabled = true;
#else
constexpr bool skyLookupEnabled = true, skyHalfResolutionEnabled = true, skyReducedSamplesEnabled = true;
constexpr bool skyCloudTemporalEnabled = true;
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
        vec2 direction = p+ODISkyVertex.yz;
        mat4 invViewProj = inverse(u_viewProj);
        vec4 nearPoint = invViewProj * vec4(direction, -1.0, 1.0);
        vec4 farPoint = invViewProj * vec4(direction, 1.0, 1.0);
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
uniform vec4 ODISkyFragment; // enabled, native celestial angle, weather amount, vanilla celestials
uniform vec4 ODISkyQuality; // reduced samples, source: direct/LUT/screen
uniform vec4 ODISkyViewport; // viewport origin and dimensions
uniform highp sampler2D ODISkyAtmosphere;
uniform vec4 ODISkyClouds; // enabled/seed, blend/detail, view samples/tile size, atlas tile size
uniform highp sampler2D ODISkyCloudA;
uniform highp sampler2D ODISkyCloudB;
uniform highp sampler2D ODISkyCloudLayer;
uniform vec4 ODISkyCloudResolve; // subpixel offset UV, history weight, sampling phase
uniform mat4 ODISkyCloudPrevious;
layout(location = 0) out highp vec4 bgfx_FragData0;
const float PI = 3.14159265359;
const float cloudBase = 1.2, cloudTop = 4.0;
float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float cloudNoise(vec3 p, vec2 period, float seed) {
    vec3 cell = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    float values[8];
    for (int i = 0; i < 8; ++i) {
        vec3 q = cell + vec3(float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1));
        q = mod(q, vec3(period.x, period.x, period.y));
        values[i] = hash(q + seed * vec3(17.0, 31.0, 43.0));
    }
    return mix(mix(mix(values[0], values[1], f.x), mix(values[2], values[3], f.x), f.y),
               mix(mix(values[4], values[5], f.x), mix(values[6], values[7], f.x), f.y), f.z);
}
float cloudBillows(vec3 p, float seed) {
    vec3 cell = floor(p), f = fract(p);
    float nearest = 3.0;
    for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        vec3 offset = vec3(float(x), float(y), float(z)), q = cell + offset;
        q = mod(q, 16.0);
        q += seed * vec3(17.0, 31.0, 43.0);
        vec3 center = vec3(hash(q), hash(q+7.0), hash(q+13.0));
        vec3 delta = offset + center - f;
        nearest = min(nearest, dot(delta, delta));
    }
    return 1.0-smoothstep(0.20, 0.85, sqrt(nearest));
}
vec4 cloudField() {
    float size = ODISkyClouds.z;
    vec2 tile = floor(gl_FragCoord.xy / size);
    vec2 uv = (mod(gl_FragCoord.xy, size) - 0.5) / (size - 1.0);
    float height = (tile.x + tile.y * 4.0) / 15.0;
    vec3 p = vec3(uv.x * 8.0, height * 2.0, uv.y * 8.0);
    float seed = ODISkyClouds.x, fine = 0.0, weight = 0.0, coarse = 0.0;
    int octaves = int(ODISkyClouds.y) + 3;
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        float scale = exp2(float(i)), amplitude = 1.0 / scale;
        fine += cloudNoise(p * scale, vec2(8.0 * scale), seed) * amplitude;
        weight += amplitude;
        if (i == 1) coarse = fine / weight;
    }
    fine /= weight;
    float coverage = cloudNoise(vec3(uv.x*4.0, 0.7, uv.y*4.0), vec2(4.0), seed+47.0);
    // Broad coverage grows coherent cloud masses; 3D erosion rounds their edges.
    coarse = coverage;
    fine = coverage + (fine-0.5)*0.22 - (1.0-cloudBillows(p*2.0, seed))*0.055;
    // The upper layer has long fibres, separate from the rounded lower field.
    vec3 wisps = vec3(uv.x * 8.0, 0.7, uv.y * 64.0);
    wisps.x += cloudNoise(vec3(uv.x*4.0, 0.3, uv.y*4.0), vec2(4.0), seed+71.0)*1.2;
    wisps.z += cloudNoise(vec3(uv.x*8.0, 0.4, uv.y*8.0), vec2(8.0), seed+83.0)*8.0;
    float cirrusCoarse = cloudNoise(wisps, vec2(8.0, 64.0), seed + 97.0);
    float cirrus = cirrusCoarse * 0.65
                 + cloudNoise(wisps * 2.0, vec2(16.0, 128.0), seed + 97.0) * 0.25
                 + cloudNoise(wisps * 4.0, vec2(32.0, 256.0), seed + 97.0) * 0.10;
    float cirrusCoverage = cloudNoise(vec3(uv.x*4.0,1.4,uv.y*4.0),vec2(4.0),seed+97.0);
    cirrus = mix(cirrusCoverage,cirrus,0.55);
    // A fixed, periodic fine volume shares the atlas but never follows the animated seed.
    vec3 detailPoint = vec3(uv.x*8.0, height*8.0, uv.y*8.0);
    float micro = cloudBillows(detailPoint*2.0, 193.0) * 0.75
                + cloudNoise(detailPoint*2.0, vec2(16.0), 211.0) * 0.25;
    return vec4(coarse, fine, cirrus, micro);
}
vec4 cloudSlice(sampler2D field, vec2 uv, float slice) {
    float size = ODISkyClouds.w;
    vec2 tile = vec2(mod(slice, 4.0), floor(slice / 4.0));
    return texture(field, (tile * size + fract(uv) * (size - 1.0) + 0.5) / (size * 4.0));
}
vec4 cloudMap(vec3 p) {
    float slice = clamp((p.y - cloudBase) / (cloudTop-cloudBase), 0.0, 1.0) * 15.0;
    float low = floor(slice), high = min(low + 1.0, 15.0);
    vec2 uv = p.xz / 16.0;
    vec4 a = mix(cloudSlice(ODISkyCloudA, uv, low), cloudSlice(ODISkyCloudA, uv, high), fract(slice));
    vec4 b = mix(cloudSlice(ODISkyCloudB, uv, low), cloudSlice(ODISkyCloudB, uv, high), fract(slice));
    return mix(a, b, ODISkyClouds.y);
}
float cloudMicro(vec3 p) {
    // Tile the small fixed volume independently in all axes. Its pattern stays
    // still while the broad coverage evolves, including across seed boundaries.
    float slice = fract(p.y*1.3) * 15.0;
    return mix(cloudSlice(ODISkyCloudA, p.xz*0.6, floor(slice)).a,
               cloudSlice(ODISkyCloudA, p.xz*0.6, min(floor(slice)+1.0, 15.0)).a, fract(slice));
}
vec2 cloudSample(vec3 p, float distant, bool fineDetail) {
    if (p.y <= cloudBase || p.y >= cloudTop) return vec2(0.0, 0.5);
    vec4 field = cloudMap(p);
    float n = mix(field.g, field.r, distant);
    float height = (p.y-cloudBase) / (cloudTop-cloudBase);
    // Denser banks grow taller towers instead of sharing one flat upper cap.
    float cap = mix(0.35, 1.0, smoothstep(0.46, 0.70, field.r));
    float shape = smoothstep(0.0, 0.08, height) * (1.0-smoothstep(cap*0.65, cap, height));
    float threshold = mix(0.49, 0.38, min(ODISkyFragment.z * 2.0, 1.0));
    float density = max(n-threshold-(1.0-shape)*0.18, 0.0);
    float micro = 0.5;
    if (fineDetail && density > 0.0 && distant < 1.0) {
        micro = cloudMicro(p);
        float strength = mix(0.025, 0.065, clamp((ODISkyClouds.w-64.0)/192.0, 0.0, 1.0));
        density = max(density-(1.0-micro)*strength*(1.0-distant), 0.0);
    }
    return vec2(density*8.0*shape, micro);
}
float cloudDensity(vec3 p, float distant) {
    return cloudSample(p, distant, false).x;
}
vec3 cloudAmbient(vec3 sun) {
    float daylight = smoothstep(-0.10, 0.25, sun.y);
    float twilight = smoothstep(-0.28, -0.06, sun.y) * (1.0-daylight);
    return mix(vec3(0.00018, 0.00028, 0.00048), vec3(0.19, 0.23, 0.29), daylight)
         + vec3(0.012, 0.009, 0.017) * twilight;
}
vec3 cloudSunlight(vec3 sun) {
    return mix(vec3(1.0, 0.27, 0.09), vec3(1.0, 0.94, 0.84), smoothstep(0.0, 0.45, sun.y))
         * smoothstep(-0.08, 0.06, sun.y);
}
vec4 clouds(vec3 ray, vec3 sun) {
    if (ray.y <= 0.035) return vec4(0.0);
    float distant = smoothstep(5.0, 13.0, 1.2 / ray.y);
    vec3 ambient = cloudAmbient(sun), sunlight = cloudSunlight(sun);
    vec3 moon = -sun;
    float moonlight = smoothstep(0.0, 0.3, moon.y) * (1.0-smoothstep(-0.12, 0.08, sun.y));
    // Thin cirrus at a higher altitude, with a softly filtered distant field.
    vec3 highPoint = ray * (6.2 / ray.y);
    vec4 wisps = cloudMap(vec3(highPoint.x * 0.35, cloudTop, highPoint.z * 1.6));
    float thin = pow(smoothstep(0.43, 0.76, wisps.b),1.6) * 0.35;
    thin *= 1.0-smoothstep(18.0, 32.0, length(highPoint.xz));
    vec3 thinColor = ambient + sunlight * 0.48 + vec3(0.00045, 0.00065, 0.001) * moonlight;
    vec4 upper = vec4(thinColor * thin, thin);
    float start = cloudBase / ray.y, end = min(cloudTop / ray.y, 24.0);
    if (start >= end) return upper;
    int count = max(8, int(mix(ODISkyClouds.z, ODISkyClouds.z * 0.5, distant)));
    float stepSize = (end-start) / float(count);
    // Fixed per-pixel offsets break up horizontal march bands without animating noise.
    float jitter = (hash(vec3(floor(gl_FragCoord.xy), 53.0+ODISkyCloudResolve.w*7.0))-0.5)*0.9;
    vec4 accumulated = vec4(0.0);
    float mu = dot(ray, sun);
    // Forward/backward scattering lobes brighten the edges looking toward the sun.
    float phase = 0.35 + 0.22 * 0.6156 / pow(max(1.3844-1.24*mu, 0.02), 1.5)
                       + 0.10 * 0.96 / pow(max(1.04+0.4*mu, 0.02), 1.5);
    for (int i = 0; i < 64; ++i) {
        if (i >= count || accumulated.a > 0.985) break;
        vec3 p = ray * (start + (float(i)+0.5+jitter)*stepSize);
        vec2 cloud = cloudSample(p, distant, true);
        float density = cloud.x;
        if (density <= 0.001) continue;
        float optical = 0.0;
        if (sun.y > -0.08) {
            // Three bounded sunlight samples shade the side facing away from the sun.
            for (int j = 0; j < 3; ++j)
                optical += cloudDensity(p + sun * (0.18 + float(j)*0.50), distant) * 0.50;
        }
        // Lower-extinction terms approximate light scattered back into shadowed interiors.
        float scattered = exp(-optical*2.5) + 0.30*exp(-optical*0.8) + 0.08*exp(-optical*0.25);
        float powder = mix(0.75, 1.15, 1.0-exp(-density*3.0));
        vec3 illumination = ambient * mix(0.48, 1.0, smoothstep(cloudBase, cloudTop, p.y))
                          + sunlight * scattered * phase * powder
                          + vec3(0.00045, 0.00065, 0.001) * moonlight;
        float opacity = (1.0-exp(-density*stepSize*2.0)) * (1.0-smoothstep(16.0, 24.0, length(p.xz)));
        accumulated.rgb += (1.0-accumulated.a) * opacity * illumination;
        accumulated.a += (1.0-accumulated.a) * opacity;
    }
    vec4 result = accumulated + upper * (1.0-accumulated.a);
    float thunder = max(ODISkyFragment.z*2.0-1.0, 0.0);
    float day = smoothstep(-0.12, 0.08, sun.y);
    result.rgb *= (1.0-thunder*0.65) * (1.0-smoothstep(0.20, 0.30, ODISkyFragment.z)*(1.0-day));
    result *= smoothstep(0.035, 0.09, ray.y);
    return result;
}
vec4 resolveClouds(vec3 ray) {
    vec2 uv = gl_FragCoord.xy / ODISkyViewport.zw;
    vec2 currentUV = uv-ODISkyCloudResolve.xy;
    ivec2 size = textureSize(ODISkyCloudA, 0);
    vec2 pixel = currentUV*vec2(size)-0.5;
    ivec2 cell = ivec2(floor(pixel));
    vec2 fraction = fract(pixel);
    // A compact reconstruction kernel preserves edge contrast on the first frame.
    fraction = fraction*fraction*(3.0-2.0*fraction);
    vec4 a = texelFetch(ODISkyCloudA, clamp(cell, ivec2(0), size-1), 0);
    vec4 b = texelFetch(ODISkyCloudA, clamp(cell+ivec2(1,0), ivec2(0), size-1), 0);
    vec4 c = texelFetch(ODISkyCloudA, clamp(cell+ivec2(0,1), ivec2(0), size-1), 0);
    vec4 d = texelFetch(ODISkyCloudA, clamp(cell+ivec2(1,1), ivec2(0), size-1), 0);
    vec4 current = mix(mix(a,b,fraction.x), mix(c,d,fraction.x), fraction.y);
    if (ODISkyCloudResolve.z <= 0.0) return current;
    vec4 previous = ODISkyCloudPrevious * vec4(ray, 0.0);
    if (previous.w <= 0.00001) return current;
    vec2 previousUV = previous.xy/previous.w*0.5+0.5;
    if (any(lessThan(previousUV, vec2(0.0))) || any(greaterThan(previousUV, vec2(1.0)))) return current;
    vec4 history = texture(ODISkyCloudB, previousUV);
    vec4 low = min(min(a,b),min(c,d)), high = max(max(a,b),max(c,d));
    history = clamp(history, low, high);
    float rejection = 1.0-smoothstep(0.08,0.35,abs(history.a-current.a));
    int divisor = int(ODISkyClouds.x), phase = int(ODISkyCloudResolve.w);
    ivec2 offset = ivec2(phase%divisor, (phase/divisor)%divisor);
    bool fresh = all(equal(ivec2(gl_FragCoord.xy)%divisor, offset));
    bool exactGrid = all(equal(ivec2(ODISkyViewport.zw), size*divisor));
    // Keep the reprojected high-resolution samples between checkerboard updates.
    if (exactGrid && !fresh && rejection > 0.95) return history;
    return mix(current, history, min(ODISkyCloudResolve.z,0.65)*rejection);
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
    // Keep clear night atmosphere near black without dimming celestial details.
    float twilightBrightness = smoothstep(-0.35, -0.08, sun.y);
    float nightBrightness = mix(0.002, 1.0, twilightBrightness);
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
    // Stars brighten as the twilight atmosphere fades, and reverse at dawn.
    float starVisibility = 1.0 - twilightBrightness;
    if (starVisibility > 0.0 && ray.y > 0.0 && ODISkyFragment.z < 0.20) {
        // Sample the rotating field about the same Z axis as the sun/moon.
        // The angle wraps during daylight, when the stars are invisible.
        float starAngle = rotation * 0.5;
        float c = cos(starAngle), s = sin(starAngle);
        vec3 starRay = vec3(c * ray.x - s * ray.y, s * ray.x + c * ray.y, ray.z);
        vec3 cell = floor(starRay * 450.0);
        float star = hash(cell);
        // Keep 70% of the original 0.8% star-cell density.
        if (star >= 0.9944) {
            float starShape = pow(max(1.0 - length(fract(starRay * 450.0) - 0.5) * 2.0, 0.0), 5.0);
            color += mix(vec3(0.65, 0.78, 1.0), vec3(1.0, 0.82, 0.62), hash(cell + 7.0))
                     * starShape * 2.5 * starVisibility * smoothstep(0.0, 0.18, ray.y);
        }
    }
    // Apply overcast after the cached clear atmosphere and celestial details.
    // Weather changes need no lookup refresh and affect every rendering path.
    float rain = min(ODISkyFragment.z * 2.0, 1.0);
    float thunder = max(ODISkyFragment.z * 2.0 - 1.0, 0.0);
    vec3 overcast = mix(vec3(0.003, 0.004, 0.007), vec3(0.20, 0.22, 0.25), day);
    overcast *= mix(0.65, 1.0, 1.0 - max(ray.y, 0.0));
    color = mix(color, overcast, rain * 0.95) * (1.0 - thunder * 0.65);
    // Fade nights to black between weather 20 and 30; storms stay black.
    color *= 1.0 - smoothstep(0.20, 0.30, ODISkyFragment.z) * (1.0 - day);
    if (ODISkyClouds.x > 0.5) {
        vec4 layer = texture(ODISkyCloudLayer, (gl_FragCoord.xy - ODISkyViewport.xy) / ODISkyViewport.zw);
        color = layer.rgb + color * (1.0-layer.a);
    }
    return pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
}
void main() {
#if defined(ODI_CLOUD_RESOLVE_PASS)
    bgfx_FragData0 = resolveClouds(normalize(v_color0.xyz));
#elif defined(ODI_CLOUD_FIELD_PASS)
    bgfx_FragData0 = cloudField();
#elif defined(ODI_CLOUD_PASS)
    float rotation = ODISkyFragment.y * 2.0 * PI;
    bgfx_FragData0 = clouds(normalize(v_color0.xyz), vec3(-sin(rotation), cos(rotation), 0.0));
#elif defined(ODI_LUT_PASS)
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
struct ProgramUniforms {
    GLuint program; Kind kind; GLint vertex, fragment, matrix, quality, viewport, atmosphere;
    GLint clouds = -1, cloudA = -1, cloudB = -1, cloudLayer = -1;
    GLint cloudResolve = -1, cloudPrevious = -1;
};
struct AtmosphereTarget { GLuint texture, framebuffer; int width, height; };
struct CloudTargets {
    ProgramUniforms fieldProgram, layerProgram, resolveProgram;
    AtmosphereTarget fields[2], layer, history[2];
    GLfloat previousMatrix[16]{}, angle = 0, weather = 0;
    long long lastFrame = 0;
    unsigned int phase = 0, age = 0, historyIndex = 0;
    int samples = 0, resolution = -1;
    bool historyValid = false, resolveFailed = false;
    long long epoch = 0, period = -1;
    int detail = 0, tileSize = 0;
    bool failed = false;
};
struct ContextTargets {
    EGLContext context;
    ProgramUniforms lookupProgram, halfProgram;
    GLuint vao;
    AtmosphereTarget lookup, half;
    float angle;
    bool reduced, lookupValid, failed;
    GLint textureUnit;
    CloudTargets clouds;
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
    auto weather = environment_weather();
    GLfloat fragment[] = {enabled ? 1.0f : 0.0f, angle,
                          weather.enabled ? (weather.rain + weather.thunder) * 0.5f : 0.0f,
                          client_environment_vanilla_celestials() ? 1.0f : 0.0f};
    uniform4(program.vertex, 1, vertex);
    uniform4(program.fragment, 1, fragment);
    const GLfloat quality[] = {skyReducedSamplesEnabled ? 1.0f : 0.0f, 0, 0, 0};
    uniform4(program.quality, 1, quality);
    const GLfloat noClouds[] = {0, 0, 0, 0};
    uniform4(program.clouds, 1, noClouds);
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
        cached.clouds = getUniformLocation(program, "ODISkyClouds");
        cached.cloudA = getUniformLocation(program, "ODISkyCloudA");
        cached.cloudB = getUniformLocation(program, "ODISkyCloudB");
        cached.cloudLayer = getUniformLocation(program, "ODISkyCloudLayer");
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
ProgramUniforms atmosphereProgram(bool lookup, int cloudPass = 0) {
    GLuint vertex = apiCreateShader(GL_VERTEX_SHADER), fragment = apiCreateShader(GL_FRAGMENT_SHADER);
    const char* vertexSource = lookup || cloudPass == 1 ? lookupVertexShader : skyVertexShader;
    // Compile the same atmosphere implementation for both offscreen passes.
    const char* define = cloudPass == 1 ? "#define ODI_CLOUD_FIELD_PASS\n"
                      : cloudPass == 2 ? "#define ODI_CLOUD_PASS\n"
                      : cloudPass == 3 ? "#define ODI_CLOUD_RESOLVE_PASS\n"
                      : lookup ? "#define ODI_LUT_PASS\n" : "#define ODI_ATMOSPHERE_PASS\n";
    const char* sources[] = {"#version 310 es\n", define,
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
            getUniformLocation(program, "ODISkyAtmosphere"),
            getUniformLocation(program, "ODISkyClouds"), getUniformLocation(program, "ODISkyCloudA"),
            getUniformLocation(program, "ODISkyCloudB"), getUniformLocation(program, "ODISkyCloudLayer"),
            getUniformLocation(program, "ODISkyCloudResolve"), getUniformLocation(program, "ODISkyCloudPrevious")};
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
bool optimizationFallback, cloudFallback;
bool prepareClouds(ContextTargets& t, const GLfloat* matrix, const GLint* viewport,
                   TextureBinding& fieldA, TextureBinding& fieldB, TextureBinding& layerBinding) {
    auto& c = t.clouds;
    if (c.failed || t.textureUnit < 3) return false;
    fieldA.save(t.textureUnit - 2); fieldB.save(t.textureUnit - 3); layerBinding.save(t.textureUnit - 1);
    if (!c.fieldProgram.program) c.fieldProgram = atmosphereProgram(false, 1);
    if (!c.layerProgram.program) c.layerProgram = atmosphereProgram(false, 2);
    if (!c.fieldProgram.program || !c.layerProgram.program) { c.failed = true; return false; }
    const auto frame = fps_limiter_frame_timestamp_ns(); // Reuse the frame clock, no extra clock syscall.
    if (c.period < 0 || (frame > 0 && (!c.epoch || frame < c.epoch))) {
        c.epoch = frame > 0 ? frame : 0; c.period = -1;
    }
    const auto elapsed = frame > c.epoch ? frame - c.epoch : 0;
    const auto period = elapsed / 40000000000ll;
    int detail = client_environment_cloud_detail();
    bool rebuild = c.detail != detail || c.period < 0 || period < c.period || period > c.period + 1;
    int first = 2;
    if (rebuild) { first = 0; c.detail = detail; c.tileSize = 32 << detail; }
    else if (period != c.period) {
        // The old target becomes the next seed; the fully blended target stays current.
        auto old = c.fields[0]; c.fields[0] = c.fields[1]; c.fields[1] = old;
        first = 1;
    }
    // Generate at most one GPU atlas every forty seconds after initial allocation.
    for (int i = first; i < 2; ++i) {
        apiActiveTexture(GL_TEXTURE0 + fieldA.unit);
        if (!resizeTarget(c.fields[i], c.tileSize * 4, c.tileSize * 4)) { c.failed = true; return false; }
        apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, c.fields[i].framebuffer);
        apiViewport(0, 0, c.fields[i].width, c.fields[i].height);
        apiUseProgram(c.fieldProgram.program);
        const GLfloat generation[] = {static_cast<float>((period + i) % 4096), static_cast<float>(detail),
                                      static_cast<float>(c.tileSize), 0};
        uniform4(c.fieldProgram.clouds, 1, generation);
        drawArrays(GL_TRIANGLES, 0, 3);
    }
    c.period = period;
    apiActiveTexture(GL_TEXTURE0 + layerBinding.unit);
    int resolution = client_environment_cloud_resolution(), samples = client_environment_cloud_samples();
    int divisor = 1 << (2-resolution);
    if (!resizeTarget(c.layer, (viewport[2]+divisor-1)/divisor, (viewport[3]+divisor-1)/divisor)) {
        c.failed = true; return false;
    }
    bool temporal = skyCloudTemporalEnabled && divisor > 1 && !c.resolveFailed;
    if (temporal && !c.resolveProgram.program) {
        c.resolveProgram = atmosphereProgram(false, 3);
        if (!c.resolveProgram.program) { c.resolveFailed = true; temporal = false; }
    }
    auto weatherState = environment_weather();
    float weather = weatherState.enabled ? (weatherState.rain+weatherState.thunder)*0.5f : 0.0f;
    float angle = environment_sky_angle();
    float angleDelta = angle > c.angle ? angle-c.angle : c.angle-angle;
    if (angleDelta > 0.5f) angleDelta = 1.0f-angleDelta;
    bool history = temporal && c.historyValid && !rebuild && c.samples == samples && c.resolution == resolution
        && c.history[c.historyIndex].width == viewport[2] && c.history[c.historyIndex].height == viewport[3]
        && frame >= c.lastFrame && frame-c.lastFrame < 250000000ll && angleDelta < 1.0f/1024.0f
        && weather-c.weather < 0.02f && c.weather-weather < 0.02f;
    // Row lengths isolate projection scale from camera rotation; FOV jumps reset history.
    if (history) for (int row = 0; row < 2; ++row) {
        float oldScale = 0, newScale = 0;
        for (int col = 0; col < 3; ++col) {
            oldScale += c.previousMatrix[col*4+row]*c.previousMatrix[col*4+row];
            newScale += matrix[col*4+row]*matrix[col*4+row];
        }
        if (newScale < oldScale*0.96f || newScale > oldScale*1.04f) history = false;
    }
    if (!history) { c.age = 0; c.phase = 0; }
    unsigned int phase = c.phase % static_cast<unsigned int>(divisor*divisor);
    float offsetX = temporal ? ((float(phase%divisor)+0.5f)/divisor-0.5f)/c.layer.width : 0.0f;
    float offsetY = temporal ? ((float(phase/divisor)+0.5f)/divisor-0.5f)/c.layer.height : 0.0f;
    apiActiveTexture(GL_TEXTURE0 + fieldA.unit); apiBindTexture(GL_TEXTURE_2D, c.fields[0].texture);
    apiActiveTexture(GL_TEXTURE0 + fieldB.unit); apiBindTexture(GL_TEXTURE_2D, c.fields[1].texture);
    apiBindFramebuffer(GL_DRAW_FRAMEBUFFER, c.layer.framebuffer);
    apiViewport(0, 0, c.layer.width, c.layer.height);
    apiUseProgram(c.layerProgram.program); parameters(c.layerProgram, true);
    const GLfloat vertex[] = {1, offsetX*2.0f, offsetY*2.0f, 0}; uniform4(c.layerProgram.vertex, 1, vertex);
    const GLfloat sampling[] = {0, 0, 0, temporal ? float(phase) : 0}; uniform4(c.layerProgram.cloudResolve,1,sampling);
    apiUniformMatrix4fv(c.layerProgram.matrix, 1, GL_FALSE, matrix);
    float blend = static_cast<float>(elapsed % 40000000000ll) / 40000000000.0f;
    blend = blend*blend*(3.0f-2.0f*blend);
    const GLfloat cloud[] = {1, blend, static_cast<float>(samples), static_cast<float>(c.tileSize)};
    uniform4(c.layerProgram.clouds, 1, cloud);
    apiUniform1i(c.layerProgram.cloudA, fieldA.unit); apiUniform1i(c.layerProgram.cloudB, fieldB.unit);
    drawArrays(GL_TRIANGLES, 0, 3);
    c.historyValid = false;
    if (temporal) {
        auto next = 1-c.historyIndex;
        apiActiveTexture(GL_TEXTURE0+layerBinding.unit);
        if (!resizeTarget(c.history[next],viewport[2],viewport[3])) { c.resolveFailed = true; return true; }
        apiActiveTexture(GL_TEXTURE0+fieldA.unit); apiBindTexture(GL_TEXTURE_2D,c.layer.texture);
        apiActiveTexture(GL_TEXTURE0+fieldB.unit);
        apiBindTexture(GL_TEXTURE_2D,history ? c.history[c.historyIndex].texture : c.layer.texture);
        apiBindFramebuffer(GL_DRAW_FRAMEBUFFER,c.history[next].framebuffer); apiViewport(0,0,viewport[2],viewport[3]);
        apiUseProgram(c.resolveProgram.program); parameters(c.resolveProgram,true);
        apiUniformMatrix4fv(c.resolveProgram.matrix,1,GL_FALSE,matrix);
        apiUniformMatrix4fv(c.resolveProgram.cloudPrevious,1,GL_FALSE,history ? c.previousMatrix : matrix);
        const GLfloat bounds[] = {0,0,float(viewport[2]),float(viewport[3])};
        uniform4(c.resolveProgram.viewport,1,bounds);
        const GLfloat resolve[] = {offsetX,offsetY,history ? float(c.age)/float(c.age+1) : 0,float(phase)};
        uniform4(c.resolveProgram.cloudResolve,1,resolve);
        const GLfloat grid[] = {float(divisor),0,0,0}; uniform4(c.resolveProgram.clouds,1,grid);
        apiUniform1i(c.resolveProgram.cloudA,fieldA.unit); apiUniform1i(c.resolveProgram.cloudB,fieldB.unit);
        drawArrays(GL_TRIANGLES,0,3);
        c.historyIndex = next; c.historyValid = true;
        if (c.age < 16) ++c.age;
        ++c.phase;
    }
    for (int i = 0; i < 16; ++i) c.previousMatrix[i] = matrix[i];
    c.lastFrame = frame; c.angle = angle; c.weather = weather; c.samples = samples; c.resolution = resolution;
    return true;
}
void prepareAtmosphere(ThreadState& s, const GLfloat* matrix, TextureBinding& binding,
                       TextureBinding& fieldA, TextureBinding& fieldB, TextureBinding& layerBinding) {
    bool lookup = skyLookupEnabled, half = skyHalfResolutionEnabled;
    bool cloudEnabled = client_environment_clouds();
    __atomic_store_n(&cloudFallback, false, __ATOMIC_RELEASE);
    if (!lookup && !half && !cloudEnabled) {
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
        __atomic_store_n(&cloudFallback, cloudEnabled, __ATOMIC_RELEASE);
        __atomic_store_n(&optimizationFallback, true, __ATOMIC_RELEASE); return;
    }
    auto& t = *targets;
    if (!cloudEnabled) { t.clouds.historyValid = false; t.clouds.age = 0; }
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
    bool cloudReady = false;
    // An unchanged lookup needs no offscreen draw or framebuffer/state switches.
    if (half || refresh || cloudEnabled) {
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
            int divisor = client_environment_sky_quarter_resolution() ? 4 : 2;
            if (!t.halfProgram.program || !resizeTarget(t.half, (viewport[2] + divisor - 1) / divisor,
                                                       (viewport[3] + divisor - 1) / divisor)) t.failed = true;
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
        if (cloudEnabled && !t.failed) cloudReady = prepareClouds(t, matrix, viewport, fieldA, fieldB, layerBinding);
    }
    __atomic_store_n(&cloudFallback, cloudEnabled && !cloudReady, __ATOMIC_RELEASE);
    __atomic_store_n(&optimizationFallback, t.failed, __ATOMIC_RELEASE);
    if (t.failed) return;
    apiActiveTexture(GL_TEXTURE0 + binding.unit);
    apiBindTexture(GL_TEXTURE_2D, texture);
    const GLfloat quality[] = {reduced ? 1.0f : 0.0f, static_cast<float>(source), 0, 0};
    const GLfloat bounds[] = {static_cast<float>(viewport[0]), static_cast<float>(viewport[1]),
                              static_cast<float>(viewport[2]), static_cast<float>(viewport[3])};
    uniform4(s.current.quality, 1, quality); uniform4(s.current.viewport, 1, bounds);
    apiUniform1i(s.current.atmosphere, t.textureUnit);
    if (cloudReady) {
        apiActiveTexture(GL_TEXTURE0 + layerBinding.unit);
        apiBindTexture(GL_TEXTURE_2D, t.clouds.historyValid
            ? t.clouds.history[t.clouds.historyIndex].texture : t.clouds.layer.texture);
        apiUniform1i(s.current.cloudLayer, layerBinding.unit);
        const GLfloat cloud[] = {1, 0, 0, 0}; uniform4(s.current.clouds, 1, cloud);
    }
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
    if (!enabled) {
        parameters(s->current, false); s->replacedFrame = 0;
        for (auto& target : s->targets) { target.clouds.historyValid = false; target.clouds.age = 0; }
        return false;
    }
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
    TextureBinding textureBinding, cloudA, cloudB, cloudLayer;
    prepareAtmosphere(*s, matrix, textureBinding, cloudA, cloudB, cloudLayer);
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
    if (__atomic_load_n(&cloudFallback, __ATOMIC_ACQUIRE)) return "Cloud textures unavailable; rendering sky without clouds";
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
