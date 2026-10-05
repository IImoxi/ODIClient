// Real GLES regression check for the K menu's shared text/icon renderer.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include "custom_font.cpp"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* lib, const char* name) { return dlsym(lib, name); }

constexpr int width = 320, height = 160;
static unsigned char reference[width * height * 4], actual[sizeof(reference)];
static void reset() {
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_RASTERIZER_DISCARD); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
}
static unsigned char face[16 * 16 * 4];
static unsigned char playerSkin[64*64*4];
static unsigned long previewSerial;
static void draw(int icon) {
    if (icon==3) { assert(custom_font_draw_skin(playerSkin,64,64,++previewSerial,160,10,128,width,height)); return; }
    if (icon==4) { assert(custom_font_draw_ring(160,80,24,0.75f,width,height)); return; }
    if (icon == 2) { assert(custom_font_draw_head(face, 160, 30, 64, width, height)); return; }
    if (icon) assert(custom_font_draw_icon("assets/icon-zoom.png", 160, 30, 64, 1, 1, 1, width, height));
    else {
        assert(custom_font_draw("Motion Blur", 160, 10, 24, width, height));
        assert(custom_font_draw_left("FPS-based averaging ON", 10, 60, 20, width, height));
        assert(custom_font_draw_left("Blur strength (%): 30", 10, 110, 20, width, height));
    }
}
static void checkUpload(GLuint texture, const unsigned char* expected, int w, int h) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    static unsigned char pixels[atlasWidth * atlasHeight * 4];
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int i = 0; i < w * h; ++i) assert(pixels[i * 4] == expected[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo);
}
int main() {
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttrs);
    EGLint surfaceAttrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    assert(eglMakeCurrent(display, surface, surface, context));
    std::printf("Font test renderer: %s\n", glGetString(GL_RENDERER));
    // First use must upload correct pixels and restore the game's unpack state.
    GLuint unpackBuffer;
    glGenBuffers(1, &unpackBuffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, nullptr, GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8); glPixelStorei(GL_UNPACK_ROW_LENGTH, 544);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4); glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    assert(custom_font_text_width("Motion Blur", 24) > 0);
    checkUpload(atlasTexture, atlasPixels, atlasWidth, atlasHeight);
    assert(activeFontPixels == 24);
    GLuint font24 = atlasTexture;
    int width24 = custom_font_text_width("Motion Blur", 24);
    assert(custom_font_text_width("Motion Blur", 20) > 0);
    assert(activeFontPixels == 20 && atlasTexture != font24);
    assert(custom_font_text_width("Motion Blur", 24) == width24 && atlasTexture == font24);
    custom_font_set_raster_scale(0.75f);
    assert(custom_font_text_width("Motion Blur", 18) > 0);
    assert(activeFontPixels == 24 && atlasTexture == font24);
    custom_font_set_raster_scale(1.0f);
    // Fractional animation sizes must change geometry while retaining the exact atlas.
    assert(custom_font_draw_left_scaled("Wg", 10, 10, 24, 0.751f, width, height));
    assert(activeFontPixels == 24 && atlasTexture == font24);
    float animatedWidth = vertices[1].x - vertices[0].x;
    assert(custom_font_draw_left_scaled("Wg", 10, 10, 24, 0.752f, width, height));
    assert(activeFontPixels == 24 && atlasTexture == font24);
    assert(vertices[1].x - vertices[0].x > animatedWidth);
    assert((vertices[1].x - vertices[0].x) - animatedWidth < 0.1f);
    for (int size = 1; size <= 60; ++size) {
        assert(custom_font_text_width("Wg", size) > 0);
        assert(activeFontPixels == size);
    }
    assert(custom_font_draw_left("Wg", 10, 10, 20, width, height));
    for (int i = 0; i < 12; ++i) {
        assert(vertices[i].x == static_cast<int>(vertices[i].x));
        assert(vertices[i].y == static_cast<int>(vertices[i].y));
    }
    assert(!ftLoadChar(ftFace, 'W', FT_LOAD_RENDER));
    assert(glyphs['W' - firstChar].width == static_cast<int>(ftFace->glyph->bitmap.width));
    assert(glyphs['W' - firstChar].advance == (ftFace->glyph->advance.x >> 6));
    int cachedCount = 0;
    for (const FontAtlas& cached : fontAtlases) if (cached.texture) ++cachedCount;
    assert(cachedCount == fontCacheCapacity);
    const GLenum caps[] = {GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_RASTERIZER_DISCARD};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        int i = (y * 16 + x) * 4;
        face[i] = x < 8 ? 255 : 0; face[i + 1] = y < 8 ? 255 : 0;
        face[i + 2] = 100; face[i + 3] = 255;
    }
    for (int i=0;i<64*64;++i) {
        playerSkin[i*4]=200;playerSkin[i*4+1]=100;playerSkin[i*4+2]=60;
        playerSkin[i*4+3]=(i/64<32 && i%64<32) || (i/64>=16 && i/64<32)
            || (i/64>=48 && i%64>=16 && i%64<48) ? 255 : 0;
    }
    for (int y=8;y<16;++y) for(int x=8;x<16;++x) {
        auto* p=playerSkin+(y*64+x)*4;p[0]=255;p[1]=0;p[2]=0;
    }
    for (int y=8;y<16;++y) for(int x=40;x<48;++x) {
        auto* p=playerSkin+(y*64+x)*4;p[0]=0;p[1]=0;p[2]=255;p[3]=128;
    }
    for (int icon : {0, 1, 2, 3, 4}) {
        reset(); draw(icon);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, reference);
        int visible = 0;
        for (unsigned int i = 0; i < sizeof(reference); i += 4) if (reference[i]) ++visible;
        assert(visible > 100);
        if (icon == 1) checkUpload(iconTextures[0].texture, iconAlpha, iconWidth, iconHeight);
        if (icon == 2) {
            // Upload remains RGBA, top-left UV orientation and nearest scaling.
            int i = ((height - 40 - 1) * width + 140) * 4;
            assert(reference[i] == 255 && reference[i + 1] == 255 && reference[i + 2] == 100);
            i = ((height - 80 - 1) * width + 180) * 4;
            assert(reference[i] == 0 && reference[i + 1] == 0 && reference[i + 2] == 100);
        }
        if(icon==3) {
            int pixel=((height-30-1)*width+156)*4;
            assert(reference[pixel]>=126 && reference[pixel]<=128 && reference[pixel+2]>=127);
            GLint framebuffer,renderbuffer;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);assert(framebuffer==0);
            glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);assert(renderbuffer==0);
        }
        GLint unpack;
        const GLenum unpackNames[] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
            GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_PIXEL_UNPACK_BUFFER_BINDING};
        const GLint unpackValues[] = {8, 544, 4, 2, static_cast<GLint>(unpackBuffer)};
        for (int i = 0; i < 5; ++i) {
            glGetIntegerv(unpackNames[i], &unpack); assert(unpack == unpackValues[i]);
        }
        for (GLenum cap : caps) {
            reset(); glEnable(cap);
            glScissor(width / 2, 0, width / 2, height);
            glStencilFunc(GL_NEVER, 0, ~0u);
            draw(icon);
            assert(glIsEnabled(cap));
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual);
            assert(std::memcmp(reference, actual, sizeof(reference)) == 0);
        }
        if(icon==3 || icon==4) {
            reset();glEnable(GL_SCISSOR_TEST);glScissor(width/2,0,width/2,height);
            custom_font_set_clip(true);draw(icon);custom_font_set_clip(false);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                int i=(y*width+x)*4;
                if(x>=width/2) assert(!std::memcmp(actual+i,reference+i,4));
                else assert(actual[i]==0 && actual[i+1]==0 && actual[i+2]==0);
            }
        }
        reset(); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        draw(icon);
        GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        assert(!mask[0] && !mask[1] && !mask[2] && !mask[3]);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        assert(std::memcmp(reference, actual, sizeof(reference)) == 0);
    }
    assert(glGetError() == GL_NO_ERROR);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    std::puts("PASS: complete tile/toggle/slider text and icons under inherited clipping, discard, and color masks; atlas/icon/RGBA skin uploads, fixed 3D player/hat composition, hold ring, clipping, and GL state restored");
}
