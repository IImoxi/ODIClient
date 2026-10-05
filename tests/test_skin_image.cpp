#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <dlfcn.h>
#include "../skin_image.cpp"
static bool missingZlib = true, missingMaps = true;
static FILE* mappedOpen(const char* path,const char* mode) {
    if (std::strcmp(path,"/proc/self/maps")) return std::fopen(path,mode);
    if (missingMaps) return nullptr;
    FILE* file=std::tmpfile();assert(file);
    unsigned long self=reinterpret_cast<unsigned long>(&skin_image_init);
    std::fprintf(file,"%lx-%lx r-xp 00000000 00:00 1 /tmp/ODIClient/1.0.0/x86_64/libblank-client-menu.so\n",self,self+1);
    std::rewind(file);return file;
}
extern "C" void* mcpelauncher_host_dlopen(const char* name,int flags) {
    if(missingZlib && !std::strcmp(name,"libz.so.1")) return nullptr;
    return dlopen(name,flags);
}
extern "C" void* mcpelauncher_host_dlsym(void* lib,const char* name) {
    // Match the Android mod case: the host cannot discover its ELF with dladdr.
    if(!std::strcmp(name,"dladdr")) return nullptr;
    if(!std::strcmp(name,"fopen")) return reinterpret_cast<void*>(&mappedOpen);
    return dlsym(lib,name);
}
static unsigned int integer(const unsigned char* p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
int main() {
    assert(!skin_image_init());
    auto* pixels=skin_image_allocate(64*64*4);assert(pixels);skin_image_release(pixels);
    missingZlib=false;
    assert(skin_image_init() && !*directory);
    pixels=skin_image_allocate(64*64*4);assert(pixels);
    assert(!skin_image_save(pixels,64,64,"no-path"));skin_image_release(pixels);
    missingMaps=false;findDirectory();
    assert(!std::strcmp(directory,"/tmp/ODIClient/skins"));
    char temporary[]="/tmp/odiclient-skins-XXXXXX";assert(mkdtemp(temporary));
    std::snprintf(directory,sizeof(directory),"%s/skins",temporary);
    static unsigned char image[skin_image_max_bytes],png[skin_image_max_bytes+8192],raw[skin_image_max_bytes+256];
    auto unpack=reinterpret_cast<decltype(&uncompress)>(dlsym(dlopen("libz.so.1",2),"uncompress"));assert(unpack);
    for(int w : {64,128,256}) for(int h : {w,w/2}) {
        for(int i=0;i<w*h*4;++i) image[i]=static_cast<unsigned char>(i*37+i/13);
        assert(skin_image_save(image,w,h,"../Test Player"));
        for(int i=0;i<2000 && skin_image_save_status()==1;++i) usleep(1000);
        assert(skin_image_save_status()==2);
        assert(std::strstr(job.path,"/___Test_Player-"));
        FILE* file=std::fopen(job.path,"rb");assert(file);
        unsigned long bytes=std::fread(png,1,sizeof(png),file);std::fclose(file);
        const unsigned char magic[]={137,80,78,71,13,10,26,10};assert(!std::memcmp(png,magic,8));
        unsigned long pos=8;bool header=false,data=false,end=false;
        while(pos+12<=bytes) {
            unsigned int n=integer(png+pos);assert(pos+n+12<=bytes);
            assert(host_crc32(0,png+pos+4,n+4)==integer(png+pos+8+n));
            const auto* payload=png+pos+8;
            if(!std::memcmp(png+pos+4,"IHDR",4)) { assert(integer(payload)==static_cast<unsigned int>(w) && integer(payload+4)==static_cast<unsigned int>(h));header=true; }
            if(!std::memcmp(png+pos+4,"IDAT",4)) {
                unsigned long count=sizeof(raw);assert(unpack(raw,&count,payload,n)==Z_OK);
                assert(count==static_cast<unsigned long>((w*4+1)*h));
                for(int y=0;y<h;++y) { assert(raw[y*(w*4+1)]==0);assert(!std::memcmp(raw+y*(w*4+1)+1,image+y*w*4,w*4)); } data=true;
            }
            if(!std::memcmp(png+pos+4,"IEND",4)) end=true;
            pos+=n+12;
        }
        assert(header && data && end && pos==bytes);assert(unlink(job.path)==0);
    }
    assert(!skin_image_save(image,63,64,"invalid"));assert(!skin_image_save(nullptr,64,64,"invalid"));
    assert(rmdir(directory)==0 && rmdir(temporary)==0);
    std::puts("PASS: skin capture survives missing export dependencies/path, mapped Android ELF resolves mod root, and asynchronous PNG export preserves full RGBA/resolution, valid CRCs, legacy skins, sanitized names, and exclusive timestamped files");
}
