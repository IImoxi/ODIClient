#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "launcher_api.h"
#include "motion_blur.h"
#include "panel_renderer.h"

namespace {
// Resolve against the host's current GL context, never the Android GL shim.
#define GL_FUNCTIONS(X) \
    X(GetString) X(GetIntegerv) X(GetBooleanv) X(IsEnabled) X(Enable) X(Disable) X(Scissor) \
    X(ActiveTexture) X(GenTextures) X(BindTexture) X(TexParameteri) X(TexImage2D) \
    X(CopyTexSubImage2D) X(GenerateMipmap) X(BindBuffer) X(BindSampler) X(GenVertexArrays) X(BindVertexArray) \
    X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv) X(DeleteShader) \
    X(CreateProgram) X(AttachShader) X(LinkProgram) X(GetProgramiv) X(DeleteProgram) \
    X(GetUniformLocation) X(UseProgram) X(Uniform1i) X(Uniform1f) X(Uniform3f) X(Uniform4f) \
    X(GenFramebuffers) X(FramebufferTexture2D) X(CheckFramebufferStatus) X(BindFramebuffer) X(ReadBuffer) X(DrawBuffers) X(Viewport) X(ColorMask) \
    X(BlendFuncSeparate) X(BlendEquationSeparate) X(DrawArrays)
#define DECLARE(name) decltype(&gl##name) name;
GL_FUNCTIONS(DECLARE)
#undef DECLARE
decltype(&eglGetProcAddress) getProc;
decltype(&eglGetCurrentContext) getContext;
decltype(&eglGetCurrentDisplay) getDisplay;
decltype(&eglGetCurrentSurface) getSurface;
decltype(&eglQuerySurface) querySurface;
EGLContext context = EGL_NO_CONTEXT;
// GLES 3 / OpenGL 3.3 guarantee at least 16 fragment texture units.
constexpr int maxHistoryFrames = 15;
GLuint program, historyTextures[maxHistoryFrames], scratchTexture, vao;
GLuint panelTexture, panelBlurTexture, panelBlurFbo;
long long panelBlurTimestamp;
float panelBlurScale;
GLint weightUniform, historyTotalUniform, frameCountUniform, spatialUniform;
GLint tintColorUniform, tintAmountUniform, panelAlphaUniform;
GLint spatialScaleUniform;
GLint roundedRectUniform, cornerRadiusUniform, borderWidthUniform;
GLint historyUniforms[maxHistoryFrames], historyWeightUniforms[maxHistoryFrames];
long long historyTimestamps[maxHistoryFrames];
int width, height;
bool valid = false;
int frameHistoryCount;
int allocatedHistoryCount;
bool scratchAllocated;
int frostedScratchWidth, frostedScratchHeight;
bool previousAverageMode;
bool previousAdaptiveMode;
bool modeSet = false;
bool apiLoaded;
const char* error;

void setError(const char* message) { __atomic_store_n(&error, message, __ATOMIC_RELEASE); }

bool loadAPI() {
    void* library = mcpelauncher_host_dlopen("libEGL.so.1", 2);
    if (!library) return false;
#define EGL_LOAD(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(library, name))
    EGL_LOAD(getProc, "eglGetProcAddress");
    EGL_LOAD(getContext, "eglGetCurrentContext");
    EGL_LOAD(getDisplay, "eglGetCurrentDisplay");
    EGL_LOAD(getSurface, "eglGetCurrentSurface");
    EGL_LOAD(querySurface, "eglQuerySurface");
#undef EGL_LOAD
    if (!getProc || !getContext || !getDisplay || !getSurface || !querySurface) return false;
#define LOAD(name) name = reinterpret_cast<decltype(name)>(getProc("gl" #name)); if (!name) return false;
    GL_FUNCTIONS(LOAD)
#undef LOAD
    apiLoaded = true;
    return true;
}

GLuint shader(GLenum type, const char* version, const char* source) {
    GLuint result = CreateShader(type);
    const char* parts[] = {version, source};
    ShaderSource(result, 2, parts, nullptr);
    CompileShader(result);
    GLint ok;
    GetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) { DeleteShader(result); return 0; }
    return result;
}

bool createResources() {
    const char* glVersion = reinterpret_cast<const char*>(GetString(GL_VERSION));
    if (!glVersion) return false;
    bool es = glVersion[0] == 'O' && glVersion[1] == 'p' && glVersion[2] == 'e';
    GLint major, minor;
    GetIntegerv(GL_MAJOR_VERSION, &major);
    GetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (!es && major == 3 && minor < 3)) {
        setError("Motion Blur: needs GLES 3 / OpenGL 3.3");
        return false;
    }
    const char* version = es ? "#version 300 es\nprecision highp float;\n" : "#version 330 core\n";
    GLuint vertex = shader(GL_VERTEX_SHADER, version,
        "out vec2 uv;\n"
        "void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
        "uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n");
    GLuint fragment = shader(GL_FRAGMENT_SHADER, version,
        "in vec2 uv;out vec4 color;uniform sampler2D history0,history1,history2,history3,history4,history5,history6,history7,history8,history9,history10,history11,history12,history13,history14;"
        "uniform float historyWeight0,historyWeight1,historyWeight2,historyWeight3,historyWeight4,historyWeight5,historyWeight6,historyWeight7,historyWeight8,historyWeight9,historyWeight10,historyWeight11,historyWeight12,historyWeight13,historyWeight14;"
        "uniform int count;uniform float weight,historyTotal;uniform int spatial;"
        "uniform vec3 tintColor;uniform float tintAmount,spatialScale,cornerRadius,panelAlpha,borderWidth;"
        "uniform vec4 roundedRect;\n"
        "void main(){float coverage=1.0;if(spatial!=0&&(cornerRadius>0.0||borderWidth>0.0)){vec2 q=abs(gl_FragCoord.xy-(roundedRect.xy+roundedRect.zw*0.5))"
        "-(roundedRect.zw*0.5-vec2(cornerRadius));"
        "float d=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-cornerRadius;"
        "float aa=max(fwidth(d),1.0);coverage=1.0-smoothstep(-aa*0.5,aa*0.5,d);"
        "if(borderWidth>0.0)coverage-=1.0-smoothstep(-aa*0.5,aa*0.5,d+borderWidth);"
        "if(coverage<=0.0)discard;}"
        "if(spatial!=0){vec3 background=tintColor;"
        "if(tintAmount<1.0){background=texture(history0,uv).rgb;"
        "if(spatialScale>0.0){vec2 stepUV=2.0*spatialScale/vec2(textureSize(history0,0));"
        "float lod=log2(max(2.0*spatialScale,1.0));"
        "vec4 sum=vec4(0.0);float total=0.0;"
        "for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x){"
        "float w=exp(-float(x*x+y*y)/(8.0*spatialScale*spatialScale));"
        "sum+=textureLod(history0,uv+vec2(float(x),float(y))*stepUV,lod)*w;total+=w;}"
        "background=sum.rgb/total;}}color=vec4(mix(background,tintColor,tintAmount),panelAlpha*coverage);return;}"
        "vec3 c=texture(history0,uv).rgb*historyWeight0;"
        "if(count>1)c+=texture(history1,uv).rgb*historyWeight1;"
        "if(count>2)c+=texture(history2,uv).rgb*historyWeight2;"
        "if(count>3)c+=texture(history3,uv).rgb*historyWeight3;"
        "if(count>4)c+=texture(history4,uv).rgb*historyWeight4;"
        "if(count>5)c+=texture(history5,uv).rgb*historyWeight5;"
        "if(count>6)c+=texture(history6,uv).rgb*historyWeight6;"
        "if(count>7)c+=texture(history7,uv).rgb*historyWeight7;"
        "if(count>8)c+=texture(history8,uv).rgb*historyWeight8;"
        "if(count>9)c+=texture(history9,uv).rgb*historyWeight9;"
        "if(count>10)c+=texture(history10,uv).rgb*historyWeight10;"
        "if(count>11)c+=texture(history11,uv).rgb*historyWeight11;"
        "if(count>12)c+=texture(history12,uv).rgb*historyWeight12;"
        "if(count>13)c+=texture(history13,uv).rgb*historyWeight13;"
        "if(count>14)c+=texture(history14,uv).rgb*historyWeight14;"
        "color=vec4(c/historyTotal,weight);}\n");
    if (!vertex || !fragment) {
        if (vertex) DeleteShader(vertex);
        if (fragment) DeleteShader(fragment);
        setError("Motion Blur: shader compilation failed");
        return false;
    }
    program = CreateProgram();
    AttachShader(program, vertex);
    AttachShader(program, fragment);
    LinkProgram(program);
    DeleteShader(vertex);
    DeleteShader(fragment);
    GLint ok;
    GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        DeleteProgram(program); program = 0;
        setError("Motion Blur: shader link failed");
        return false;
    }
    weightUniform = GetUniformLocation(program, "weight");
    historyTotalUniform = GetUniformLocation(program, "historyTotal");
    spatialUniform = GetUniformLocation(program, "spatial");
    tintColorUniform = GetUniformLocation(program, "tintColor");
    tintAmountUniform = GetUniformLocation(program, "tintAmount");
    panelAlphaUniform = GetUniformLocation(program, "panelAlpha");
    spatialScaleUniform = GetUniformLocation(program, "spatialScale");
    roundedRectUniform = GetUniformLocation(program, "roundedRect");
    cornerRadiusUniform = GetUniformLocation(program, "cornerRadius");
    borderWidthUniform = GetUniformLocation(program, "borderWidth");
    frameCountUniform = GetUniformLocation(program, "count");
    const char* names[] = {"history0", "history1", "history2", "history3", "history4", "history5", "history6", "history7", "history8", "history9", "history10", "history11", "history12", "history13", "history14"};
    const char* weightNames[] = {"historyWeight0", "historyWeight1", "historyWeight2", "historyWeight3", "historyWeight4", "historyWeight5", "historyWeight6", "historyWeight7", "historyWeight8", "historyWeight9", "historyWeight10", "historyWeight11", "historyWeight12", "historyWeight13", "historyWeight14"};
    for (int i = 0; i < maxHistoryFrames; ++i) {
        historyUniforms[i] = GetUniformLocation(program, names[i]);
        historyWeightUniforms[i] = GetUniformLocation(program, weightNames[i]);
    }
    GenTextures(maxHistoryFrames, historyTextures);
    GenTextures(1, &scratchTexture);
    GenTextures(1, &panelTexture);
    GenTextures(1, &panelBlurTexture);
    GenFramebuffers(1, &panelBlurFbo);
    GenVertexArrays(1, &vao);
    return true;
}

struct SavedState {
    GLint activeTexture, textures[maxHistoryFrames], samplers[maxHistoryFrames], unpackBuffer, program, vao;
    GLint drawFbo, readFbo, viewport[4], scissor[4], srcRGB, dstRGB, srcAlpha, dstAlpha, eqRGB, eqAlpha;
    GLboolean colorMask[4], enabled[6];
    static constexpr GLenum caps[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST,
                                      GL_CULL_FACE, GL_SCISSOR_TEST, GL_RASTERIZER_DISCARD};
    SavedState() {
        GetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int i = 0; i < maxHistoryFrames; ++i) {
            ActiveTexture(GL_TEXTURE0 + i);
            GetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
            GetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
        }
        GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        GetIntegerv(GL_CURRENT_PROGRAM, &program);
        GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        GetIntegerv(GL_VIEWPORT, viewport);
        GetIntegerv(GL_SCISSOR_BOX, scissor);
        GetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); GetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        GetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); GetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        GetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB); GetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
        GetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        for (int i = 0; i < 6; ++i) enabled[i] = IsEnabled(caps[i]);
    }
    ~SavedState() {
        for (int i = 0; i < 6; ++i) { if (enabled[i]) Enable(caps[i]); else Disable(caps[i]); }
        BlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
        BlendEquationSeparate(eqRGB, eqAlpha);
        ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        Scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        UseProgram(program); BindVertexArray(vao);
        BindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        for (int i = 0; i < maxHistoryFrames; ++i) {
            ActiveTexture(GL_TEXTURE0 + i);
            BindTexture(GL_TEXTURE_2D, textures[i]); BindSampler(i, samplers[i]);
        }
        ActiveTexture(activeTexture);
    }
};
}

const char* motion_blur_error() { return __atomic_load_n(&error, __ATOMIC_ACQUIRE); }

bool draw_gl_panel(int x, int y, int panelWidth, int panelHeight, const PanelPaint& paint) {
    float cornerRadius = paint.cornerRadius;
    if (panelWidth <= 0 || panelHeight <= 0) return false;
    if (cornerRadius < 0.0f) cornerRadius = 0.0f;
    float maxRadius = (panelWidth < panelHeight ? panelWidth : panelHeight) * 0.5f;
    if (cornerRadius > maxRadius) cornerRadius = maxRadius;
    if (!apiLoaded && !loadAPI()) return false;
    EGLContext current = getContext();
    if (current == EGL_NO_CONTEXT) return false;
    if (current != context) {
        context = current; program = scratchTexture = vao = 0;
        panelTexture = panelBlurTexture = panelBlurFbo = 0; panelBlurTimestamp = 0;
        for (int i = 0; i < maxHistoryFrames; ++i) { historyTextures[i] = 0; historyTimestamps[i] = 0; }
        width = height = 0; valid = false; frameHistoryCount = 0;
        allocatedHistoryCount = 0; scratchAllocated = false; modeSet = false;
        frostedScratchWidth = frostedScratchHeight = 0;
    }

    EGLint surfaceWidth, surfaceHeight;
    EGLSurface surface = getSurface(EGL_DRAW);
    if (surface == EGL_NO_SURFACE
        || !querySurface(getDisplay(), surface, EGL_WIDTH, &surfaceWidth)
        || !querySurface(getDisplay(), surface, EGL_HEIGHT, &surfaceHeight)
        || surfaceWidth <= 0 || surfaceHeight <= 0) return false;
    if (!program && !createResources()) return false;

    SavedState saved;
    bool cachedBlur = paint.blurTimestampNs > 0 && paint.blurScale > 0.0f
        && paint.tintAmount < 1.0f;
    bool resized = frostedScratchWidth != surfaceWidth || frostedScratchHeight != surfaceHeight;
    bool refresh = !cachedBlur || resized || panelBlurTimestamp == 0
        || paint.blurTimestampNs < panelBlurTimestamp
        || paint.blurTimestampNs - panelBlurTimestamp >= 33333333LL
        || panelBlurScale != paint.blurScale;
    if (paint.tintAmount < 1.0f && resized) {
        ActiveTexture(GL_TEXTURE0);
        BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        GLuint panelTextures[] = {panelTexture, panelBlurTexture};
        for (GLuint texture : panelTextures) {
            BindTexture(GL_TEXTURE_2D, texture);
            TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                       texture == panelTexture ? surfaceWidth : (surfaceWidth + 3) / 4,
                       texture == panelTexture ? surfaceHeight : (surfaceHeight + 3) / 4,
                       0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        frostedScratchWidth = surfaceWidth;
        frostedScratchHeight = surfaceHeight;
        panelBlurTimestamp = 0;
    }

    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GLint readBuffer, drawBuffer;
    GetIntegerv(GL_READ_BUFFER, &readBuffer);
    GetIntegerv(GL_DRAW_BUFFER0, &drawBuffer);
    ReadBuffer(GL_BACK);
    GLenum back = GL_BACK;
    DrawBuffers(1, &back);
    ActiveTexture(GL_TEXTURE0);
    BindTexture(GL_TEXTURE_2D, panelTexture);
    if (paint.tintAmount < 1.0f && refresh)
        CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, surfaceWidth, surfaceHeight);
    if (paint.tintAmount < 1.0f && paint.blurScale > 0.0f && refresh) {
        // Prefilter the gaps between blur taps so fine detail cannot form a sample grid.
        GenerateMipmap(GL_TEXTURE_2D);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    for (GLenum cap : SavedState::caps) Disable(cap);
    Enable(GL_SCISSOR_TEST);
    Enable(GL_BLEND);
    BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                      GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    int clipX = x, clipY = y, clipRight = x + panelWidth, clipTop = y + panelHeight;
    if (paint.inheritScissor && saved.enabled[4]) {
        if (clipX < saved.scissor[0]) clipX = saved.scissor[0];
        if (clipY < saved.scissor[1]) clipY = saved.scissor[1];
        if (clipRight > saved.scissor[0] + saved.scissor[2]) clipRight = saved.scissor[0] + saved.scissor[2];
        if (clipTop > saved.scissor[1] + saved.scissor[3]) clipTop = saved.scissor[1] + saved.scissor[3];
    }
    Scissor(clipX, clipY, clipRight > clipX ? clipRight - clipX : 0,
            clipTop > clipY ? clipTop - clipY : 0);
    Viewport(0, 0, surfaceWidth, surfaceHeight);
    ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    ActiveTexture(GL_TEXTURE0);
    BindSampler(0, 0);
    BindTexture(GL_TEXTURE_2D, panelTexture);
    UseProgram(program);
    Uniform1i(historyUniforms[0], 0);
    Uniform1i(spatialUniform, 1);
    Uniform1f(spatialScaleUniform, paint.blurScale);
    Uniform3f(tintColorUniform, paint.tint.red, paint.tint.green, paint.tint.blue);
    Uniform1f(tintAmountUniform, paint.tintAmount);
    Uniform1f(panelAlphaUniform, paint.opacity);
    Uniform4f(roundedRectUniform, static_cast<float>(x), static_cast<float>(y),
              static_cast<float>(panelWidth), static_cast<float>(panelHeight));
    Uniform1f(cornerRadiusUniform, cornerRadius);
    Uniform1f(borderWidthUniform, paint.borderWidth);
    BindVertexArray(vao);
    if (cachedBlur) {
        if (refresh) {
            BindFramebuffer(GL_DRAW_FRAMEBUFFER, panelBlurFbo);
            FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, panelBlurTexture, 0);
            GLenum attachment = GL_COLOR_ATTACHMENT0;
            DrawBuffers(1, &attachment);
            if (CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                ReadBuffer(readBuffer);
                GLenum previousDraw = static_cast<GLenum>(drawBuffer);
                DrawBuffers(1, &previousDraw);
                return false;
            }
            Disable(GL_SCISSOR_TEST);
            Disable(GL_BLEND);
            Viewport(0, 0, (surfaceWidth + 3) / 4, (surfaceHeight + 3) / 4);
            Uniform1f(tintAmountUniform, 0.0f);
            Uniform1f(panelAlphaUniform, 1.0f);
            Uniform1f(cornerRadiusUniform, 0.0f);
            Uniform1f(borderWidthUniform, 0.0f);
            DrawArrays(GL_TRIANGLES, 0, 3);
            panelBlurTimestamp = paint.blurTimestampNs;
            panelBlurScale = paint.blurScale;
            BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            DrawBuffers(1, &back);
            Enable(GL_SCISSOR_TEST);
            Enable(GL_BLEND);
            Viewport(0, 0, surfaceWidth, surfaceHeight);
        }
        BindTexture(GL_TEXTURE_2D, panelBlurTexture);
        Uniform1f(spatialScaleUniform, 0.0f);
        Uniform1f(tintAmountUniform, paint.tintAmount);
        Uniform1f(panelAlphaUniform, paint.opacity);
        Uniform1f(cornerRadiusUniform, cornerRadius);
        Uniform1f(borderWidthUniform, paint.borderWidth);
    }
    DrawArrays(GL_TRIANGLES, 0, 3);

    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    ReadBuffer(readBuffer);
    GLenum previousDraw = static_cast<GLenum>(drawBuffer);
    DrawBuffers(1, &previousDraw);
    return true;
}

void motion_blur_render(bool enabled, float strength, float averageFrames, bool screenBlur,
                        int averageHz, long long frameTimestampNs) {
    bool adaptiveMode = averageHz >= 30 && averageHz <= 500;
    bool temporal = enabled && (adaptiveMode || strength > 0.0f);
    if (!temporal) { valid = false; frameHistoryCount = 0; }
    if (!temporal && !screenBlur) { valid = false; frameHistoryCount = 0; return; }
    if (averageFrames < 1.0f) averageFrames = 1.0f;
    if (averageFrames > maxHistoryFrames + 1) averageFrames = maxHistoryFrames + 1;
    bool averageMode = adaptiveMode || averageFrames > 1;
    if (error) return;
    if (!apiLoaded && !loadAPI()) { setError("Motion Blur: GL API unavailable"); return; }
    EGLContext current = getContext();
    if (current == EGL_NO_CONTEXT) { valid = false; frameHistoryCount = 0; return; }
    if (current != context) {
        // Old-context names must never be deleted or reused in a new context.
        context = current; program = scratchTexture = vao = 0;
        panelTexture = panelBlurTexture = panelBlurFbo = 0; panelBlurTimestamp = 0;
        for (int i = 0; i < maxHistoryFrames; ++i) { historyTextures[i] = 0; historyTimestamps[i] = 0; }
        width = height = 0; valid = false; frameHistoryCount = 0;
        allocatedHistoryCount = 0; scratchAllocated = false; modeSet = false;
        frostedScratchWidth = frostedScratchHeight = 0;
    }
    if (!modeSet || previousAverageMode != averageMode || previousAdaptiveMode != adaptiveMode) {
        valid = false; frameHistoryCount = 0;
        previousAverageMode = averageMode;
        previousAdaptiveMode = adaptiveMode;
        modeSet = true;
    }
    int neededHistory = averageMode ? static_cast<int>(averageFrames) - 1 : 1;
    if (averageMode && !adaptiveMode && static_cast<float>(neededHistory + 1) < averageFrames)
        ++neededHistory;
    if (adaptiveMode) {
        long long remaining = 1000000000LL / averageHz;
        if (frameHistoryCount > 0 && frameTimestampNs > historyTimestamps[0])
            remaining -= frameTimestampNs - historyTimestamps[0];
        if (remaining < 0) remaining = 0;
        long long estimatedFrameNs = remaining;
        neededHistory = 0;
        for (int i = 0; i < frameHistoryCount && i < maxHistoryFrames && remaining > 0; ++i) {
            long long duration = i == 0 ? frameTimestampNs - historyTimestamps[0]
                                        : historyTimestamps[i - 1] - historyTimestamps[i];
            if (duration > 0) {
                if (estimatedFrameNs <= 0 || estimatedFrameNs > duration)
                    estimatedFrameNs = duration;
                ++neededHistory;
                remaining -= duration;
            }
        }
        // Count frames the ring still needs to collect so a larger target window
        // can grow beyond the history allocated for the previous slider value.
        if (remaining > 0 && estimatedFrameNs > 0)
            neededHistory += static_cast<int>((remaining + estimatedFrameNs / 2) / estimatedFrameNs);
        if (neededHistory < 1) neededHistory = 1;
        if (neededHistory > maxHistoryFrames) neededHistory = maxHistoryFrames;
    }
    EGLint w, h;
    EGLSurface surface = getSurface(EGL_DRAW);
    if (surface == EGL_NO_SURFACE || !querySurface(getDisplay(), surface, EGL_WIDTH, &w)
        || !querySurface(getDisplay(), surface, EGL_HEIGHT, &h) || w < 1 || h < 1) {
        valid = false; frameHistoryCount = 0; return;
    }
    SavedState saved;
    if (!program && !createResources()) {
        if (!error) setError("Motion Blur: resource initialization failed");
        return;
    }
    ActiveTexture(GL_TEXTURE0);
    BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (w != width || h != height) {
        width = w; height = h; valid = false; frameHistoryCount = 0;
        allocatedHistoryCount = 0; scratchAllocated = false;
    }
    if (neededHistory > allocatedHistoryCount) {
        for (int i = allocatedHistoryCount; i < neededHistory; ++i) {
            BindTexture(GL_TEXTURE_2D, historyTextures[i]);
            TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        allocatedHistoryCount = neededHistory;
    }
    if ((averageMode || screenBlur) && !scratchAllocated) {
        BindTexture(GL_TEXTURE_2D, scratchTexture);
        TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        scratchAllocated = true;
    }
    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GLint readBuffer, drawBuffer;
    GetIntegerv(GL_READ_BUFFER, &readBuffer);
    GetIntegerv(GL_DRAW_BUFFER0, &drawBuffer);
    ReadBuffer(GL_BACK);
    GLenum back = GL_BACK;
    DrawBuffers(1, &back);
    if (averageMode) {
        // Preserve the raw frame before drawing any history over it.
        ActiveTexture(GL_TEXTURE0);
        BindTexture(GL_TEXTURE_2D, scratchTexture);
        CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    }
    if (valid) {
        float sampleWeights[maxHistoryFrames]{};
        float historyTotal = 0.0f;
        float weight;
        int samples;
        if (adaptiveMode) {
            long long targetNs = 1000000000LL / averageHz;
            long long currentNs = frameHistoryCount > 0 && frameTimestampNs > historyTimestamps[0]
                ? frameTimestampNs - historyTimestamps[0] : 0;
            if (currentNs > targetNs) currentNs = targetNs;
            long long remaining = targetNs - currentNs;
            samples = 0;
            for (int i = 0; i < frameHistoryCount && i < maxHistoryFrames && remaining > 0; ++i) {
                long long duration = i == 0 ? frameTimestampNs - historyTimestamps[0]
                                            : historyTimestamps[i - 1] - historyTimestamps[i];
                if (duration <= 0) continue;
                long long used = duration < remaining ? duration : remaining;
                sampleWeights[i] = static_cast<float>(used);
                historyTotal += sampleWeights[i];
                remaining -= used;
                samples = i + 1;
            }
            weight = historyTotal / (historyTotal + static_cast<float>(currentNs));
        } else {
            float historyAmount = averageMode ? averageFrames - 1.0f : 1.0f;
            if (averageMode && historyAmount > frameHistoryCount)
                historyAmount = static_cast<float>(frameHistoryCount);
            samples = static_cast<int>(historyAmount);
            if (static_cast<float>(samples) < historyAmount) ++samples;
            if (!averageMode) samples = 1;
            if (samples > frameHistoryCount) samples = frameHistoryCount;
            for (int i = 0; i < samples; ++i) {
                sampleWeights[i] = averageMode ? historyAmount - i : 1.0f;
                if (sampleWeights[i] < 0.0f) sampleWeights[i] = 0.0f;
                if (sampleWeights[i] > 1.0f) sampleWeights[i] = 1.0f;
                historyTotal += sampleWeights[i];
            }
            weight = averageMode ? historyTotal / (historyTotal + 1.0f)
                                 : (strength > 0.8f ? 0.8f : strength);
        }
        for (int i = 0; i < maxHistoryFrames; ++i) {
            ActiveTexture(GL_TEXTURE0 + i);
            BindSampler(i, 0);
            BindTexture(GL_TEXTURE_2D, historyTextures[i < samples ? i : 0]);
        }
        if (samples > 0) {
            for (GLenum cap : SavedState::caps) Disable(cap);
            Enable(GL_BLEND);
            BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            Viewport(0, 0, w, h);
            UseProgram(program);
            Uniform1i(spatialUniform, 0);
            for (int i = 0; i < maxHistoryFrames; ++i) Uniform1i(historyUniforms[i], i);
            Uniform1i(frameCountUniform, samples);
            Uniform1f(weightUniform, weight);
            Uniform1f(historyTotalUniform, historyTotal);
            for (int i = 0; i < maxHistoryFrames; ++i) Uniform1f(historyWeightUniforms[i], sampleWeights[i]);
            BindVertexArray(vao);
            DrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    if (temporal && averageMode) {
        int ringSize = allocatedHistoryCount;
        GLuint oldest = historyTextures[ringSize - 1];
        for (int i = ringSize - 1; i > 0; --i) historyTextures[i] = historyTextures[i - 1];
        historyTextures[0] = scratchTexture;
        scratchTexture = oldest;
        for (int i = ringSize - 1; i > 0; --i) historyTimestamps[i] = historyTimestamps[i - 1];
        historyTimestamps[0] = frameTimestampNs;
        if (frameHistoryCount < ringSize) ++frameHistoryCount;
    } else if (temporal) {
        // Save the blended image before the launcher's client menu is drawn.
        ActiveTexture(GL_TEXTURE0);
        BindTexture(GL_TEXTURE_2D, historyTextures[0]);
        CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        historyTimestamps[0] = frameTimestampNs;
        frameHistoryCount = 1;
    }
    valid = temporal;
    if (screenBlur) {
        // Blur the finished game frame, including inventory and HUD. The
        // launcher draws its own menu afterward, so the toggle stays readable.
        ActiveTexture(GL_TEXTURE0);
        BindSampler(0, 0);
        BindTexture(GL_TEXTURE_2D, scratchTexture);
        CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        for (GLenum cap : SavedState::caps) Disable(cap);
        ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        Viewport(0, 0, w, h);
        UseProgram(program);
        Uniform1i(historyUniforms[0], 0);
        Uniform1i(spatialUniform, 1);
        Uniform1f(spatialScaleUniform, 1.0f);
        Uniform1f(cornerRadiusUniform, 0.0f);
        Uniform1f(borderWidthUniform, 0.0f);
        Uniform1f(panelAlphaUniform, 1.0f);
        Uniform3f(tintColorUniform, 0.0f, 0.0f, 0.0f);
        Uniform1f(tintAmountUniform, 0.0f);
        BindVertexArray(vao);
        DrawArrays(GL_TRIANGLES, 0, 3);
    }
    ReadBuffer(readBuffer);
    GLenum previousDraw = static_cast<GLenum>(drawBuffer);
    DrawBuffers(1, &previousDraw);
}
