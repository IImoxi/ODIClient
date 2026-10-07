#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cassert>
#include <dlfcn.h>
#include "../popup.h"

extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) { return dlopen(name, flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) { return dlsym(library, name); }
float custom_menu_main_button_radius(int, int) { return 4; }
static void clear() {
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
static int red(int x, int y) {
    unsigned char pixel[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0];
}
int main() {
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint count;
    assert(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
    EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    EGLint surfaceAttributes[] = {EGL_WIDTH, 960, EGL_HEIGHT, 720, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
    assert(eglMakeCurrent(display, surface, surface, context));
    glViewport(0, 0, 960, 720);
    assert(popup_show("Confirmation", "Would you like to continue?"));
    clear(); popup_render(true, 1000000000LL);
    assert(red(720, 25) == 255); // First animation frame starts to the right of the screen.
    clear(); popup_render(true, 1100000000LL);
    assert(red(720, 25) == 255 && red(850, 25) < 180); // Moves horizontally.
    clear(); popup_render(true, 1500000000LL);
    assert(red(720, 25) < 150); // Settled bottom-right panel.
    assert(red(100, 25) == 255 && red(720, 400) == 255);
    auto answerPixels = [&](int left, int right) {
        int pixels = 0;
        for (int y = 20; y <= 40; ++y)
            for (int x = left; x <= right; ++x)
                if (red(x, y) > 180) ++pixels;
        return pixels;
    };
    assert(answerPixels(755, 785) > 8 && answerPixels(875, 903) > 8);
    assert(answerPixels(812, 850) == 0); // Evenly spaced Yes/No, empty center at rest.
    // Real glyphs and progress ring, with normal input still untouched.
    popup_on_mouse_button(1, 0, true);
    clear(); popup_render(true, 1600000000LL);
    assert(answerPixels(755, 785) > 8 && answerPixels(875, 903) > 8); // Animation begins at rest.
    clear(); popup_render(true, 1690000000LL);
    assert(answerPixels(812, 840) > 8 && answerPixels(875, 903) == 0);
    clear(); popup_render(true, 2100000000LL);
    assert(red(720, 25) < 150);
    int ringPixels = 0;
    for (int y = 18; y <= 38; ++y)
        for (int x = 840; x <= 860; ++x)
            if (red(x, y) > 180) ++ringPixels;
    assert(ringPixels > 8);
    int textPixels = 0;
    for (int y = 62; y < 80; ++y)
        for (int x = 722; x < 900; ++x)
            if (red(x, y) > 180) ++textPixels;
    assert(textPixels > 10);
    assert(glGetError() == GL_NO_ERROR);
    popup_on_mouse_button(1, 1, true);
    clear(); popup_render(true, 2300000000LL);
    clear(); popup_render(true, 2500000000LL);
    assert(answerPixels(755, 785) > 8 && answerPixels(875, 903) > 8); // Release restores both labels.
    assert(answerPixels(812, 860) == 0);
    popup_on_mouse_button(2, 0, true);
    clear(); popup_render(true, 2600000000LL);
    clear(); popup_render(true, 2800000000LL);
    assert(answerPixels(812, 860) > 8 && answerPixels(755, 785) == 0);
    assert(answerPixels(875, 903) == 0); // No moves to center and Yes fades away.
    popup_on_mouse_button(2, 1, true);
    clear(); popup_render(true, 2900000000LL);
    clear(); popup_render(true, 3100000000LL);
    assert(answerPixels(755, 785) > 8 && answerPixels(875, 903) > 8);
    clear(); popup_render(true, 6000000000LL);
    assert(red(725, 12) > red(930, 12) + 10); // Faint timeout fill at the bubble bottom.
    popup_cancel();
    clear(); popup_render(true, 6100000000LL);
    clear(); popup_render(true, 6600000000LL);
    assert(red(720, 25) == 255);
    // Completed response: only the answer moves vertically and grows for 500 ms.
    assert(popup_show("Confirmation", "Would you like to continue?"));
    clear(); popup_render(true, 7000000000LL);
    clear(); popup_render(true, 7500000000LL);
    popup_on_mouse_button(1, 0, true);
    clear(); popup_render(true, 7600000000LL);
    clear(); popup_render(true, 8100000000LL);
    clear(); popup_render(true, 8600000000LL);
    int initialAnswerPixels = answerPixels(812, 850);
    assert(initialAnswerPixels > 8);
    int headerPixels = 0;
    for (int y = 62; y < 80; ++y)
        for (int x = 722; x < 900; ++x)
            if (red(x, y) > 180) ++headerPixels;
    assert(headerPixels == 0); // Header/body disappear as soon as the hold completes.
    clear(); popup_render(true, 9100000000LL);
    int centeredPixels = 0;
    for (int y = 40; y < 61; ++y)
        for (int x = 810; x < 851; ++x)
            if (red(x, y) > 180) ++centeredPixels;
    assert(centeredPixels > initialAnswerPixels); // Larger title-size glyphs at the dialog center.
    assert(answerPixels(812, 860) == 0); // Lower answer/ring area is now empty.
    assert(red(725, 12) <= red(930, 12) + 2); // Timer hidden on completion.
    clear(); popup_render(true, 9599999999LL);
    assert(red(720, 25) < 150); // Remains for another half second.
    clear(); popup_render(true, 9600000000LL);
    clear(); popup_render(true, 10100000000LL);
    assert(red(720, 25) == 255); // Normal dismissal follows the one-second result sequence.
    assert(glGetError() == GL_NO_ERROR);
    // Wrapping adds one body line while leaving the bottom answer row in place.
    assert(popup_show("Confirmation", "Would you like to continue with this longer popup message?"));
    clear(); popup_render(true, 11000000000LL);
    clear(); popup_render(true, 11500000000LL);
    assert(red(720, 95) < 150); // Extra height above the old 89px panel top.
    assert(red(720, 110) == 255);
    assert(answerPixels(755, 785) > 8 && answerPixels(875, 903) > 8);
    for (int bottom = 53; bottom <= 67; bottom += 14) {
        int bodyPixels = 0;
        for (int y = bottom; y < bottom + 11; ++y)
            for (int x = 722; x < 938; ++x)
                if (red(x, y) > 180) ++bodyPixels;
        assert(bodyPixels > 10); // Both body lines render at the normal font size.
    }
    popup_on_mouse_button(1, 0, true);
    clear(); popup_render(true, 11600000000LL);
    clear(); popup_render(true, 12600000000LL);
    assert(red(720, 95) < 150); // Completion preserves the taller panel.
    assert(glGetError() == GL_NO_ERROR);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
}
