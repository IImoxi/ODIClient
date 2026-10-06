#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/mman.h>
#include <time.h>
#include "../gpu_uniform_cache.cpp"
using Matrix = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
static EGLContext mockContextValue=reinterpret_cast<EGLContext>(1);
static unsigned long mockThreadValue=1;
static GLint mockProgram=7;
static int forwards[9], queries, contextReads;
static EGLBoolean mockMakeCurrentResult=EGL_TRUE;
static EGLBoolean mockMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext context) {
    ++forwards[8]; if (mockMakeCurrentResult) mockContextValue=context; return mockMakeCurrentResult;
}
static EGLContext mockContext() { ++contextReads; return mockContextValue; }
static unsigned long mockThread() { return mockThreadValue; }
static void mockGetInteger(GLenum name,GLint* value) { assert(name==GL_CURRENT_PROGRAM); *value=mockProgram; ++queries; }
static void mockMatrix4(GLint,GLsizei,GLboolean,const GLfloat*) { ++forwards[0]; }
static void mockLink(GLuint) { ++forwards[1]; }
static void mockDelete(GLuint) { ++forwards[2]; }
static void mockVector(GLint,GLsizei,const GLfloat*) { ++forwards[3]; }
static void mockMatrix3(GLint,GLsizei,GLboolean,const GLfloat*) { ++forwards[4]; }
static void mockScalar(GLint,GLint) { ++forwards[5]; }
static void mockUse(GLuint program) { mockProgram=program; ++forwards[6]; }
static void mockIntegers(GLint,GLsizei,const GLint*) { ++forwards[7]; }
static __eglMustCastToProperFunctionPointerType mockProc(const char* name) {
    assert(std::strcmp(name,"glGetIntegerv")==0);
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(mockGetInteger);
}
extern "C" void* mcpelauncher_host_dlopen(const char* name,int flags) { return dlopen(name,flags); }
extern "C" void* mcpelauncher_host_dlsym(void* library,const char* name) {
    if (std::strcmp(name,"eglGetCurrentContext")==0) return reinterpret_cast<void*>(mockContext);
    if (std::strcmp(name,"eglGetProcAddress")==0) return reinterpret_cast<void*>(mockProc);
    if (std::strcmp(name,"pthread_self")==0) return reinterpret_cast<void*>(mockThread);
    return dlsym(library,name);
}
static void mocks() {
    assert(hooks::initialize());
    auto size=minecraft_build::current::buildNote+4096;
    auto image=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image!=MAP_FAILED); auto base=reinterpret_cast<unsigned long>(image);
    const unsigned char note[]={4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,minecraft_build::current::buildId,20);
    const unsigned long functions[]={reinterpret_cast<unsigned long>(mockMatrix4),reinterpret_cast<unsigned long>(mockLink),reinterpret_cast<unsigned long>(mockDelete),reinterpret_cast<unsigned long>(mockVector),reinterpret_cast<unsigned long>(mockMatrix3),reinterpret_cast<unsigned long>(mockScalar),reinterpret_cast<unsigned long>(mockUse),reinterpret_cast<unsigned long>(mockIntegers),reinterpret_cast<unsigned long>(mockMakeCurrent)};
    for (unsigned int i=0;i<9;++i) {
        std::memcpy(image+profile::plt[i],profile::pltSignatures[i],6);
        *reinterpret_cast<unsigned long*>(image+profile::slots[i])=functions[i];
    }
    image[profile::plt[8]]^=1;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0); assert(!gpu_uniform_cache_install(base));
    assert(mprotect(image,size,PROT_READ|PROT_WRITE)==0); image[profile::plt[8]]^=1;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0); assert(gpu_uniform_cache_install(base)); assert(!gpu_uniform_cache_install(base));
    auto vec=reinterpret_cast<Vector>(*reinterpret_cast<unsigned long*>(image+profile::slots[3]));
    GLfloat v[16]={1,2,3,4};
    vec(2,1,v); vec(2,1,v); assert(forwards[3]==2 && queries==0); // OFF forwards without querying GL.
    gpu_uniform_cache_trace_enable(true); client_set_uniform_cache(true);
    vec(2,1,v); assert(forwards[3]==3); // No frame ownership yet.
    gpu_uniform_cache_frame(); assert(gpu_uniform_cache_available());
    auto beforeUploads=contextReads;
    vec(2,1,v); vec(2,1,v); assert(forwards[3]==4 && queries==1);
    assert(contextReads==beforeUploads); // No EGL context query in the upload fast path.
    v[0]=5; vec(2,1,v); assert(forwards[3]==5); vec(2,1,v); assert(forwards[3]==5);
    auto rawMatrix4=reinterpret_cast<Matrix>(*reinterpret_cast<unsigned long*>(image+profile::slots[0]));
    auto rawMatrix3=reinterpret_cast<Matrix>(*reinterpret_cast<unsigned long*>(image+profile::slots[4]));
    assert(reinterpret_cast<unsigned long>(rawMatrix4)==functions[0] && reinterpret_cast<unsigned long>(rawMatrix3)==functions[4]);
    rawMatrix4(3,1,GL_FALSE,v); rawMatrix4(3,1,GL_FALSE,v); assert(forwards[0]==2);
    rawMatrix3(4,1,GL_FALSE,v); rawMatrix3(4,1,GL_FALSE,v); assert(forwards[4]==2);
    uniform1i(5,9); GLint scalar=9; uniform1iv(5,1,&scalar); assert(forwards[5]==1 && forwards[7]==0);
    vec(2,2,v); vec(2,1,v); assert(forwards[3]==7); // Array update invalidates all entries, including aliases.
    vec(2,1,v); assert(forwards[3]==7);
    useProgram(8); vec(2,1,v); assert(forwards[3]==8 && queries==3);
    linkProgram(8); vec(2,1,v); assert(forwards[3]==9);
    deleteProgram(8); vec(2,1,v); assert(forwards[3]==10);
    mockThreadValue=2; vec(2,1,v); assert(forwards[3]==11);
    mockThreadValue=1; vec(2,1,v); assert(forwards[3]==12);
    mockContextValue=reinterpret_cast<EGLContext>(2); gpu_uniform_cache_frame(); assert(!gpu_uniform_cache_available()); vec(2,1,v); assert(forwards[3]==13);
    mockContextValue=reinterpret_cast<EGLContext>(1); gpu_uniform_cache_frame(); assert(gpu_uniform_cache_available()); vec(2,1,v); assert(forwards[3]==14);
    auto switchContext=reinterpret_cast<decltype(&eglMakeCurrent)>(*reinterpret_cast<unsigned long*>(image+profile::slots[8]));
    assert(switchContext(EGL_NO_DISPLAY,EGL_NO_SURFACE,EGL_NO_SURFACE,reinterpret_cast<EGLContext>(2)));
    assert(!gpu_uniform_cache_available()); vec(2,1,v); assert(forwards[3]==15);
    assert(switchContext(EGL_NO_DISPLAY,EGL_NO_SURFACE,EGL_NO_SURFACE,reinterpret_cast<EGLContext>(1)));
    gpu_uniform_cache_frame(); assert(gpu_uniform_cache_available());
    mockMakeCurrentResult=EGL_FALSE;
    assert(!switchContext(EGL_NO_DISPLAY,EGL_NO_SURFACE,EGL_NO_SURFACE,reinterpret_cast<EGLContext>(2)));
    assert(mockContextValue==reinterpret_cast<EGLContext>(1));
    mockMakeCurrentResult=EGL_TRUE;
    gpu_uniform_cache_frame(); vec(2,1,v); assert(forwards[3]==16);
    client_set_uniform_cache(false); vec(2,1,v); assert(forwards[3]==17);
    client_set_uniform_cache(true); vec(2,1,v); assert(forwards[3]==18);
    rawMatrix4(3,1,GL_TRUE,v); rawMatrix4(3,1,GL_TRUE,v); assert(forwards[0]==4);
    vec(-1,1,v); vec(-1,1,v); assert(forwards[3]==20);
    GLfloat zeros[4]={0,0,0,1}; auto beforeExact=forwards[3];
    vec(9,1,zeros); vec(9,1,zeros); assert(forwards[3]==beforeExact+1);
    zeros[0]=-0.0f; vec(9,1,zeros); assert(forwards[3]==beforeExact+2); // Bit-exact comparison preserves signed zero.
    mockProgram=0; gpu_uniform_cache_frame(); vec(2,1,v); vec(2,1,v); assert(forwards[3]==beforeExact+4);
    auto snapshot=gpu_uniform_cache_snapshot(); assert(snapshot.calls>snapshot.skipped && snapshot.skipped>=4 && snapshot.skipped_bytes>=36);
    assert(snapshot.eligible>=snapshot.skipped && snapshot.owner_rejected>=3 && snapshot.uncacheable>=5);
    gpu_uniform_cache_trace_enable(false); vec(2,1,v); assert(gpu_uniform_cache_snapshot().calls==0);
    puts("Uniform reuse import/epoch/context/lifecycle checks passed");
}
static GLuint shader(GLenum type,const char* source) {
    auto result=glCreateShader(type); glShaderSource(result,1,&source,nullptr); glCompileShader(result);
    GLint ok=0; glGetShaderiv(result,GL_COMPILE_STATUS,&ok); assert(ok); return result;
}
static long long nanoseconds() {
    timespec value; assert(clock_gettime(CLOCK_MONOTONIC,&value)==0);
    return value.tv_sec*1000000000LL+value.tv_nsec;
}
static void benchmark(GLuint program,GLint color,GLint transform,GLint marker,const GLfloat* matrix,const GLfloat* value) {
    constexpr int repetitions=204800, frameCalls=4096;
    const char* names[]={"4fv","Matrix4fv_untouched","1i"};
    puts("GLES CPU submission benchmark: median of 3 x 204800 calls; binds/frame reset every 4096 calls");
    printf("Renderer: %s\n",glGetString(GL_RENDERER));
    for (int scenario=0;scenario<2;++scenario) {
    puts(scenario==0 ? "Identical values" : "Values change every seventh call (85.7% duplicates)");
    puts("API raw_ns OFF_no_trace_ns ON_no_trace_ns OFF_trace_ns ON_trace_ns");
    for (int api=0;api<3;++api) {
        double results[5];
        for (int mode=0;mode<5;++mode) {
            client_set_uniform_cache(mode==2 || mode==4); gpu_uniform_cache_trace_enable(mode>=3);
            auto rawVector=reinterpret_cast<Vector>(originals[3]);
            auto rawMatrix=reinterpret_cast<Matrix>(originals[0]);
            auto rawScalar=reinterpret_cast<Scalar>(originals[5]);
            double samples[3];
            for (int sample=0;sample<3;++sample) {
                glFinish(); gpu_uniform_cache_snapshot();
                GLfloat uploadedVector[4],uploadedMatrix[16];
                std::memcpy(uploadedVector,value,sizeof(uploadedVector));
                std::memcpy(uploadedMatrix,matrix,sizeof(uploadedMatrix));
                GLint uploadedInteger=1; bool alternate=false;
                auto start=nanoseconds();
                for (int call=0;call<repetitions;++call) {
                    if ((call&(frameCalls-1))==0) {
                        if (mode==0) glUseProgram(program); else { gpu_uniform_cache_frame(); useProgram(program); }
                    }
                    if (scenario && call%7==0) {
                        alternate=!alternate; uploadedVector[0]=alternate?0.5f:0.0f;
                        uploadedMatrix[0]=alternate?0.9f:1.0f; uploadedInteger=alternate?2:1;
                    }
                    if (api==0) { if (mode==0) rawVector(color,1,uploadedVector); else vector4(color,1,uploadedVector); }
                    else if (api==1) { if (mode==0) rawMatrix(transform,1,GL_FALSE,uploadedMatrix); else rawMatrix(transform,1,GL_FALSE,uploadedMatrix); }
                    else { if (mode==0) rawScalar(marker,uploadedInteger); else uniform1i(marker,uploadedInteger); }
                }
                samples[sample]=static_cast<double>(nanoseconds()-start)/repetitions;
                assert(glGetError()==GL_NO_ERROR);
            }
            if (samples[0]>samples[1]) { auto t=samples[0];samples[0]=samples[1];samples[1]=t; }
            if (samples[1]>samples[2]) { auto t=samples[1];samples[1]=samples[2];samples[2]=t; }
            if (samples[0]>samples[1]) { auto t=samples[0];samples[0]=samples[1];samples[1]=t; }
            results[mode]=samples[1];
        }
        printf("%s %.2f %.2f %.2f %.2f %.2f\n",names[api],results[0],results[1],results[2],results[3],results[4]);
    }
    }
    gpu_uniform_cache_snapshot(); gpu_uniform_cache_trace_enable(false);
}
static void pixels(bool runBenchmark) {
    auto getPlatform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    assert(getPlatform); auto display=getPlatform(0x31dd,EGL_DEFAULT_DISPLAY,nullptr); assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,nullptr,nullptr)); assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count; assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    const EGLint surfaceAttrs[]={EGL_WIDTH,4,EGL_HEIGHT,4,EGL_NONE}; auto surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE}; auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    assert(eglMakeCurrent(display,surface,surface,context));
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    ownerContext=0; ownerThread=0; ownerEstablished=false;
    getContext=eglGetCurrentContext; getInteger=glGetIntegerv; threadSelf=reinterpret_cast<unsigned long(*)()>(pthread_self);
    originals[0]=reinterpret_cast<unsigned long>(glUniformMatrix4fv); originals[1]=reinterpret_cast<unsigned long>(glLinkProgram);
    originals[2]=reinterpret_cast<unsigned long>(glDeleteProgram); originals[3]=reinterpret_cast<unsigned long>(glUniform4fv);
    originals[4]=reinterpret_cast<unsigned long>(glUniformMatrix3fv); originals[5]=reinterpret_cast<unsigned long>(glUniform1i);
    originals[6]=reinterpret_cast<unsigned long>(glUseProgram); originals[7]=reinterpret_cast<unsigned long>(glUniform1iv);
    originals[8]=reinterpret_cast<unsigned long>(eglMakeCurrent);
    gpu_uniform_cache_frame(); client_set_uniform_cache(true); gpu_uniform_cache_trace_enable(true);
    auto program=glCreateProgram();
    auto vs=shader(GL_VERTEX_SHADER,"#version 300 es\nuniform mat4 transform;void main(){vec2 p=vec2((gl_VertexID==1)?3.0:-1.0,(gl_VertexID==2)?3.0:-1.0);gl_Position=transform*vec4(p,0,1);}");
    auto fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;uniform vec4 color;uniform int marker;out vec4 result;void main(){result=color*float(marker);}");
    glAttachShader(program,vs);glAttachShader(program,fs);linkProgram(program);
    GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);useProgram(program);
    auto marker=glGetUniformLocation(program,"marker"); assert(marker>=0); uniform1i(marker,1);
    auto color=glGetUniformLocation(program,"color"), transform=glGetUniformLocation(program,"transform");assert(color>=0 && transform>=0);
    GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; GLfloat red[4]={1,0,0,1}, green[4]={0,1,0,1};
    glUniformMatrix4fv(transform,1,GL_FALSE,identity);glUniformMatrix4fv(transform,1,GL_FALSE,identity);
    vector4(color,1,red);vector4(color,1,red);
    GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,4,4);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char pixel[4];glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==255 && pixel[1]==0);
    vector4(color,1,green);vector4(color,1,green);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0 && pixel[1]==255);
    gpu_uniform_cache_frame();vector4(color,1,green);auto snapshot=gpu_uniform_cache_snapshot();assert(snapshot.skipped==2 && snapshot.skipped_bytes==32);
    client_set_uniform_cache(false);vector4(color,1,red);client_set_uniform_cache(true);vector4(color,1,green);
    glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[1]==255 && glGetError()==GL_NO_ERROR);
    if (runBenchmark) benchmark(program,color,transform,marker,identity,green);
    useProgram(0);deleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteVertexArrays(1,&vao);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));gpu_uniform_cache_frame();assert(!gpu_uniform_cache_available());
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
    puts("Uniform reuse real GLES uniform/pixel checks passed");
}
int main(int argc,char** argv) {
    mocks();
    if (argc>1 && (std::strcmp(argv[1],"--egl")==0 || std::strcmp(argv[1],"--benchmark")==0))
        pixels(std::strcmp(argv[1],"--benchmark")==0);
}
