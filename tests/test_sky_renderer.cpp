#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define ODI_SKY_TEST_OPTIONS
#include "../sky_renderer.cpp"

static bool enabledValue = true;
static float angleValue;
static long long frameValue = 100;
static GpuShaderSourceTransform sourceCallback;
static GpuShaderSourceMatched matchedCallback;
static GpuShaderCompileObserver compileCallback;
static GpuProgramBound bindCallback;
static RenderGlDrawObserver drawCallback;
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
bool client_environment_enabled() { return enabledValue; }
bool client_environment_sky() { return enabledValue; }
bool client_environment_vanilla_celestials() { return false; }
const char* environment_error() { return nullptr; }
float environment_sky_angle() { return angleValue; }
long long fps_limiter_frame_timestamp_ns() { return frameValue; }
bool chat_print_local(const char*) { return true; }
bool gpu_shader_services_set_shader_source_transform(GpuShaderSourceTransform callback) { sourceCallback = callback; return true; }
bool gpu_shader_services_set_shader_diagnostics(GpuShaderSourceMatched matched, GpuShaderCompileObserver compiled) {
    matchedCallback = matched; compileCallback = compiled; return true;
}
bool gpu_shader_services_set_program_observer(GpuProgramBound bound, GpuProgramInvalidated) { bindCallback = bound; return true; }
bool render_gl_trace_set_draw_observer(RenderGlDrawObserver callback) { drawCallback = callback; return true; }
static int width = 96, height = 64;
// Matching declarations/call site from the supported standard and instanced interfaces.
static const char* vertexParts[] = {
    "#version 310 es\nuniform mat4 u_model[4];\nuniform vec4 SkyColor;\nuniform vec4 FogColor;\n",
    "in vec4 a_color0;\nin vec3 a_position;\nvoid main(){ v_color0 = mix(SkyColor, FogColor, vec4(a_color0.x)); }"
};
static const char* originalFragment = "#version 310 es\nin highp vec4 v_color0;\nvoid main(){ bgfx_FragData0 = vec4(v_color0.xyz, v_color0.w); }";
static GLuint shader(GLenum type, const char* text) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &text, nullptr);
    glCompileShader(id);
    GLint ok = 0; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(id, sizeof(log), nullptr, log); std::fprintf(stderr, "%s\n", log); }
    assert(ok); return id;
}
static GLuint program(const char* vertex, const char* fragment) {
    GLuint v = shader(GL_VERTEX_SHADER, vertex), f = shader(GL_FRAGMENT_SHADER, fragment);
    if (vertex == skyVertexShader || vertex == skyInstancedVertexShader) { matchedCallback(v, vertex); compileCallback(v); }
    if (fragment == skyFragmentShader) { matchedCallback(f, fragment); compileCallback(f); }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); std::fprintf(stderr, "%s\n", log); }
    assert(ok); glDeleteShader(v); glDeleteShader(f); return p;
}
static void matrix(float* result, float rotation = 0, float translation = 0) {
    // Column-major perspective * camera-yaw view, with translated camera.
    const float n = 0.1f, f = 100.0f, sx = 0.8f, sy = 1.2f;
    const float p[16] = {sx,0,0,0, 0,sy,0,0, 0,0,-(f+n)/(f-n),-1, 0,0,-2*f*n/(f-n),0};
    float c = std::cos(rotation), s = std::sin(rotation);
    const float v[16] = {c,0,s,0, 0,1,0,0, -s,0,c,0, -translation,0,0,1};
    for (int col=0; col<4; ++col) for (int row=0; row<4; ++row) {
        result[col*4+row] = 0;
        for (int k=0; k<4; ++k) result[col*4+row] += p[k*4+row]*v[col*4+k];
    }
}
static void bind(GLuint p, const float* camera) {
    glUseProgram(p); bindCallback(p);
    GLint location = glGetUniformLocation(p, "u_viewProj");
    if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, camera);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    location = glGetUniformLocation(p, "u_model[0]");
    if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, identity);
    const float color[] = {0.2f,0.3f,0.4f,1};
    location = glGetUniformLocation(p, "SkyColor"); if (location>=0) glUniform4fv(location,1,color);
    location = glGetUniformLocation(p, "FogColor"); if (location>=0) glUniform4fv(location,1,color);
    // Tiny geometry and constant native fog weight must not affect custom coverage.
    GLint position = glGetAttribLocation(p, "a_position");
    if (position >= 0) { glDisableVertexAttribArray(position); glVertexAttrib3f(position,0,0,-1); }
    GLint colorAttribute = glGetAttribLocation(p, "a_color0");
    if (colorAttribute >= 0) { glDisableVertexAttribArray(colorAttribute); glVertexAttrib4f(colorAttribute,1,1,1,1); }
}
static std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(width*height*4);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    assert(glGetError()==GL_NO_ERROR); return result;
}
static std::vector<unsigned char> render(GLuint p, const float* camera, float angle) {
    angleValue = angle; ++frameValue;
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearDepthf(1);
    glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    bind(p,camera);
    glEnable(GL_CULL_FACE); glEnable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER); glDepthMask(GL_TRUE);
    assert(drawCallback());
    assert(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_BLEND) && !glIsEnabled(GL_DEPTH_TEST));
    GLint function; GLboolean write;
    glGetIntegerv(GL_DEPTH_FUNC,&function); glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    assert(function==GL_GREATER && write);
    float marker[4]; glGetUniformfv(p,glGetUniformLocation(p,"ODISkyVertex"),marker); assert(marker[0]==0);
    glGetUniformfv(p,glGetUniformLocation(p,"ODISkyFragment"),marker); assert(marker[0]==0);
    return pixels();
}
static double difference(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    double result=0;
    for (unsigned int i=0;i<a.size();++i) if (i%4!=3) result+=std::abs(int(a[i])-int(b[i]));
    return result/(width*height*3);
}
static double average(const std::vector<unsigned char>& a) {
    double result=0; for (unsigned int i=0;i<a.size();++i) if (i%4!=3) result+=a[i];
    return result/(width*height*3);
}
static void ppm(const char* path, const std::vector<unsigned char>& a) {
    FILE* file=std::fopen(path,"wb"); assert(file); std::fprintf(file,"P6\n%d %d\n255\n",width,height);
    for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x) std::fwrite(&a[(y*width+x)*4],1,3,file);
    std::fclose(file);
}
int main(int argc, char** argv) {
    assert(skyLookupEnabled && skyHalfResolutionEnabled && skyReducedSamplesEnabled);
    skyLookupEnabled = skyHalfResolutionEnabled = skyReducedSamplesEnabled = false;
    if (argc > 1 && std::strcmp(argv[1], "--preview") == 0) { width = 768; height = 512; }
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    assert(platform); EGLDisplay display=platform(0x31dd,EGL_DEFAULT_DISPLAY,nullptr);
    assert(eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
    EGLConfig config; EGLint count; assert(eglChooseConfig(display,configAttrs,&config,1,&count) && count);
    const EGLint surfaceAttrs[]={EGL_WIDTH,width,EGL_HEIGHT,height,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    assert(eglMakeCurrent(display,surface,surface,context));
    glViewport(0,0,width,height); GLuint vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    sky_renderer_init(); assert(!sky_renderer_error());
    int length=0; const int partLengths[]={int(std::strlen(vertexParts[0])),int(std::strlen(vertexParts[1]))};
    const char* standard=sourceCallback(2,vertexParts,partLengths,&length); assert(standard==skyVertexShader && length>0);
    const char* instancedParts[]={vertexParts[0],"in vec4 i_data1;",vertexParts[1]};
    const char* instanced=sourceCallback(3,instancedParts,nullptr,&length); assert(instanced==skyInstancedVertexShader);
    const char* fragment=sourceCallback(1,&originalFragment,nullptr,&length); assert(fragment==skyFragmentShader);
    const char* unrelated="#version 310 es\nvoid main(){}"; assert(!sourceCallback(1,&unrelated,nullptr,&length));
    GLuint standardProgram=program(standard,fragment), instancedProgram=program(instanced,fragment);
    float camera[16]; matrix(camera);
    auto day=render(standardProgram,camera,0), instancedDay=render(instancedProgram,camera,0);
    auto night=render(standardProgram,camera,0.5f);
    int minValue=255,maxValue=0,uncovered=0;
    for (int i=0;i<width*height;++i) { for(int c=0;c<3;++c) { int v=day[i*4+c]; if(v<minValue) minValue=v; if(v>maxValue) maxValue=v; }
        if(day[i*4]==255 && day[i*4+1]==0 && day[i*4+2]==255) ++uncovered; }
    assert(maxValue-minValue>40 && !uncovered);
    assert(difference(day,instancedDay)<2 && difference(day,night)>15 && average(day)>average(night));
    matrix(camera,0.8f); auto rotated=render(standardProgram,camera,0.20f);
    matrix(camera); auto unrotated=render(standardProgram,camera,0.20f); assert(difference(rotated,unrotated)>2);
    matrix(camera,0,20); auto translated=render(standardProgram,camera,0.20f);
    assert(difference(translated,unrotated)<2); // Infinite sky direction is translation invariant.
    float infiniteCamera[16]; matrix(infiniteCamera);
    infiniteCamera[10] = -1.0f; infiniteCamera[14] = -0.2f;
    auto infiniteDay=render(standardProgram,infiniteCamera,0);
    assert(difference(day,infiniteDay)<2); // Homogeneous far point has w=0.
    ppm("build/sky-day.ppm",day); ppm("build/sky-night.ppm",night);
    // Every optimization and combination uses the cloudless reference path.
    // Keep a hostile sampler/texture and restrictive state on the borrowed unit:
    // offscreen passes must restore all of it before the native renderer resumes.
    GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    GLuint borrowedTexture, borrowedSampler;
    glGenTextures(1, &borrowedTexture); glGenSamplers(1, &borrowedSampler);
    glActiveTexture(GL_TEXTURE0 + units - 1); glBindTexture(GL_TEXTURE_2D, borrowedTexture);
    glSamplerParameteri(borrowedSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glBindSampler(units - 1, borrowedSampler); glActiveTexture(GL_TEXTURE0);
    for (float angle : {0.0f, 0.20f, 0.25f, 0.30f, 0.5f, 0.75f}) {
        matrix(camera, 0.8f);
        auto reference = render(standardProgram, camera, angle);
        for (int mask = 1; mask < 8; ++mask) {
            skyLookupEnabled = mask & 1; skyHalfResolutionEnabled = mask & 2; skyReducedSamplesEnabled = mask & 4;
            glEnable(GL_STENCIL_TEST); glStencilFunc(GL_ALWAYS, 0, ~0u);
            auto optimized = render(standardProgram, camera, angle);
            assert(!optimizationFallback && glIsEnabled(GL_STENCIL_TEST));
            GLint value, viewport[4], framebuffer;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &value); assert(value == GL_TEXTURE0);
            glActiveTexture(GL_TEXTURE0 + units - 1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &value); assert(value == GLint(borrowedTexture));
            glGetIntegerv(GL_SAMPLER_BINDING, &value); assert(value == GLint(borrowedSampler));
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_VIEWPORT, viewport); assert(viewport[0] == 0 && viewport[1] == 0 && viewport[2] == width && viewport[3] == height);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer); assert(framebuffer == 0);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &value); assert(value == GLint(vao));
            glGetIntegerv(GL_CURRENT_PROGRAM, &value); assert(value == GLint(standardProgram));
            double error = difference(reference, optimized);
            std::printf("Sky option mask %d, angle %.2f: mean RGB difference %.3f/255\n", mask, angle, error);
            assert(error < 3.0);
            assert(difference(optimized, render(instancedProgram, camera, angle)) < 2.0);
            if (argc > 1 && angle == 0.20f) {
                char path[64]; std::snprintf(path, sizeof(path), "build/sky-option-%d.ppm", mask); ppm(path, optimized);
            }
        }
        skyLookupEnabled = skyHalfResolutionEnabled = skyReducedSamplesEnabled = false;
        assert(difference(reference, render(standardProgram, camera, angle)) == 0);
    }
    // Lookup reuse, sample/time invalidation and fallback all remain reversible.
    skyLookupEnabled = true; matrix(camera);
    auto cached = render(standardProgram, camera, 0.2f);
    auto thread = state(false); assert(thread);
    ContextTargets* targets = nullptr;
    for (auto& target : thread->targets) if (target.context == context) targets = &target;
    assert(targets && targets->lookupValid && !targets->failed);
    assert(difference(cached, render(standardProgram, camera, 0.2f)) == 0);
    float cachedAngle = targets->angle;
    render(standardProgram, camera, 0.2f + 1.0f / 8192.0f);
    assert(targets->angle == cachedAngle); // Small time changes reuse GPU storage.
    render(standardProgram, camera, 0.21f);
    assert(targets->angle == 0.21f); // Larger changes refresh it.
    skyReducedSamplesEnabled = true;
    render(standardProgram, camera, 0.21f);
    assert(targets->reduced);
    skyReducedSamplesEnabled = false;
    render(standardProgram, camera, 0.99995f);
    float rolloverAngle = targets->angle;
    render(standardProgram, camera, 0.00005f);
    assert(targets->angle == rolloverAngle); // Midnight rollover is a small delta.
    targets->failed = true;
    auto fallback = render(standardProgram, camera, 0.2f); assert(optimizationFallback);
    skyLookupEnabled = false;
    assert(difference(fallback, render(standardProgram, camera, 0.2f)) == 0);
    targets->failed = false;
    // Offset/odd viewport, clipping and channel masks survive the half pass.
    skyHalfResolutionEnabled = true; bind(standardProgram, camera);
    glViewport(3, 5, width-7, height-11); glEnable(GL_SCISSOR_TEST); glScissor(4, 6, width-9, height-13);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE); assert(drawCallback());
    GLint offset[4]; GLboolean channels[4]; glGetIntegerv(GL_VIEWPORT, offset); glGetBooleanv(GL_COLOR_WRITEMASK, channels);
    assert(offset[0] == 3 && offset[1] == 5 && offset[2] == width-7 && offset[3] == height-11);
    assert(!channels[0] && channels[1] && !channels[2] && channels[3] && glIsEnabled(GL_SCISSOR_TEST));
    glViewport(0, 0, width, height); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glActiveTexture(GL_TEXTURE0 + units - 1); glBindTexture(GL_TEXTURE_2D, 0); glBindSampler(units - 1, 0);
    glActiveTexture(GL_TEXTURE0); glDeleteTextures(1, &borrowedTexture); glDeleteSamplers(1, &borrowedSampler);
    skyHalfResolutionEnabled = false; skyLookupEnabled = true;
    glEnable(GL_SAMPLE_MASK); glSampleMaski(0, 0);
    render(standardProgram, camera, 0.21f); assert(glIsEnabled(GL_SAMPLE_MASK));
    glSampleMaski(0, ~0u); glDisable(GL_SAMPLE_MASK);
    auto unmasked = render(standardProgram, camera, 0.21f);
    skyLookupEnabled = false;
    assert(difference(unmasked, render(standardProgram, camera, 0.21f)) < 0.2);
    skyLookupEnabled = skyHalfResolutionEnabled = skyReducedSamplesEnabled = false;
    // A foreground depth region already present must survive a sky drawn at far depth.
    for (int mask = 0; mask < 8; ++mask) {
        skyLookupEnabled = mask & 1; skyHalfResolutionEnabled = mask & 2; skyReducedSamplesEnabled = mask & 4;
        matrix(camera); bind(standardProgram,camera);
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearDepthf(1); glClearColor(1,0,1,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glEnable(GL_SCISSOR_TEST); glScissor(32,20,32,24);
        glClearDepthf(0.2f); glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        assert(drawCallback()); auto foreground=pixels(); auto center=(32*width+48)*4;
        assert(foreground[center]==255 && foreground[center+1]==0 && foreground[center+2]==0);
    }
    skyLookupEnabled = skyHalfResolutionEnabled = skyReducedSamplesEnabled = false;
    // Shared Clouds fragment must link with a normal native vertex and retain its colors.
    const char* cloudVertex=R"GLSL(#version 310 es
uniform vec4 CloudColor;
out highp vec4 v_color0;
void main(){ vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);gl_Position=vec4(p,0,1);v_color0=CloudColor; }
)GLSL";
    GLuint cloudProgram=program(cloudVertex,fragment); glUseProgram(cloudProgram); bindCallback(cloudProgram);
    assert(drawCallback()); // Suppressed only after the sky replacement this frame.
    ++frameValue; assert(!drawCallback());
    const float cloudColor[]={0.25f,0.5f,0.75f,1}; glUniform4fv(glGetUniformLocation(cloudProgram,"CloudColor"),1,cloudColor);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDrawArrays(GL_TRIANGLES,0,3);
    auto cloud=pixels(); assert(std::abs(int(cloud[0])-64)<=1 && std::abs(int(cloud[1])-128)<=1 && std::abs(int(cloud[2])-191)<=1);
    for (const char* name : {"SunMoonColor","StarsColor"}) {
        char source[512]; std::snprintf(source,sizeof(source),"#version 310 es\nuniform vec4 %s;\nout highp vec4 v_color0;\nvoid main(){gl_Position=vec4(0);v_color0=%s;}\n",name,name);
        GLuint celestial=program(source,fragment); bind(standardProgram,camera); assert(drawCallback());
        glUseProgram(celestial);bindCallback(celestial);assert(drawCallback());++frameValue;assert(!drawCallback());glDeleteProgram(celestial);
    }
    // Active feedback cannot consume the draw; paused feedback permits normal rendering.
    const char* feedbackVarying = "v_worldPos";
    glTransformFeedbackVaryings(standardProgram,1,&feedbackVarying,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(standardProgram); GLint linked; glGetProgramiv(standardProgram,GL_LINK_STATUS,&linked); assert(linked);
    invalidated(standardProgram); bind(standardProgram,camera);
    GLuint feedbackBuffer; glGenBuffers(1,&feedbackBuffer); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedbackBuffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,4096,nullptr,GL_DYNAMIC_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedbackBuffer);
    glBeginTransformFeedback(GL_TRIANGLES); assert(glGetError()==GL_NO_ERROR);
    assert(!drawCallback()); glPauseTransformFeedback(); assert(drawCallback());
    glResumeTransformFeedback(); glEndTransformFeedback();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0); glDeleteBuffers(1,&feedbackBuffer);
    bind(standardProgram,camera); enabledValue=false; assert(!drawCallback());
    // Native tiny degenerate geometry does not cover a cleared framebuffer while disabled.
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
    auto disabled=pixels();assert(disabled[0]==255 && disabled[1]==0 && disabled[2]==255);
    enabledValue=true;float zero[16]{};bind(standardProgram,zero);assert(!drawCallback());
    zero[0]=NAN;bind(standardProgram,zero);assert(!drawCallback());
    assert(glGetError()==GL_NO_ERROR);
    std::printf("Custom sky GLES pixels passed: range %d..%d, day %.1f, night %.1f, variants diff %.3f, day/night diff %.1f, yaw diff %.1f\n",
        minValue,maxValue,average(day),average(night),difference(day,instancedDay),difference(day,night),difference(rotated,unrotated));
    glDeleteProgram(standardProgram);glDeleteProgram(instancedProgram);glDeleteProgram(cloudProgram);glDeleteVertexArrays(1,&vao);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
