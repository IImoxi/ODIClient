#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../gpu_multidraw.cpp"

static EGLContext context = reinterpret_cast<EGLContext>(1);
static EGLContext mockContext() { return context; }
static bool extensionAvailable=true;
static const char* contextVersion="OpenGL ES 3.2 NVIDIA";
static GLint vao=1, indirect=1, indices=1, feedback=0;
static int arrayDraws, elementDraws, arrayMultis, elementMultis;
static const void* lastOffset;
static int lastCount, lastStride;
static const GLubyte* mockString(GLenum token) {
    assert(token == GL_VERSION); return reinterpret_cast<const GLubyte*>(contextVersion);
}
static const GLubyte* mockStringi(GLenum token, GLuint index) {
    assert(token == GL_EXTENSIONS && index == 0);
    return reinterpret_cast<const GLubyte*>(extensionAvailable ? "GL_EXT_multi_draw_indirect" : "other");
}
static void mockInteger(GLenum token, GLint* value) {
    switch (token) {
        case GL_NUM_EXTENSIONS: *value=1; break;
        case GL_VERTEX_ARRAY_BINDING: *value=vao; break;
        case GL_DRAW_INDIRECT_BUFFER_BINDING: *value=indirect; break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: *value=indices; break;
        case GL_TRANSFORM_FEEDBACK_ACTIVE: *value=feedback; break;
        default: assert(false);
    }
}
static void mockMultiArrays(unsigned int mode, const void* offset, int count, int stride) {
    assert(mode==4); ++arrayMultis; lastOffset=offset; lastCount=count; lastStride=stride;
}
static void mockMultiElements(unsigned int mode, unsigned int type, const void* offset, int count, int stride) {
    assert(mode==4 && type==0x1403); ++elementMultis; lastOffset=offset; lastCount=count; lastStride=stride;
}
static void singleArrays(unsigned int mode, const void* offset) {
    assert(mode==4); ++arrayDraws; lastOffset=offset;
}
static void singleElements(unsigned int mode, unsigned int type, const void* offset) {
    assert(mode==4 && type==0x1403); ++elementDraws; lastOffset=offset;
}
static __eglMustCastToProperFunctionPointerType mockProc(const char* name) {
#define PROC(n, fn) if (std::strcmp(name,n)==0) return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(fn)
    PROC("glGetString", mockString); PROC("glGetStringi", mockStringi); PROC("glGetIntegerv", mockInteger);
    PROC("glMultiDrawArraysIndirectEXT", mockMultiArrays); PROC("glMultiDrawElementsIndirectEXT", mockMultiElements);
#undef PROC
    return nullptr;
}
extern "C" void* mcpelauncher_host_dlopen(const char* name, int flags) {
    if (std::strcmp(name,"libEGL.so.1")==0) return reinterpret_cast<void*>(1);
    return dlopen(name,flags);
}
extern "C" void* mcpelauncher_host_dlsym(void* library, const char* name) {
    if (library==reinterpret_cast<void*>(1)) {
        if (std::strcmp(name,"eglGetCurrentContext")==0) return reinterpret_cast<void*>(mockContext);
        if (std::strcmp(name,"eglGetProcAddress")==0) return reinterpret_cast<void*>(mockProc);
        return nullptr;
    }
    return dlsym(library,name);
}
int main() {
    assert(hooks::initialize());
    auto size = minecraft_build::current::buildNote + 4096;
    auto image=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image!=MAP_FAILED);
    auto base=reinterpret_cast<unsigned long>(image);
    const unsigned char note[]={4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,minecraft_build::current::buildId,20);
    for (unsigned int i=0;i<2;++i) std::memcpy(image+profile::fallback[i],profile::fallbackSignatures[i],profile::fallbackSizes[i]);
    *reinterpret_cast<unsigned long*>(image+profile::singleSlots[0])=reinterpret_cast<unsigned long>(singleArrays);
    *reinterpret_cast<unsigned long*>(image+profile::singleSlots[1])=reinterpret_cast<unsigned long>(singleElements);
    image[profile::fallback[1]+0x29]^=1; // Reject a changed inner-loop call, not just changed entry bytes.
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    assert(!gpu_multidraw_install(base));
    assert(mprotect(image,size,PROT_READ|PROT_WRITE)==0);
    image[profile::fallback[1]+0x29]^=1;
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    assert(gpu_multidraw_install(base)); assert(!gpu_multidraw_install(base));
    auto callArrays=reinterpret_cast<Arrays>(base+profile::fallback[0]);
    auto callElements=reinterpret_cast<Elements>(base+profile::fallback[1]);
    callArrays(4,reinterpret_cast<void*>(64),4,32);
    assert(arrayDraws==4 && lastOffset==reinterpret_cast<void*>(160));
    callElements(4,0x1403,reinterpret_cast<void*>(64),4,32);
    assert(elementDraws==4 && lastOffset==reinterpret_cast<void*>(160));
    callArrays(4,nullptr,0,32); callElements(4,0x1403,nullptr,-1,32);
    assert(arrayDraws==4 && elementDraws==4);
    gpu_multidraw_frame(); assert(ready && gpu_multidraw_available());
    client_set_gpu_multidraw(true);
    callArrays(4,reinterpret_cast<void*>(64),4,32);
    callElements(4,0x1403,reinterpret_cast<void*>(96),8,32);
    assert(arrayMultis==1 && elementMultis==1 && lastOffset==reinterpret_cast<void*>(96) && lastCount==8 && lastStride==32);
    auto snapshot=gpu_multidraw_snapshot();
    assert(snapshot.array_batches==1 && snapshot.element_batches==1 && snapshot.commands==12 && snapshot.avoided_calls==10 && snapshot.fallback_batches==0);
    callArrays(4,reinterpret_cast<void*>(64),4,0); // Native loop repeats offset; EXT zero stride does not.
    assert(arrayDraws==8 && lastOffset==reinterpret_cast<void*>(64));
    callElements(4,0x1403,nullptr,1,32); assert(elementDraws==5);
    context=reinterpret_cast<EGLContext>(2);
    gpu_multidraw_frame(); assert(!gpu_multidraw_available());
    callArrays(4,nullptr,4,32); assert(arrayDraws==12);
    context=reinterpret_cast<EGLContext>(1);
    gpu_multidraw_frame(); assert(gpu_multidraw_available());
    feedback=1; callArrays(4,nullptr,4,32); assert(arrayDraws==16); feedback=0;
    vao=0; callArrays(4,nullptr,4,32); assert(arrayDraws==20); vao=1;
    indirect=0; callArrays(4,nullptr,4,32); assert(arrayDraws==24); indirect=1;
    indices=0; callElements(4,0x1403,nullptr,4,32); assert(elementDraws==9); indices=1;
    callArrays(4,reinterpret_cast<void*>(1),4,32); assert(arrayDraws==28);
    callArrays(4,nullptr,4,18); assert(arrayDraws==32);
    client_set_gpu_multidraw(false); callArrays(4,nullptr,4,32); assert(arrayDraws==36);
    snapshot=gpu_multidraw_snapshot(); assert(snapshot.fallback_batches==9);
    // Unsupported capability never enables replacement, while patched native loops remain callable.
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE); capabilityChecked=false; extensionAvailable=false;
    gpu_multidraw_frame(); assert(!ready);
    client_set_gpu_multidraw(true); callArrays(4,nullptr,4,32); assert(arrayDraws==40 && arrayMultis==1);
    capabilityChecked=false; contextVersion="4.6 NVIDIA"; extensionAvailable=true;
    gpu_multidraw_frame(); assert(!ready);
    capabilityChecked=false; contextVersion="OpenGL ES 3.0 NVIDIA";
    gpu_multidraw_frame(); assert(!ready);
    puts("GPU multidraw executable fallback and capability checks passed");
}
