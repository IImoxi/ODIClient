#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <initializer_list>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "../projection_jitter.cpp"

static bool active = true;
static int sampleMode;
int client_jitter_sample_mode() { return sampleMode; }
bool client_jitter_active() { return active; }
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }

static GLuint shader(GLenum type, const char* source) {
    auto s = glCreateShader(type); glShaderSource(s, 1, &source, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); assert(ok); return s;
}
static GLuint program(const char* uniform) {
    char source[512];
    std::snprintf(source, sizeof(source), R"(#version 300 es
uniform mat4 %s;
void main() {
    vec2 p[3] = vec2[3](vec2(-0.73,-0.61),vec2(0.82,-0.42),vec2(-0.17,0.88));
    gl_Position = %s * vec4(p[gl_VertexID], -1.0, 1.0);
})", uniform, uniform);
    auto v = shader(GL_VERTEX_SHADER, source);
    auto f = shader(GL_FRAGMENT_SHADER, "#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(1);}");
    auto p = glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); assert(ok);
    glDeleteShader(v); glDeleteShader(f); return p;
}
int main() {
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    assert(platform); auto display = platform(0x31dd, EGL_DEFAULT_DISPLAY, nullptr);
    assert(eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count; assert(eglChooseConfig(display,attrs,&config,1,&count) && count);
    const EGLint surfaceAttrs[] = {EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    auto surface = eglCreatePbufferSurface(display,config,surfaceAttrs);
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    auto context = eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    assert(eglMakeCurrent(display,surface,surface,context));
    glViewport(0,0,64,64);
    getInteger = glGetIntegerv; getLocation = glGetUniformLocation;

    // Execute the installed GOT wrapper, including unsupported-signature fallback.
    assert(hooks::initialize());
    auto size = minecraft_build::current::buildNote + 4096;
    auto image = static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image != MAP_FAILED); auto base = reinterpret_cast<unsigned long>(image);
    const unsigned int note[] = {4,20,3,0x00554e47};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,minecraft_build::current::buildId,20);
    std::memcpy(image+profile::plt,profile::pltSignature,sizeof(profile::pltSignature));
    *reinterpret_cast<Upload*>(image+profile::slot) = glUniformMatrix4fv;
    image[profile::plt] ^= 1;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    assert(!install(base));
    assert(*reinterpret_cast<Upload*>(image+profile::slot)==glUniformMatrix4fv);
    assert(mprotect(image,size,PROT_READ|PROT_WRITE)==0);
    image[profile::plt] ^= 1;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    assert(install(base));
    auto hooked = *reinterpret_cast<Upload*>(image+profile::slot);
    float projection[16] = {1,0,0,0, 0,1,0,0, 0,0,-1,-1, 0,0,-0.2f,0};
    float unchanged[16]; std::memcpy(unchanged,projection,sizeof(projection));
    unsigned char samples[8][64*64*4];
    for (const char* name : {"u_proj", "u_viewProj", "u_modelViewProj"}) {
        auto p = program(name); glUseProgram(p); auto loc = glGetUniformLocation(p,name);
        for (sampleMode = 0; sampleMode < 3; ++sampleMode) {
            const int sampleCount = 2 << sampleMode;
            float cycle[8][2], totalX = 0, totalY = 0;
            for (int phase = 0; phase < sampleCount; ++phase) {
                projection_jitter_frame(64,64); hooked(loc,1,GL_FALSE,projection);
                float shifted[16]; glGetUniformfv(p,loc,shifted);
                cycle[phase][0] = shifted[8]; cycle[phase][1] = shifted[9];
                totalX += shifted[8]; totalY += shifted[9];
                assert(shifted[8] > -1.f/64 && shifted[8] < 1.f/64);
                assert(shifted[9] > -1.f/64 && shifted[9] < 1.f/64);
                if (sampleMode == 0) {
                    float expected = (frame & 1) ? -0.5f/64 : 0.5f/64;
                    assert(shifted[8] == expected && shifted[9] == expected);
                }
                assert(shifted[10] == projection[10] && shifted[11] == projection[11]);
                assert(std::memcmp(projection,unchanged,sizeof(projection)) == 0);
                // Repeated uploads in the same frame use the same sample.
                hooked(loc,1,GL_FALSE,projection);
                float repeated[16]; glGetUniformfv(p,loc,repeated);
                assert(std::memcmp(shifted,repeated,sizeof(shifted)) == 0);
                glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES,0,3);
                glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,samples[phase]);
            }
            assert(totalX == 0 && totalY == 0);
            for (int a = 0; a < sampleCount; ++a) for (int b = 0; b < a; ++b) {
                assert(cycle[a][0] != cycle[b][0] || cycle[a][1] != cycle[b][1]);
                assert(std::memcmp(samples[a],samples[b],sizeof(samples[a])) != 0);
            }
            projection_jitter_frame(64,64); hooked(loc,1,GL_FALSE,projection);
            float next[16]; glGetUniformfv(p,loc,next);
            assert(next[8] == cycle[0][0] && next[9] == cycle[0][1]);
            // Changing the setting cannot change uploads within the current frame.
            int saved = sampleMode; sampleMode = (sampleMode + 1) % 3;
            hooked(loc,1,GL_FALSE,projection); float same[16]; glGetUniformfv(p,loc,same);
            assert(std::memcmp(next,same,sizeof(next)) == 0); sampleMode = saved;
        }
        sampleMode = 0;
        auto forwarded = [&] {
            hooked(loc,1,GL_FALSE,projection); float received[16]; glGetUniformfv(p,loc,received);
            assert(std::memcmp(received,projection,sizeof(projection))==0);
        };
        active = false; forwarded(); active = true;
        // HUD/orthographic projection stays byte-identical.
        projection[11]=0; projection[15]=1; forwarded(); projection[11]=-1; projection[15]=0;
        glViewport(0,0,32,32); forwarded(); glViewport(0,0,64,64);
        GLuint fbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        forwarded(); glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fbo);
        projection_jitter_frame(0,0); forwarded(); projection_jitter_frame(64,64);
        glDeleteProgram(p);
    }
    // An unrelated perspective-valued uniform must pass through too.
    auto p = program("otherMatrix"); glUseProgram(p); auto loc = glGetUniformLocation(p,"otherMatrix");
    hooked(loc,1,GL_FALSE,projection); float received[16]; glGetUniformfv(p,loc,received);
    assert(std::memcmp(received,projection,sizeof(projection))==0);
    glDeleteProgram(p); assert(glGetError()==GL_NO_ERROR);
    assert(munmap(image,size)==0);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
    puts("Projection jitter: exact-build hook, 2/4/8 centered GLES sample cycles, original/HUD matrices and pass exclusions passed");
}
