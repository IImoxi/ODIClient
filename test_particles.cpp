#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include "particles.cpp"
extern "C" void* mcpelauncher_host_dlopen(const char* n, int f) { return dlopen(n,f); }
extern "C" void* mcpelauncher_host_dlsym(void* l, const char* n) { return dlsym(l,n); }
static bool saved;
bool client_settings_get_particles() { return saved; }
void client_settings_set_particles(bool v) { saved = v; }
static int attacks, emissions;
static void *expectedMode, *expectedTarget, *expectedPlayer;
static const void* expectedItem;
static bool result;
static bool nativeAttack(void* mode, void* target, const void* item) {
    assert(mode == expectedMode && target == expectedTarget && item == expectedItem);
    ++attacks; return result;
}
static void nativeEmit(void* player, void* target, int count) {
    assert(player == expectedPlayer && target == expectedTarget && count == 16);
    ++emissions;
}
template<class T> void put(void* o, unsigned long off, T v) {
    std::memcpy(static_cast<unsigned char*>(o)+off,&v,sizeof(v));
}
int main() {
    originals[0] = originals[1] = nativeAttack; emit = nativeEmit;
    gameBase = 0x100000000;
    alignas(8) unsigned char player[0xc90]{}, target[8]{}, mode[16]{}, item[8]{};
    put(player,0,gameBase+profile::localPlayerTable);
    put(player,profile::localParticleContext,player);
    put(target,0,gameBase+profile::remotePlayerTable);
    put(mode,profile::modePlayer,player);
    expectedMode=mode; expectedTarget=target; expectedPlayer=player; expectedItem=item;
    ready=true; particles_update(true);
    assert(!client_particles_enabled() && !baseAttack(mode,target,item) && emissions==0);
    client_set_particles(true); assert(saved); result=true;
    assert(baseAttack(mode,target,item) && emissions==1 && attacks==2);
    assert(survivalAttack(mode,target,item) && emissions==2 && attacks==3);
    result=false; assert(!baseAttack(mode,target,item) && emissions==3); // Effect follows attack attempts.
    particles_update(false); baseAttack(mode,target,item); assert(emissions==3);
    particles_update(true); ready=false; baseAttack(mode,target,item); assert(emissions==3);
    ready=true; put(target,0,gameBase+profile::remotePlayerTable+8);
    baseAttack(mode,target,item); assert(emissions==3); // Mobs/unknown classes.
    put(target,0,gameBase+profile::remotePlayerTable);
    put(player,0,gameBase+profile::remotePlayerTable);
    baseAttack(mode,target,item); assert(emissions==3); // Integrated-server GameMode.
    put(player,0,gameBase+profile::localPlayerTable);
    put(player,profile::localParticleContext,static_cast<void*>(nullptr));
    baseAttack(mode,target,item); assert(emissions==3);
    put(player,profile::localParticleContext,player);
    expectedTarget=player; baseAttack(mode,player,item); assert(emissions==3);
    expectedTarget=nullptr; baseAttack(mode,nullptr,item); assert(emissions==3);
    expectedTarget=target; expectedMode=nullptr; baseAttack(nullptr,target,item); assert(emissions==3);
    expectedMode=mode;
    client_set_particles(false); baseAttack(mode,target,item); assert(emissions==3 && !saved);

    assert(hooks::initialize());
    unsigned long size=minecraft_build::current::buildNote+4096;
    auto image=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(image!=MAP_FAILED);
    auto base=reinterpret_cast<unsigned long>(image);
    unsigned int note[]={4,20,3,0x00554e47};
    std::memcpy(image+minecraft_build::current::buildNote,note,sizeof(note));
    std::memcpy(image+minecraft_build::current::buildNote+16,
                minecraft_build::current::buildId,20);
    std::memcpy(image+profile::attackFunctions[0],profile::attackEntry,sizeof(profile::attackEntry));
    std::memcpy(image+profile::attackFunctions[1],profile::survivalAttackEntry,sizeof(profile::survivalAttackEntry));
    std::memcpy(image+profile::attackPlayerSite,profile::attackPlayerRead,sizeof(profile::attackPlayerRead));
    std::memcpy(image+profile::criticalEmitter,profile::criticalEntry,sizeof(profile::criticalEntry));
    std::memcpy(image+profile::emitterBody,profile::emitterEntry,sizeof(profile::emitterEntry));
    for (int i=0;i<2;++i) put(image,profile::attackTables[i]+profile::attackSlot,base+profile::attackFunctions[i]);
    put(image,profile::localPlayerTable+profile::criticalSlot,base+profile::criticalEmitter);
    // Verified attack thunks branch into the common implementation with edx=1.
    // A mock there verifies that the original actor/item arguments still arrive.
    auto jump=[&](unsigned long off, unsigned long dest) {
        image[off]=0x48; image[off+1]=0xb8; put(image,off+2,dest);
        image[off+10]=0xff; image[off+11]=0xe0;
    };
    // Common implementation receives item in rcx; restore rdx for the mock.
    // Both verified wrappers jump to baseAttack+16.
    image[profile::attackFunctions[0]+16]=0x48;
    image[profile::attackFunctions[0]+17]=0x89;
    image[profile::attackFunctions[0]+18]=0xca;
    jump(profile::attackFunctions[0]+19,reinterpret_cast<unsigned long>(&nativeAttack));
    assert(mprotect(image,size,PROT_READ|PROT_EXEC)==0);
    auto corrupt=[&](unsigned long off) {
        auto page=image+(off&~(hooks::page_size()-1));
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_WRITE)==0);
        image[off]^=1;
        assert(mprotect(page,hooks::page_size(),PROT_READ|PROT_EXEC)==0);
    };
    unsigned long gates[]={minecraft_build::current::buildNote+16,profile::attackFunctions[0],
        profile::attackFunctions[1],profile::attackPlayerSite,profile::criticalEmitter,profile::emitterBody,
        profile::attackTables[0]+profile::attackSlot,profile::attackTables[1]+profile::attackSlot,
        profile::localPlayerTable+profile::criticalSlot};
    for (auto off:gates) { corrupt(off); assert(!install(base)); corrupt(off); }
    assert(install(base)); ready=false; result=true;
    for (int i=0;i<2;++i) {
        auto callback=field<Attack>(image,profile::attackTables[i]+profile::attackSlot);
        assert(callback(mode,target,item)); // Inactive/Retained hooks always forward.
    }
    std::puts("PASS: Particles targets, gameplay/enable gates, saved toggle, original ABI/returns, and executable hook/signature checks");
}
