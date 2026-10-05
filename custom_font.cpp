#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "launcher_api.h"
#include "custom_font.h"
#include "menu_style.h"

namespace {
constexpr int firstChar = 32, glyphCount = 95;
constexpr int cell = 64, columns = 8, atlasWidth = cell * columns;
constexpr int atlasHeight = cell * ((glyphCount + columns - 1) / columns);
constexpr int fontPixels = 48;
constexpr int iconWidth = 256, iconHeight = 256;
constexpr unsigned long iconRawSize = iconHeight * (iconWidth * 4 + 1);
constexpr int iconCapacity = 36, iconPathCapacity = 256;
struct Glyph { float u0, v0, u1, v1; int width, height, left, top, advance; };
struct Vertex { float x, y, u, v; };
struct IconTexture { char path[iconPathCapacity]; GLuint texture; };
Glyph glyphs[glyphCount];
unsigned char atlasPixels[atlasWidth * atlasHeight];
Vertex vertices[6 * 64];
bool initialized, available;
int ascent;
constexpr int fontCacheCapacity = 16;
struct FontAtlas { Glyph glyphs[glyphCount]; GLuint texture; int pixels, ascent; bool mojangles; unsigned long used; };
FontAtlas fontAtlases[fontCacheCapacity];
unsigned long fontUse;
int activeFontPixels = fontPixels;
float rasterScale = 1.0f;
GLuint atlasTexture, headTexture, program, vao, buffer;
GLuint skinTexture, previewTexture, previewFramebuffer, previewDepth, skinVao, ringTexture;
unsigned long previewRevision;
float drawOpacity = 1.0f;
bool drawClip;
IconTexture iconTextures[iconCapacity];
GLint screenUniform, colorUniform;
GLint samplerUniform, rgbaUniform;
void* fontLibrary;
void* zlibLibrary;
FT_Library ftLibrary;
FT_Face ftFace, interFace, mojanglesFace;
bool useMojangles;
unsigned char iconPng[32768], iconCompressed[32768], iconRaw[iconRawSize];
unsigned char iconAlpha[iconWidth * iconHeight];

decltype(&FT_Init_FreeType) ftInit;
decltype(&FT_New_Face) ftNewFace;
decltype(&FT_Set_Pixel_Sizes) ftSetPixelSizes;
decltype(&FT_Load_Char) ftLoadChar;
decltype(&FT_Done_Face) ftDoneFace;
decltype(&FT_Done_FreeType) ftDoneFreeType;
decltype(&eglGetProcAddress) getProc;
decltype(&glGetString) getString;
decltype(&glGetIntegerv) getIntegerv;
decltype(&glGetBooleanv) getBooleanv;
decltype(&glColorMask) colorMask;
decltype(&glPixelStorei) pixelStorei;
decltype(&glIsEnabled) isEnabled;
decltype(&glEnable) enable;
decltype(&glDisable) disable;
decltype(&glBlendFuncSeparate) blendFuncSeparate;
decltype(&glBlendEquationSeparate) blendEquationSeparate;
decltype(&glActiveTexture) activeTexture;
decltype(&glGenTextures) genTextures;
decltype(&glBindTexture) bindTexture;
decltype(&glTexParameteri) texParameteri;
decltype(&glTexImage2D) texImage2D;
decltype(&glTexSubImage2D) texSubImage2D;
decltype(&glCreateShader) createShader;
decltype(&glShaderSource) shaderSource;
decltype(&glCompileShader) compileShader;
decltype(&glGetShaderiv) getShaderiv;
decltype(&glDeleteShader) deleteShader;
decltype(&glCreateProgram) createProgram;
decltype(&glAttachShader) attachShader;
decltype(&glLinkProgram) linkProgram;
decltype(&glGetProgramiv) getProgramiv;
decltype(&glGetUniformLocation) getUniformLocation;
decltype(&glUseProgram) useProgram;
decltype(&glUniform2f) uniform2f;
decltype(&glUniform4f) uniform4f;
decltype(&glUniform1i) uniform1i;
decltype(&glGenVertexArrays) genVertexArrays;
decltype(&glBindVertexArray) bindVertexArray;
decltype(&glGenBuffers) genBuffers;
decltype(&glBindBuffer) bindBuffer;
decltype(&glBufferData) bufferData;
decltype(&glEnableVertexAttribArray) enableVertexAttribArray;
decltype(&glVertexAttribPointer) vertexAttribPointer;
decltype(&glDrawArrays) drawArrays;
decltype(&glBindSampler) bindSampler;
decltype(&glViewport) viewport;
decltype(&glGenFramebuffers) genFramebuffers;
decltype(&glBindFramebuffer) bindFramebuffer;
decltype(&glFramebufferTexture2D) framebufferTexture2D;
decltype(&glCheckFramebufferStatus) checkFramebufferStatus;
decltype(&glGenRenderbuffers) genRenderbuffers;
decltype(&glBindRenderbuffer) bindRenderbuffer;
decltype(&glRenderbufferStorage) renderbufferStorage;
decltype(&glFramebufferRenderbuffer) framebufferRenderbuffer;
decltype(&glDepthMask) depthMask;
decltype(&glDepthFunc) depthFunc;
decltype(&glClearBufferfv) clearBufferfv;

decltype(&fopen) hostFopen;
decltype(&fgets) hostFgets;
decltype(&fclose) hostFclose;
decltype(&fgetc) hostFgetc;
decltype(&sscanf) hostSscanf;
int (*zlibUncompress)(unsigned char*, unsigned long*, const unsigned char*, unsigned long);

bool loadHostFunctions() {
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    fontLibrary = mcpelauncher_host_dlopen("libfreetype.so.6", 2);
    zlibLibrary = mcpelauncher_host_dlopen("libz.so.1", 2);
    if (!libc || !fontLibrary || !zlibLibrary) return false;
#define LOAD_HOST(dst, lib, name) dst = reinterpret_cast<decltype(dst)>(mcpelauncher_host_dlsym(lib, name)); if (!dst) return false
    LOAD_HOST(hostFopen, libc, "fopen");
    LOAD_HOST(hostFgets, libc, "fgets");
    LOAD_HOST(hostFclose, libc, "fclose");
    LOAD_HOST(hostFgetc, libc, "fgetc");
    LOAD_HOST(hostSscanf, libc, "sscanf");
    LOAD_HOST(ftInit, fontLibrary, "FT_Init_FreeType");
    LOAD_HOST(ftNewFace, fontLibrary, "FT_New_Face");
    LOAD_HOST(ftSetPixelSizes, fontLibrary, "FT_Set_Pixel_Sizes");
    LOAD_HOST(ftLoadChar, fontLibrary, "FT_Load_Char");
    LOAD_HOST(ftDoneFace, fontLibrary, "FT_Done_Face");
    LOAD_HOST(ftDoneFreeType, fontLibrary, "FT_Done_FreeType");
    zlibUncompress = reinterpret_cast<decltype(zlibUncompress)>(
        mcpelauncher_host_dlsym(zlibLibrary, "uncompress"));
    if (!zlibUncompress) return false;
#undef LOAD_HOST
    return true;
}

bool findAssetPath(const char* suffix, char* path, unsigned long capacity) {
    FILE* maps = hostFopen("/proc/self/maps", "r");
    if (!maps) return false;
    unsigned long self = reinterpret_cast<unsigned long>(&custom_font_draw);
    char line[4096];
    bool found = false;
    while (hostFgets(line, sizeof(line), maps)) {
        unsigned long start, end;
        if (hostSscanf(line, "%lx-%lx", &start, &end) != 2 || self < start || self >= end) continue;
        char* file = line;
        while (*file && *file != '/') ++file;
        if (!*file) break;
        unsigned long length = 0;
        while (file[length] && file[length] != '\n' && file[length] != '\r') ++length;
        while (length && file[length - 1] != '/') --length;
        const volatile char* suffixChars = suffix;
        unsigned long suffixLength = 0;
        while (suffixChars[suffixLength]) ++suffixLength;
        if (!length || length + suffixLength >= capacity) break;
        for (unsigned long i = 0; i < length; ++i) path[i] = file[i];
        for (unsigned long i = 0; i < suffixLength; ++i) path[length + i] = suffix[i];
        path[length + suffixLength] = 0;
        found = true;
        break;
    }
    hostFclose(maps);
    return found;
}

bool loadGL() {
    void* egl = mcpelauncher_host_dlopen("libEGL.so.1", 2);
    if (!egl) return false;
    getProc = reinterpret_cast<decltype(getProc)>(mcpelauncher_host_dlsym(egl, "eglGetProcAddress"));
    if (!getProc) return false;
#define LOAD_GL(dst, name) dst = reinterpret_cast<decltype(dst)>(getProc("gl" #name)); if (!dst) return false
    LOAD_GL(getString, GetString); LOAD_GL(getIntegerv, GetIntegerv);
    LOAD_GL(getBooleanv, GetBooleanv); LOAD_GL(colorMask, ColorMask); LOAD_GL(pixelStorei, PixelStorei);
    LOAD_GL(isEnabled, IsEnabled); LOAD_GL(enable, Enable); LOAD_GL(disable, Disable);
    LOAD_GL(blendFuncSeparate, BlendFuncSeparate); LOAD_GL(blendEquationSeparate, BlendEquationSeparate);
    LOAD_GL(activeTexture, ActiveTexture); LOAD_GL(genTextures, GenTextures);
    LOAD_GL(bindTexture, BindTexture); LOAD_GL(texParameteri, TexParameteri); LOAD_GL(texImage2D, TexImage2D); LOAD_GL(texSubImage2D, TexSubImage2D);
    LOAD_GL(createShader, CreateShader); LOAD_GL(shaderSource, ShaderSource);
    LOAD_GL(compileShader, CompileShader); LOAD_GL(getShaderiv, GetShaderiv);
    LOAD_GL(deleteShader, DeleteShader); LOAD_GL(createProgram, CreateProgram);
    LOAD_GL(attachShader, AttachShader); LOAD_GL(linkProgram, LinkProgram);
    LOAD_GL(getProgramiv, GetProgramiv); LOAD_GL(getUniformLocation, GetUniformLocation);
    LOAD_GL(useProgram, UseProgram); LOAD_GL(uniform2f, Uniform2f); LOAD_GL(uniform4f, Uniform4f);
    LOAD_GL(uniform1i, Uniform1i); LOAD_GL(genVertexArrays, GenVertexArrays);
    LOAD_GL(bindVertexArray, BindVertexArray); LOAD_GL(genBuffers, GenBuffers);
    LOAD_GL(bindBuffer, BindBuffer); LOAD_GL(bufferData, BufferData);
    LOAD_GL(enableVertexAttribArray, EnableVertexAttribArray);
    LOAD_GL(vertexAttribPointer, VertexAttribPointer); LOAD_GL(drawArrays, DrawArrays);
    LOAD_GL(bindSampler, BindSampler); LOAD_GL(viewport, Viewport);
    LOAD_GL(genFramebuffers, GenFramebuffers); LOAD_GL(bindFramebuffer, BindFramebuffer);
    LOAD_GL(framebufferTexture2D, FramebufferTexture2D); LOAD_GL(checkFramebufferStatus, CheckFramebufferStatus);
    LOAD_GL(genRenderbuffers, GenRenderbuffers); LOAD_GL(bindRenderbuffer, BindRenderbuffer);
    LOAD_GL(renderbufferStorage, RenderbufferStorage); LOAD_GL(framebufferRenderbuffer, FramebufferRenderbuffer);
    LOAD_GL(depthMask, DepthMask); LOAD_GL(depthFunc, DepthFunc); LOAD_GL(clearBufferfv, ClearBufferfv);
#undef LOAD_GL
    return true;
}

GLuint makeShader(GLenum type, const char* version, const char* source) {
    GLuint shader = createShader(type);
    const char* parts[] = {version, source};
    shaderSource(shader, 2, parts, nullptr);
    compileShader(shader);
    GLint ok;
    getShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { deleteShader(shader); return 0; }
    return shader;
}

// CPU images are tightly packed, regardless of Minecraft's last texture upload.
struct UnpackState {
    static constexpr GLenum names[] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                                      GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS};
    GLint values[4], buffer;
    UnpackState() {
        getIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        for (int i = 0; i < 4; ++i) {
            getIntegerv(names[i], &values[i]);
            pixelStorei(names[i], i == 0 ? 1 : 0);
        }
    }
    ~UnpackState() {
        for (int i = 0; i < 4; ++i) pixelStorei(names[i], values[i]);
        bindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    }
};

bool rasterizeFont(int pixels) {
    if (ftSetPixelSizes(ftFace, 0, pixels)) return false;
    ascent = static_cast<int>(ftFace->size->metrics.ascender >> 6);
    for (int i = 0; i < atlasWidth * atlasHeight; ++i) atlasPixels[i] = 0;
    for (int i = 0; i < glyphCount; ++i) {
        FT_ULong code = static_cast<FT_ULong>(firstChar + i);
        if (ftLoadChar(ftFace, code, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot slot = ftFace->glyph;
        int x = (i % columns) * cell + 1, y = (i / columns) * cell + 1;
        int w = static_cast<int>(slot->bitmap.width), h = static_cast<int>(slot->bitmap.rows);
        if (w > cell - 2 || h > cell - 2 || slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) continue;
        for (int row = 0; row < h; ++row) {
            int sourceRow = slot->bitmap.pitch >= 0 ? row : h - row - 1;
            const unsigned char* source = slot->bitmap.buffer + sourceRow *
                (slot->bitmap.pitch >= 0 ? slot->bitmap.pitch : -slot->bitmap.pitch);
            for (int col = 0; col < w; ++col)
                atlasPixels[(y + row) * atlasWidth + x + col] = source[col];
        }
        glyphs[i] = {static_cast<float>(x) / atlasWidth, static_cast<float>(y) / atlasHeight,
                     static_cast<float>(x + w) / atlasWidth, static_cast<float>(y + h) / atlasHeight,
                     w, h, slot->bitmap_left, slot->bitmap_top,
                     static_cast<int>(slot->advance.x >> 6)};
    }
    return true;
}

// Resolve host filesystem APIs; never link glibc into the Android mod.
void loadMojangles() {
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    auto env = reinterpret_cast<decltype(&getenv)>(mcpelauncher_host_dlsym(libc, "getenv"));
    auto format = reinterpret_cast<decltype(&snprintf)>(mcpelauncher_host_dlsym(libc, "snprintf"));
    auto scan = reinterpret_cast<decltype(&glob)>(mcpelauncher_host_dlsym(libc, "glob"));
    auto release = reinterpret_cast<decltype(&globfree)>(mcpelauncher_host_dlsym(libc, "globfree"));
    auto compare = reinterpret_cast<decltype(&strverscmp)>(mcpelauncher_host_dlsym(libc, "strverscmp"));
    if (!env || !format || !scan || !release || !compare) return;
    const char* data = env("XDG_DATA_HOME");
    const char* home = env("HOME");
    char pattern[4096];
    int n = data && *data
        ? format(pattern, sizeof(pattern), "%s/mcpelauncher/versions/*/assets/assets/fonts/Mojangles.ttf", data)
        : home ? format(pattern, sizeof(pattern), "%s/.local/share/mcpelauncher/versions/*/assets/assets/fonts/Mojangles.ttf", home) : -1;
    if (n < 0 || n >= static_cast<int>(sizeof(pattern))) return;
    glob_t paths{};
    if (scan(pattern, 0, nullptr, &paths) == 0) {
        // Natural ordering keeps 1.26.10 newer than 1.26.9. Try older fonts on failure.
        for (unsigned long i = 0; i < paths.gl_pathc; ++i) {
            unsigned long latest = i;
            for (unsigned long j = i + 1; j < paths.gl_pathc; ++j)
                if (compare(paths.gl_pathv[j], paths.gl_pathv[latest]) > 0) latest = j;
            char* swap = paths.gl_pathv[i]; paths.gl_pathv[i] = paths.gl_pathv[latest]; paths.gl_pathv[latest] = swap;
            if (!ftNewFace(ftLibrary, paths.gl_pathv[i], 0, &mojanglesFace)) break;
        }
    }
    release(&paths);
}

bool initFont() {
    if (initialized) return available;
    initialized = true;
    if (!loadHostFunctions() || !loadGL()) return false;
    char path[4096];
    if (!findAssetPath("assets/inter.ttf", path, sizeof(path)) || ftInit(&ftLibrary)) return false;
    if (ftNewFace(ftLibrary, path, 0, &ftFace) || ftSetPixelSizes(ftFace, 0, fontPixels)) {
        ftDoneFreeType(ftLibrary);
        return false;
    }

    interFace = ftFace;
    loadMojangles();

    const char* glVersion = reinterpret_cast<const char*>(getString(GL_VERSION));
    if (!glVersion) return false;
    bool es = glVersion[0] == 'O' && glVersion[1] == 'p' && glVersion[2] == 'e';
    const char* version = es ? "#version 300 es\nprecision highp float;\n" : "#version 330 core\n";
    GLuint vertexShader = makeShader(GL_VERTEX_SHADER, version,
        "layout(location=0) in vec3 p;layout(location=1) in vec2 t;out vec2 uv;"
        "uniform vec2 screen;void main(){gl_Position=vec4(p.x*2.0/screen.x-1.0,"
        "1.0-p.y*2.0/screen.y,p.z,1.0);uv=t;}");
    GLuint fragmentShader = makeShader(GL_FRAGMENT_SHADER, version,
        "in vec2 uv;out vec4 color;uniform sampler2D atlas;uniform vec4 ink;"
        "uniform int rgbaImage;void main(){vec4 p=texture(atlas,uv);"
        "if(rgbaImage!=0 && p.a<0.0039)discard;"
        "color=rgbaImage==0 ? vec4(ink.rgb,ink.a*p.r) : p*ink;"
        "if(rgbaImage==2)color.rgb*=ink.a;}");
    if (!vertexShader || !fragmentShader) return false;
    program = createProgram();
    attachShader(program, vertexShader); attachShader(program, fragmentShader);
    linkProgram(program); deleteShader(vertexShader); deleteShader(fragmentShader);
    GLint linked;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    screenUniform = getUniformLocation(program, "screen");
    colorUniform = getUniformLocation(program, "ink");
    samplerUniform = getUniformLocation(program, "atlas");
    rgbaUniform = getUniformLocation(program, "rgbaImage");
    GLint oldActive, oldTexture;
    getIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    activeTexture(GL_TEXTURE0);
    getIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    UnpackState unpack;
    genTextures(1, &atlasTexture); bindTexture(GL_TEXTURE_2D, atlasTexture);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!rasterizeFont(fontPixels)) return false;
    texImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasWidth, atlasHeight, 0,
               GL_RED, GL_UNSIGNED_BYTE, atlasPixels);
    FontAtlas& initial = fontAtlases[0];
    initial.texture = atlasTexture; initial.pixels = fontPixels;
    initial.ascent = ascent; initial.used = ++fontUse;
    for (int i = 0; i < glyphCount; ++i) initial.glyphs[i] = glyphs[i];
    bindTexture(GL_TEXTURE_2D, oldTexture);
    activeTexture(oldActive);

    GLint oldVao, oldBuffer;
    getIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    getIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    genVertexArrays(1, &vao); genBuffers(1, &buffer);
    bindVertexArray(vao); bindBuffer(GL_ARRAY_BUFFER, buffer);
    enableVertexAttribArray(0); vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    enableVertexAttribArray(1); vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                                     reinterpret_cast<void*>(2 * sizeof(float)));
    bindVertexArray(oldVao); bindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    available = true;
    return true;
}

bool selectFont(int height) {
    if (!initFont()) return false;
    bool mojangles = useMojangles && mojanglesFace;
    ftFace = mojangles ? mojanglesFace : interFace;
    int pixels = static_cast<int>(height / rasterScale + 0.5f);
    if (pixels < 1) pixels = 1;
    // The fixed cells fit ordinary menu sizes through 4K; oversized text scales.
    if (pixels > cell - 4) pixels = cell - 4;
    int slot = 0;
    for (int i = 0; i < fontCacheCapacity; ++i) {
        if (fontAtlases[i].pixels == pixels && fontAtlases[i].mojangles == mojangles) { slot = i; break; }
        if (fontAtlases[i].used < fontAtlases[slot].used) slot = i;
    }
    FontAtlas& cached = fontAtlases[slot];
    if (cached.pixels != pixels || cached.mojangles != mojangles) {
        for (int i = 0; i < glyphCount; ++i) glyphs[i] = {};
        if (!rasterizeFont(pixels)) return false;
        GLint oldActive, oldTexture;
        getIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        activeTexture(GL_TEXTURE0);
        getIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
        UnpackState unpack;
        if (!cached.texture) genTextures(1, &cached.texture);
        bindTexture(GL_TEXTURE_2D, cached.texture);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasWidth, atlasHeight, 0,
                   GL_RED, GL_UNSIGNED_BYTE, atlasPixels);
        bindTexture(GL_TEXTURE_2D, oldTexture);
        activeTexture(oldActive);
        cached.pixels = pixels; cached.ascent = ascent; cached.mojangles = mojangles;
        for (int i = 0; i < glyphCount; ++i) cached.glyphs[i] = glyphs[i];
    }
    cached.used = ++fontUse;
    for (int i = 0; i < glyphCount; ++i) glyphs[i] = cached.glyphs[i];
    atlasTexture = cached.texture; ascent = cached.ascent; activeFontPixels = pixels;
    return true;
}

unsigned long readBigEndian32(const unsigned char* data) {
    return (static_cast<unsigned long>(data[0]) << 24)
        | (static_cast<unsigned long>(data[1]) << 16)
        | (static_cast<unsigned long>(data[2]) << 8) | data[3];
}

unsigned char paeth(unsigned char left, unsigned char above, unsigned char upperLeft) {
    int estimate = static_cast<int>(left) + above - upperLeft;
    int leftDistance = estimate > left ? estimate - left : left - estimate;
    int aboveDistance = estimate > above ? estimate - above : above - estimate;
    int cornerDistance = estimate > upperLeft ? estimate - upperLeft : upperLeft - estimate;
    if (leftDistance <= aboveDistance && leftDistance <= cornerDistance) return left;
    return aboveDistance <= cornerDistance ? above : upperLeft;
}

GLuint initIcon(const char* assetPath) {
    if (!assetPath || !*assetPath) return 0;
    int assetLength = 0;
    while (assetPath[assetLength] && assetLength < iconPathCapacity - 1) ++assetLength;
    if (assetPath[assetLength]) return 0;
    for (int i = 0; i < iconCapacity; ++i) {
        if (!iconTextures[i].path[0]) continue;
        int at = 0;
        while (assetPath[at] && assetPath[at] == iconTextures[i].path[at]) ++at;
        if (!assetPath[at] && !iconTextures[i].path[at]) return iconTextures[i].texture;
    }
    int iconIndex = 0;
    while (iconIndex < iconCapacity && iconTextures[iconIndex].path[0]) ++iconIndex;
    if (iconIndex == iconCapacity) return 0;
    char path[4096];
    if (!findAssetPath(assetPath, path, sizeof(path))) return 0;
    FILE* file = hostFopen(path, "rb");
    if (!file) return 0;
    unsigned long pngSize = 0;
    int next;
    while ((next = hostFgetc(file)) >= 0 && pngSize < sizeof(iconPng))
        iconPng[pngSize++] = static_cast<unsigned char>(next);
    hostFclose(file);
    if (pngSize < 8 || iconPng[0] != 137 || iconPng[1] != 'P'
        || iconPng[2] != 'N' || iconPng[3] != 'G') return 0;

    unsigned long cursor = 8, compressedSize = 0;
    bool validHeader = false, ended = false;
    while (cursor + 12 <= pngSize) {
        unsigned long chunkSize = readBigEndian32(iconPng + cursor);
        const unsigned char* type = iconPng + cursor + 4;
        const unsigned char* data = iconPng + cursor + 8;
        if (chunkSize > pngSize - cursor - 12) return 0;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            validHeader = chunkSize == 13
                && readBigEndian32(data) == iconWidth
                && readBigEndian32(data + 4) == iconHeight
                && data[8] == 8 && data[9] == 6 && data[12] == 0;
            if (!validHeader) return 0;
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (chunkSize > sizeof(iconCompressed) - compressedSize) return 0;
            for (unsigned long i = 0; i < chunkSize; ++i)
                iconCompressed[compressedSize + i] = data[i];
            compressedSize += chunkSize;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            ended = true;
            break;
        }
        cursor += chunkSize + 12;
    }
    if (!validHeader || !ended || !compressedSize) return 0;
    unsigned long rawSize = iconRawSize;
    if (zlibUncompress(iconRaw, &rawSize, iconCompressed, compressedSize) != 0
        || rawSize != iconRawSize) return 0;

    constexpr int rowBytes = iconWidth * 4;
    for (int y = 0; y < iconHeight; ++y) {
        unsigned char* row = iconRaw + y * (rowBytes + 1) + 1;
        const unsigned char* previous = y ? iconRaw + (y - 1) * (rowBytes + 1) + 1 : nullptr;
        unsigned char filter = row[-1];
        if (filter > 4) return 0;
        for (int x = 0; x < rowBytes; ++x) {
            unsigned char left = x >= 4 ? row[x - 4] : 0;
            unsigned char above = previous ? previous[x] : 0;
            unsigned char upperLeft = previous && x >= 4 ? previous[x - 4] : 0;
            if (filter == 1) row[x] = static_cast<unsigned char>(row[x] + left);
            else if (filter == 2) row[x] = static_cast<unsigned char>(row[x] + above);
            else if (filter == 3) row[x] = static_cast<unsigned char>(row[x] + (left + above) / 2);
            else if (filter == 4) row[x] = static_cast<unsigned char>(row[x] + paeth(left, above, upperLeft));
        }
        for (int x = 0; x < iconWidth; ++x)
            iconAlpha[y * iconWidth + x] = row[x * 4 + 3];
    }

    GLint oldActive, oldTexture;
    getIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    activeTexture(GL_TEXTURE0);
    getIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    UnpackState unpack;
    GLuint texture = 0;
    genTextures(1, &texture);
    bindTexture(GL_TEXTURE_2D, texture);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texImage2D(GL_TEXTURE_2D, 0, GL_R8, iconWidth, iconHeight, 0,
               GL_RED, GL_UNSIGNED_BYTE, iconAlpha);
    bindTexture(GL_TEXTURE_2D, oldTexture);
    activeTexture(oldActive);
    if (!texture) return 0;
    for (int pathIndex = 0; pathIndex < assetLength; ++pathIndex)
        iconTextures[iconIndex].path[pathIndex] = assetPath[pathIndex];
    iconTextures[iconIndex].path[assetLength] = 0;
    iconTextures[iconIndex].texture = texture;
    return texture;
}

struct DrawState {
    GLint program, vao, buffer, active, texture, sampler, oldViewport[4];
    GLint srcRgb, dstRgb, srcAlpha, dstAlpha, eqRgb, eqAlpha;
    GLboolean colorWrite[4], enabled[6];
    static constexpr GLenum caps[] = {GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE,
                                      GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_RASTERIZER_DISCARD};
    DrawState() {
        getIntegerv(GL_CURRENT_PROGRAM, &program); getIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        getIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer); getIntegerv(GL_ACTIVE_TEXTURE, &active);
        getIntegerv(GL_VIEWPORT, oldViewport);
        activeTexture(GL_TEXTURE0); getIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        getIntegerv(GL_SAMPLER_BINDING, &sampler);
        getIntegerv(GL_BLEND_SRC_RGB, &srcRgb); getIntegerv(GL_BLEND_DST_RGB, &dstRgb);
        getIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); getIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        getIntegerv(GL_BLEND_EQUATION_RGB, &eqRgb); getIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
        getBooleanv(GL_COLOR_WRITEMASK, colorWrite);
        for (int i = 0; i < 6; ++i) {
            enabled[i] = isEnabled(caps[i]);
            if (i == 0) enable(caps[i]); else if (i != 3 || !drawClip) disable(caps[i]);
        }
        colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    ~DrawState() {
        for (int i = 0; i < 6; ++i) {
            if (enabled[i]) enable(caps[i]); else disable(caps[i]);
        }
        colorMask(colorWrite[0], colorWrite[1], colorWrite[2], colorWrite[3]);
        blendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
        blendEquationSeparate(eqRgb, eqAlpha);
        viewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        bindVertexArray(vao); bindBuffer(GL_ARRAY_BUFFER, buffer); useProgram(program);
        activeTexture(GL_TEXTURE0); bindTexture(GL_TEXTURE_2D, texture); bindSampler(0, sampler);
        activeTexture(active);
    }
};
}

static bool drawText(const char* text, float xPosition, float top, float height,
                     int screenWidth, int screenHeight, bool centered,
                     float red = menu_style::text.red, float green = menu_style::text.green,
                     float blue = menu_style::text.blue, int rasterHeight = 0) {
    if (!text || !*text || height <= 0 || !selectFont(rasterHeight ? rasterHeight : height)) return false;
    float scale = static_cast<float>(height) / activeFontPixels;
    float textWidth = 0;
    int count = 0;
    for (const char* p = text; *p && count < 64; ++p, ++count) {
        unsigned char c = static_cast<unsigned char>(*p);
        int index = c >= firstChar && c < firstChar + glyphCount ? c - firstChar : '?' - firstChar;
        textWidth += glyphs[index].advance * scale;
    }
    float x = centered ? xPosition - textWidth * 0.5f : static_cast<float>(xPosition);
    if (scale == 1.0f) x = static_cast<float>(static_cast<int>(x + 0.5f));
    float baseline = top + ascent * scale;
    int vertexCount = 0;
    count = 0;
    for (const char* p = text; *p && count < 64; ++p, ++count) {
        unsigned char c = static_cast<unsigned char>(*p);
        int index = c >= firstChar && c < firstChar + glyphCount ? c - firstChar : '?' - firstChar;
        const Glyph& glyph = glyphs[index];
        if (glyph.width && glyph.height) {
            float left = x + glyph.left * scale, right = left + glyph.width * scale;
            float y0 = baseline - glyph.top * scale, y1 = y0 + glyph.height * scale;
            const Vertex quad[] = {
                {left,y0,glyph.u0,glyph.v0},{right,y0,glyph.u1,glyph.v0},{right,y1,glyph.u1,glyph.v1},
                {left,y0,glyph.u0,glyph.v0},{right,y1,glyph.u1,glyph.v1},{left,y1,glyph.u0,glyph.v1}
            };
            for (const Vertex& v : quad) vertices[vertexCount++] = v;
        }
        x += glyph.advance * scale;
    }
    if (!vertexCount) return true;
    DrawState saved;
    activeTexture(GL_TEXTURE0); bindTexture(GL_TEXTURE_2D, atlasTexture); bindSampler(0, 0);
    blendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    blendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    useProgram(program); viewport(0, 0, screenWidth, screenHeight);
    uniform2f(screenUniform, screenWidth, screenHeight);
    uniform4f(colorUniform, red, green, blue, drawOpacity); uniform1i(samplerUniform, 0);
    uniform1i(rgbaUniform, 0);
    bindVertexArray(vao); bindBuffer(GL_ARRAY_BUFFER, buffer);
    bufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(Vertex), vertices, GL_STREAM_DRAW);
    drawArrays(GL_TRIANGLES, 0, vertexCount);
    return true;
}

bool custom_font_draw(const char* text, int centerX, int top, int height,
                      int screenWidth, int screenHeight) {
    return drawText(text, centerX, top, height, screenWidth, screenHeight, true);
}

bool custom_font_draw_left(const char* text, int leftX, int top, int height,
                           int screenWidth, int screenHeight) {
    return drawText(text, leftX, top, height, screenWidth, screenHeight, false);
}

bool custom_font_draw_left_scaled(const char* text, float leftX, float top, int height,
                                  float scale, int screenWidth, int screenHeight) {
    float savedScale = rasterScale;
    rasterScale = 1.0f;
    bool drawn = drawText(text, leftX, top, height * scale, screenWidth, screenHeight, false,
                          menu_style::text.red, menu_style::text.green, menu_style::text.blue, height);
    rasterScale = savedScale;
    return drawn;
}

bool custom_font_draw_left_color(const char* text, int leftX, int top, int height,
                                 float red, float green, float blue,
                                 int screenWidth, int screenHeight) {
    return drawText(text, leftX, top, height, screenWidth, screenHeight, false, red, green, blue);
}

int custom_font_text_width(const char* text, int height) {
    if (!text || height <= 0 || !selectFont(height)) return 0;
    float scale = static_cast<float>(height) / activeFontPixels;
    float width = 0;
    for (int count = 0; text[count] && count < 64; ++count) {
        unsigned char c = static_cast<unsigned char>(text[count]);
        int index = c >= firstChar && c < firstChar + glyphCount ? c - firstChar : '?' - firstChar;
        width += glyphs[index].advance * scale;
    }
    return static_cast<int>(width + 0.5f);
}

void custom_font_set_opacity(float opacity) {
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    drawOpacity = opacity;
}

static bool drawImage(GLuint texture, int rgba, int centerX, int top, int size,
                      float red, float green, float blue, int screenWidth, int screenHeight, int imageHeight = 0, bool flip = false) {
    float left = centerX - size * 0.5f;
    float right = centerX + size * 0.5f;
    float bottom = top + (imageHeight ? imageHeight : size);
    float v0 = flip ? 1.0f : 0.0f, v1 = flip ? 0.0f : 1.0f;
    const Vertex quad[] = {
        {left,static_cast<float>(top),0.0f,v0},{right,static_cast<float>(top),1.0f,v0},
        {right,bottom,1.0f,v1},{left,static_cast<float>(top),0.0f,v0},
        {right,bottom,1.0f,v1},{left,bottom,0.0f,v1}
    };
    for (int i = 0; i < 6; ++i) vertices[i] = quad[i];
    DrawState saved;
    activeTexture(GL_TEXTURE0); bindTexture(GL_TEXTURE_2D, texture); bindSampler(0, 0);
    blendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    blendFuncSeparate(rgba == 2 ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    useProgram(program); viewport(0, 0, screenWidth, screenHeight);
    uniform2f(screenUniform, screenWidth, screenHeight);
    uniform4f(colorUniform, red, green, blue, drawOpacity); uniform1i(samplerUniform, 0);
    uniform1i(rgbaUniform, rgba);
    bindVertexArray(vao); bindBuffer(GL_ARRAY_BUFFER, buffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    drawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

bool custom_font_draw_icon(const char* assetPath, int centerX, int top, int size,
                           float red, float green, float blue, int screenWidth, int screenHeight) {
    if (size <= 0 || !initFont()) return false;
    GLuint texture = initIcon(assetPath);
    return texture && drawImage(texture, false, centerX, top, size, red, green, blue, screenWidth, screenHeight);
}

bool custom_font_draw_head(const unsigned char* rgba, int centerX, int top, int size,
                           int screenWidth, int screenHeight) {
    if (!rgba || size <= 0 || !initFont()) return false;
    DrawState saved;
    UnpackState unpack;
    activeTexture(GL_TEXTURE0);
    if (!headTexture) {
        genTextures(1, &headTexture); bindTexture(GL_TEXTURE_2D, headTexture);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    } else bindTexture(GL_TEXTURE_2D, headTexture);
    // One small upload buffer avoids GPU objects and eviction per player.
    texSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return drawImage(headTexture, true, centerX, top, size, 1, 1, 1, screenWidth, screenHeight);
}

void custom_font_set_clip(bool enabled) { drawClip = enabled; }

void custom_font_set_raster_scale(float scale) {
    rasterScale = scale > 0.0f ? scale : 1.0f;
}

void custom_font_set_mojangles(bool enabled) { useMojangles = enabled; }

namespace {
struct SkinVertex { float x, y, z, u, v; };
struct SkinFace { SkinVertex vertices[6]; float depth, shade; };
// Static orthographic camera, 20 degrees around the player's vertical axis.
SkinVertex projectSkin(float x, float y, float z, float u, float v, float textureHeight) {
    return {128 + (0.9396926f*x + 0.3420201f*z)*14, 32 + y*14,
            (-0.3420201f*x + 0.9396926f*z)/32, u/64, v/textureHeight};
}
void skinBox(SkinFace* faces, int& count, float x, float y, float w, float h, float d,
             int u, int v, bool overlay, bool mirror, float textureHeight) {
    float expand = overlay ? (h == 8 ? 0.5f : 0.25f) : 0;
    float x0 = x-expand, x1 = x+w+expand, y0 = y-expand, y1 = y+h+expand;
    float z0 = -d/2-expand, z1 = d/2+expand;
    struct Point { float x,y,z; };
    auto face = [&](Point a, Point b, Point c, Point e, float tx, float ty, float tw, float th, float shade) {
        if (mirror) { a.x=-a.x; b.x=-b.x; c.x=-c.x; e.x=-e.x; }
        SkinVertex q[] = {projectSkin(a.x,a.y,a.z,tx,ty,textureHeight),
            projectSkin(b.x,b.y,b.z,tx+tw,ty,textureHeight),
            projectSkin(c.x,c.y,c.z,tx+tw,ty+th,textureHeight),
            projectSkin(e.x,e.y,e.z,tx,ty+th,textureHeight)};
        SkinFace value{{q[0],q[1],q[2],q[0],q[2],q[3]},
            (q[0].z+q[1].z+q[2].z+q[3].z)/4,shade};
        faces[count++] = value;
    };
    face({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},u+d,v+d,w,h,1);
    face({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},u+d+w,v+d,d,h,0.78f);
    face({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},u+2*d+w,v+d,w,h,0.72f);
    face({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},u,v+d,d,h,0.78f);
    face({x0,y0,z1},{x1,y0,z1},{x1,y0,z0},{x0,y0,z0},u+d,v,w,d,1);
    face({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1},u+d+w,v,w,d,0.65f);
}
}

bool custom_font_draw_skin(const unsigned char* rgba, int textureWidth, int textureHeight,
                           unsigned long revision, int centerX, int top, int height,
                           int screenWidth, int screenHeight) {
    if (!rgba || height <= 0 || (textureWidth != 64 && textureWidth != 128 && textureWidth != 256)
        || (textureHeight != textureWidth && textureHeight != textureWidth/2) || !initFont()) return false;
    DrawState saved;
    UnpackState unpack;
    GLint oldFramebuffer, oldRenderbuffer, oldDepthFunc;
    GLboolean oldDepthWrite;
    getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFramebuffer);
    getIntegerv(GL_RENDERBUFFER_BINDING, &oldRenderbuffer);
    getIntegerv(GL_DEPTH_FUNC, &oldDepthFunc); getBooleanv(GL_DEPTH_WRITEMASK, &oldDepthWrite);
    struct Restore {
        GLint framebuffer, renderbuffer, function; GLboolean write;
        ~Restore() { bindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer); bindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
                     depthFunc(function); depthMask(write); }
    } restore{oldFramebuffer,oldRenderbuffer,oldDepthFunc,oldDepthWrite};
    activeTexture(GL_TEXTURE0); bindSampler(0,0);
    if (!previewFramebuffer) {
        genTextures(1,&previewTexture); bindTexture(GL_TEXTURE_2D,previewTexture);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,512,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        genFramebuffers(1,&previewFramebuffer); bindFramebuffer(GL_DRAW_FRAMEBUFFER,previewFramebuffer);
        framebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,previewTexture,0);
        genRenderbuffers(1,&previewDepth); bindRenderbuffer(GL_RENDERBUFFER,previewDepth);
        renderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,256,512);
        framebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,previewDepth);
        genTextures(1,&skinTexture); genVertexArrays(1,&skinVao);
        bindVertexArray(skinVao); bindBuffer(GL_ARRAY_BUFFER,buffer);
        enableVertexAttribArray(0); vertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(SkinVertex),nullptr);
        enableVertexAttribArray(1); vertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(SkinVertex),reinterpret_cast<void*>(3*sizeof(float)));
    }
    bindFramebuffer(GL_DRAW_FRAMEBUFFER,previewFramebuffer);
    if (checkFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    bool clipped = isEnabled(GL_SCISSOR_TEST);
    disable(GL_SCISSOR_TEST);
    if (previewRevision != revision || !revision) {
        const float clear[] = {0,0,0,0}, far = 1;
        depthMask(GL_TRUE); clearBufferfv(GL_COLOR,0,clear); clearBufferfv(GL_DEPTH,0,&far);
        enable(GL_DEPTH_TEST); depthFunc(GL_LESS);
        bindTexture(GL_TEXTURE_2D,skinTexture);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,textureWidth,textureHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        bool legacy = textureHeight != textureWidth;
        // ponytail: infer slim arms from unused alpha strips; use verified native
        // model metadata if unusual texture layouts need exact arm selection.
        bool slim = !legacy;
        int scale = textureWidth/64;
        for (int y = 20; slim && y < 32; ++y) for (int x = 54; x < 56; ++x)
            if (rgba[((y*scale)*textureWidth+x*scale)*4+3]) slim = false;
        for (int y = 52; slim && y < 64; ++y) for (int x = 46; x < 48; ++x)
            if (rgba[((y*scale)*textureWidth+x*scale)*4+3]) slim = false;
        float arm = slim ? 3 : 4, atlasHeight = legacy ? 32 : 64;
        useProgram(program); viewport(0,0,256,512); uniform2f(screenUniform,256,512);
        uniform1i(samplerUniform,0); uniform1i(rgbaUniform,1);
        blendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        blendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        bindVertexArray(skinVao); bindBuffer(GL_ARRAY_BUFFER,buffer);
        for (int layer = 0; layer < 2; ++layer) {
            SkinFace faces[36]; int count = 0;
            skinBox(faces,count,-4,0,8,8,8,layer ? 32 : 0,0,layer,false,atlasHeight);
            if (!layer || !legacy) {
                skinBox(faces,count,-4,8,8,12,4,16,layer ? 32 : 16,layer,false,atlasHeight);
                skinBox(faces,count,-4-arm,8,arm,12,4,40,layer ? 32 : 16,layer,false,atlasHeight);
                skinBox(faces,count,-4,20,4,12,4,0,layer ? 32 : 16,layer,false,atlasHeight);
                if (legacy) {
                    skinBox(faces,count,-4-arm,8,arm,12,4,40,16,false,true,atlasHeight);
                    skinBox(faces,count,-4,20,4,12,4,0,16,false,true,atlasHeight);
                } else {
                    skinBox(faces,count,4,8,arm,12,4,layer ? 48 : 32,48,layer,false,atlasHeight);
                    skinBox(faces,count,0,20,4,12,4,layer ? 0 : 16,48,layer,false,atlasHeight);
                }
            }
            // Transparent outer faces blend back to front against the base model's depth.
            depthMask(layer ? GL_FALSE : GL_TRUE);
            for (int i = 1; i < count; ++i) {
                SkinFace value = faces[i]; int j = i;
                while (j && faces[j-1].depth < value.depth) { faces[j] = faces[j-1]; --j; }
                faces[j] = value;
            }
            for (int i = 0; i < count; ++i) {
                float shade = faces[i].shade;
                uniform4f(colorUniform,shade,shade,shade,1);
                bufferData(GL_ARRAY_BUFFER,sizeof(faces[i].vertices),faces[i].vertices,GL_STREAM_DRAW);
                drawArrays(GL_TRIANGLES,0,6);
            }
        }
        previewRevision = revision;
    }
    bindFramebuffer(GL_DRAW_FRAMEBUFFER,oldFramebuffer); disable(GL_DEPTH_TEST);
    if (clipped) enable(GL_SCISSOR_TEST);
    return drawImage(previewTexture,2,centerX,top,height/2,1,1,1,screenWidth,screenHeight,height,true);
}

bool custom_font_draw_ring(int centerX, int centerY, float radius, float progress,
                           int screenWidth, int screenHeight) {
    if (radius <= 1 || progress <= 0 || !initFont()) return false;
    if (progress > 1) progress = 1;
    DrawState saved; UnpackState unpack;
    activeTexture(GL_TEXTURE0); bindSampler(0,0);
    if (!ringTexture) {
        const unsigned char white = 255;
        genTextures(1,&ringTexture); bindTexture(GL_TEXTURE_2D,ringTexture);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        texImage2D(GL_TEXTURE_2D,0,GL_R8,1,1,0,GL_RED,GL_UNSIGNED_BYTE,&white);
    } else bindTexture(GL_TEXTURE_2D,ringTexture);
    int count = 0;
    float x = 0, y = -1;
    for (int i = 0; i < 48 && i < progress*48; ++i) {
        float nx = x*0.9914449f-y*0.1305262f, ny = x*0.1305262f+y*0.9914449f;
        float fraction = progress*48-i;
        if (fraction < 1) { nx = x+(nx-x)*fraction; ny = y+(ny-y)*fraction; }
        float inner = radius-1.5f;
        Vertex a{centerX+x*radius,centerY+y*radius,0,0}, b{centerX+nx*radius,centerY+ny*radius,0,0};
        Vertex c{centerX+nx*inner,centerY+ny*inner,0,0}, d{centerX+x*inner,centerY+y*inner,0,0};
        const Vertex quad[] = {a,b,c,a,c,d};
        for (auto vertex : quad) vertices[count++] = vertex;
        x = nx; y = ny;
    }
    useProgram(program); viewport(0,0,screenWidth,screenHeight);
    uniform2f(screenUniform,screenWidth,screenHeight); uniform4f(colorUniform,1,1,1,drawOpacity);
    uniform1i(samplerUniform,0); uniform1i(rgbaUniform,0);
    blendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    blendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    bindVertexArray(vao); bindBuffer(GL_ARRAY_BUFFER,buffer);
    bufferData(GL_ARRAY_BUFFER,count*sizeof(Vertex),vertices,GL_STREAM_DRAW); drawArrays(GL_TRIANGLES,0,count);
    return true;
}
