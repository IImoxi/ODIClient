// Real software-rendered GLES tests, without opening a desktop window.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <dlfcn.h>
#include "../motion_blur.h"
#include "../panel_renderer.h"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }

static GLint integer(GLenum name) { GLint value; glGetIntegerv(name, &value); return value; }
static void clear(float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
static void pixel(int r, int g, int b) {
    assert(!motion_blur_error());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    unsigned char result[4];
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result);
    assert(result[0] >= r - 2 && result[0] <= r + 2);
    assert(result[1] >= g - 2 && result[1] <= g + 2);
    assert(result[2] >= b - 2 && result[2] <= b + 2);
    assert(result[3] == 255);
    assert(glGetError() == GL_NO_ERROR);
}
static GLuint shader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    assert(ok);
    return s;
}

int main() {
    motion_blur_render(true, 0.5f, 1); // No current context is harmless.
    assert(!motion_blur_error());
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, configAttributes, &config, 1, &count) && count == 1);
    EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    EGLint surfaceAttributes[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
    assert(eglMakeCurrent(display, surface, surface, context));
    std::printf("GLES test renderer: %s\n", glGetString(GL_RENDERER));
    clear(1, 0, 0);
    pixel(255, 0, 0);

    // Deliberately hostile state, including sampler and pixel-unpack bindings.
    GLuint textures[2], sampler, buffer, vao, fbos[2];
    glGenTextures(2, textures); glGenSamplers(1, &sampler);
    glGenBuffers(1, &buffer); glGenVertexArrays(1, &vao); glGenFramebuffers(2, fbos);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, textures[0]); glBindSampler(0, sampler);
    glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_2D, textures[1]); glBindSampler(14, sampler);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, textures[1]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer); glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, nullptr, GL_STATIC_DRAW);
    GLuint v = shader(GL_VERTEX_SHADER, "#version 300 es\nvoid main(){gl_Position=vec4(0);}");
    GLuint f = shader(GL_FRAGMENT_SHADER, "#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(1);}");
    GLuint program = glCreateProgram(); glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
    GLint linked; glGetProgramiv(program, GL_LINK_STATUS, &linked); assert(linked);
    glUseProgram(program); glBindVertexArray(vao);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[0]); glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[1]);
    const GLenum caps[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_RASTERIZER_DISCARD};
    for (GLenum cap : caps) glEnable(cap);
    glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
    glBlendFuncSeparate(GL_DST_COLOR, GL_SRC_COLOR, GL_DST_ALPHA, GL_SRC_ALPHA);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE); glViewport(2, 3, 4, 5);
    motion_blur_render(true, 0.5f, 1, true);
    if (motion_blur_error()) std::puts(motion_blur_error());
    assert(!motion_blur_error());
    assert(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE3 && integer(GL_TEXTURE_BINDING_2D) == static_cast<GLint>(textures[1]));
    assert(integer(GL_CURRENT_PROGRAM) == static_cast<GLint>(program) && integer(GL_VERTEX_ARRAY_BINDING) == static_cast<GLint>(vao));
    assert(integer(GL_DRAW_FRAMEBUFFER_BINDING) == static_cast<GLint>(fbos[0]));
    assert(integer(GL_READ_FRAMEBUFFER_BINDING) == static_cast<GLint>(fbos[1]));
    assert(integer(GL_PIXEL_UNPACK_BUFFER_BINDING) == static_cast<GLint>(buffer));
    glActiveTexture(GL_TEXTURE14);
    assert(integer(GL_TEXTURE_BINDING_2D) == static_cast<GLint>(textures[1]) && integer(GL_SAMPLER_BINDING) == static_cast<GLint>(sampler));
    glActiveTexture(GL_TEXTURE0);
    assert(integer(GL_TEXTURE_BINDING_2D) == static_cast<GLint>(textures[0]) && integer(GL_SAMPLER_BINDING) == static_cast<GLint>(sampler));
    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    assert(viewport[0] == 2 && viewport[1] == 3 && viewport[2] == 4 && viewport[3] == 5);
    GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    assert(!mask[0] && mask[1] && !mask[2] && mask[3]);
    for (GLenum cap : caps) assert(glIsEnabled(cap));
    assert(integer(GL_BLEND_EQUATION_RGB) == GL_FUNC_SUBTRACT && integer(GL_BLEND_EQUATION_ALPHA) == GL_FUNC_REVERSE_SUBTRACT);
    assert(integer(GL_BLEND_SRC_RGB) == GL_DST_COLOR && integer(GL_BLEND_DST_RGB) == GL_SRC_COLOR);
    assert(integer(GL_BLEND_SRC_ALPHA) == GL_DST_ALPHA && integer(GL_BLEND_DST_ALPHA) == GL_SRC_ALPHA);
    pixel(255, 0, 0); // First frame seeds history without changing it.

    clear(0, 1, 0); motion_blur_render(true, 0.5f, 1); pixel(128, 128, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 1); pixel(64, 64, 128);
    motion_blur_render(false, 0.5f, 1);
    clear(0, 1, 0); motion_blur_render(true, 0.5f, 1); pixel(0, 255, 0);
    clear(1, 0, 0); motion_blur_render(true, 0.0f, 1); pixel(255, 0, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 1); pixel(0, 0, 255);

    clear(1, 0, 0); motion_blur_render(true, 0.5f, 2); pixel(255, 0, 0);
    clear(0, 1, 0); motion_blur_render(true, 0.5f, 2); pixel(128, 128, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 2); pixel(0, 128, 128);

    motion_blur_render(false, 0.5f, 3); // Start this averaging scenario with fresh history.
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 3); pixel(255, 0, 0);
    clear(0, 1, 0); motion_blur_render(true, 0.5f, 3); pixel(128, 128, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 3); pixel(85, 85, 85);

    motion_blur_render(false, 0.5f, 4);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 4); pixel(255, 0, 0);
    clear(0, 1, 0); motion_blur_render(true, 0.5f, 4); pixel(128, 128, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 4); pixel(85, 85, 85);
    clear(1, 1, 0); motion_blur_render(true, 0.5f, 4); pixel(128, 128, 64);

    motion_blur_render(false, 0.5f, 8);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(255, 0, 0);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(255, 0, 0);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(255, 0, 0);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(255, 0, 0);
    clear(0, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(204, 0, 0);
    clear(0, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(170, 0, 0);
    clear(0, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(146, 0, 0);
    clear(0, 0, 0); motion_blur_render(true, 0.5f, 8); pixel(128, 0, 0);

    // Full 16-frame history, rollover, and clamping above the cap.
    for (float frames : {16.0f, 32.0f}) {
        motion_blur_render(false, 0.5f, frames);
        for (int i = 0; i < 16; ++i) {
            clear(i < 8 ? 1 : 0, 0, 0);
            motion_blur_render(true, 0.5f, frames);
        }
        pixel(128, 0, 0);
        clear(0, 0, 0); motion_blur_render(true, 0.5f, frames); pixel(112, 0, 0);
    }
    // 480 FPS / 30 Hz needs the same 16 frames, using timestamps.
    motion_blur_render(false, 0.5f, 1);
    for (int i = 0; i < 16; ++i) {
        clear(i < 8 ? 1 : 0, 0, 0);
        motion_blur_render(true, 0.0f, 1, false, 30, (i + 1) * 2083333LL);
    }
    pixel(128, 0, 0);
    clear(0, 0, 0);
    motion_blur_render(true, 0.0f, 1, false, 30, 17 * 2083333LL);
    pixel(112, 0, 0);

    // Spatial output must not overwrite the raw averaging history.
    motion_blur_render(false, 0.0f, 3);
    clear(1, 0, 0); motion_blur_render(true, 0.5f, 3, true); pixel(255, 0, 0);
    clear(0, 1, 0); motion_blur_render(true, 0.5f, 3, true); pixel(128, 128, 0);
    clear(0, 0, 1); motion_blur_render(true, 0.5f, 3, true); pixel(85, 85, 85);
    clear(0, 0, 0); motion_blur_render(true, 0.5f, 3); pixel(0, 85, 85);

    // Whole-screen blur must also reset clipping left by a rounded menu border.
    PanelPaint outline;
    outline.tint = {1, 0, 0};
    outline.cornerRadius = 3;
    outline.opacity = 0.5f;
    outline.borderWidth = 1;
    assert(draw_gl_panel(0, 0, 8, 8, outline));
    // Spatial blur works without motion history or a nonzero strength.
    clear(0, 0, 0);
    glEnable(GL_SCISSOR_TEST); glScissor(4, 0, 4, 8);
    glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    motion_blur_render(false, 0.0f, 1, false);
    pixel(0, 0, 0);
    motion_blur_render(false, 0.0f, 1, true);
    unsigned char softened[4];
    glReadPixels(3, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, softened);
    assert(softened[0] > 40 && softened[0] < 128);
    assert(softened[0] == softened[1] && softened[1] == softened[2]);
    assert(softened[3] == 255 && glGetError() == GL_NO_ERROR);

    // A one-pixel checkerboard must average to gray instead of surviving between sparse taps.
    clear(0, 0, 0);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(1, 1, 1, 1);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        if ((x + y) % 2) continue;
        glScissor(x, y, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    PanelPaint frosted;
    frosted.tintAmount = 0.0f;
    frosted.blurScale = 1.0f;
    assert(draw_gl_panel(0, 0, 8, 8, frosted));
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, softened);
        assert(softened[0] >= 126 && softened[0] <= 130);
    }
    assert(glGetError() == GL_NO_ERROR);

    // Cached blur samples the game only at 30 Hz, independently of motion history.
    frosted.blurTimestampNs = 1000000000LL;
    clear(1, 0, 0);
    assert(draw_gl_panel(0, 0, 8, 8, frosted)); pixel(255, 0, 0);
    clear(0, 0, 1);
    frosted.blurTimestampNs += 16000000LL;
    assert(draw_gl_panel(0, 0, 8, 8, frosted)); pixel(255, 0, 0);
    clear(0, 1, 0); motion_blur_render(false, 0, 1, true);
    assert(draw_gl_panel(0, 0, 8, 8, frosted)); pixel(255, 0, 0);
    clear(0, 0, 1);
    frosted.blurTimestampNs += 18000000LL;
    assert(draw_gl_panel(0, 0, 8, 8, frosted)); pixel(0, 0, 255);
    assert(glGetError() == GL_NO_ERROR);

    surfaceAttributes[1] = 12; surfaceAttributes[3] = 12;
    EGLSurface resized = eglCreatePbufferSurface(display, config, surfaceAttributes);
    assert(eglMakeCurrent(display, resized, resized, context));
    clear(1, 1, 0); motion_blur_render(true, 0.5f, 1); pixel(255, 255, 0);
    assert(draw_gl_panel(0, 0, 12, 12, frosted)); pixel(255, 255, 0);
    motion_blur_render(false, 0.0f, 1, true); pixel(255, 255, 0);
    EGLContext replacement = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    assert(eglMakeCurrent(display, resized, resized, replacement));
    clear(1, 0, 1); motion_blur_render(true, 0.5f, 1); pixel(255, 0, 255);
    assert(draw_gl_panel(0, 0, 12, 12, frosted)); pixel(255, 0, 255);
    motion_blur_render(false, 0.0f, 1, true); pixel(255, 0, 255);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    eglDestroyContext(display, replacement); eglDestroyContext(display, context);
    eglDestroySurface(display, resized); eglDestroySurface(display, surface); eglTerminate(display);
    std::puts("PASS: real GLES trail and equal 2/3/4/8/16-frame blending and timestamped 480 FPS averaging, spatial screen blur, GL state restoration, reset, resize, and context replacement");
}
