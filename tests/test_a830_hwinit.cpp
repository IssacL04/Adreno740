/* SPDX-License-Identifier: MIT */
/* Register peer and immediate software CP fences; does not execute firmware. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoA830HwInit.h"
#include "NativeAdrenoA830Regs.h"
#include <cstdio>
#include <map>
#include <set>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static uint32_t ringmem[8192],fencemem[1024];
alignas(8) static uint8_t pwrup[4096];
struct Peer {
    std::map<uint64_t,uint32_t> regs;
    std::set<uint32_t> banked;
    uint32_t aperture=0,reads=0,writes=0,fail_read=0,fail_write=0,ignored=~0u;
    uint32_t last_wptr=0,secure_packets=0,restore_published=0;
    bool authenticated=false,secure=false,hang=false,clock_stuck=false,publish_fail=false;
    IOReturn auth_result=0;
    uint64_t time=0;
    uint64_t key(uint32_t reg) const {return uint64_t(banked.count(reg)?(aperture?aperture:1):0)<<32|reg;}
};
static Peer peer;
static int write32(void *,uint32_t reg,uint32_t v)
{
    if(++peer.writes==peer.fail_write)return 1;
    if(reg==peer.ignored)return 0;
    if(reg==A830_CP_APERTURE_CNTL_HOST)peer.aperture=v>>12;
    peer.regs[peer.key(reg)]=v;
    if(reg==A830_RB_GC_GMEM_PROTECT)CHECK(peer.authenticated && peer.secure && v==0x0c000000);
    if(reg==A830_RBBM_CGC_P2S_TRIG_CMD && !peer.clock_stuck)peer.regs[A830_RBBM_CGC_P2S_STATUS]=1;
    if(reg==0x807 && v) {
        for(unsigned i=peer.last_wptr;i<v;++i)if((ringmem[i]>>28)==7 && ((ringmem[i]>>16)&127)==0x66) {
            CHECK(peer.authenticated);++peer.secure_packets;peer.secure=true;
        }
        if(!peer.hang) {fencemem[1]=ringmem[v-1];peer.regs[0x806]=v;}
        peer.last_wptr=v;
    }
    return 0;
}
static int read32(void *,uint32_t reg,uint32_t *v)
{if(++peer.reads==peer.fail_read)return 1;*v=peer.regs[peer.key(reg)];return 0;}
static int barrier(void *){return 0;}
static int publish(void *,const volatile uint32_t *words,size_t bytes)
{
    if(reinterpret_cast<const volatile void *>(words)==pwrup) {
        CHECK(bytes>1000 && bytes<=sizeof pwrup && peer.secure);++peer.restore_published;
        return peer.publish_fail;
    }
    CHECK(words==ringmem && bytes==sizeof ringmem);return 0;
}
static int acquire(void *,volatile uint32_t *words,size_t bytes)
{CHECK(words==fencemem && bytes==sizeof fencemem);return 0;}
static uint64_t now(void *){return ++peer.time;}
static IOReturn authenticate(void *)
{CHECK(!peer.authenticated);if(!peer.auth_result)peer.authenticated=true;return peer.auth_result;}
static const NativeAdrenoRingIo io{nullptr,write32,read32,barrier,nullptr,nullptr};
static const NativeAdrenoRingCache cache{nullptr,publish,acquire};
static const NativeAdrenoRingClock clock_{nullptr,now,nullptr};
static void reset()
{
    peer={};std::memset(ringmem,0,sizeof ringmem);std::memset(fencemem,0,sizeof fencemem);std::memset(pwrup,0,sizeof pwrup);
    for(const auto &row:NativeAdrenoA830Dynamic)peer.banked.insert(row[0]);
    peer.banked.insert(A830_CP_APRIV_CNTL_PIPE);
}
static void start(NativeAdrenoRing &ring,NativeAdrenoCpInit &cp,NativeAdrenoA830HwInit &hw)
{
    CHECK(!ring.attach(&io,&cache,ringmem,sizeof ringmem,0x200000,fencemem,sizeof fencemem,0x300000,NativeAdrenoChipA830));
    CHECK(!hw.configure(&io,&io,16,0x100000,0x180000));
    NativeAdrenoCpInitConfig c{0x100000,0x301000,1,0x180000};uint32_t fence;
    CHECK(!cp.run(&io,&ring,&c,&fence));
    CHECK(ring.wait(fence,&clock_,1000,nullptr)==NativeAdrenoRingFenceComplete);
}
static IOReturn finish(NativeAdrenoRing &ring,NativeAdrenoCpInit &cp,NativeAdrenoA830HwInit &hw)
{return hw.finish(&ring,&cp,&clock_,1000,authenticate,nullptr,pwrup,sizeof pwrup,&cache);}
int main()
{
    reset();NativeAdrenoA830HwInit prepared;
    CHECK(!prepared.configure(&io,&io,16,0x100000,0x180000));
    CHECK(!prepared.ready() && !peer.secure && peer.regs[0x815]==1);
    CHECK(peer.regs[A830_UCHE_CCHE_GC_GMEM_RANGE_MIN]==0x4000000);
    CHECK(peer.regs[(UINT64_C(1)<<32)|A830_GRAS_NC_MODE_CNTL]==0x60);
    CHECK(peer.regs[(UINT64_C(1)<<32)|A830_CP_PROTECT_CNTL_PIPE]==0xffff000b);
    CHECK(peer.regs[A830_CP_PROTECT_GLOBAL+63]==0x80027c02 && peer.regs[A830_CP_PROTECT_GLOBAL+47]==0);
    const unsigned config_reads=peer.reads,config_writes=peer.writes;
    for(unsigned i=1;i<=config_reads;++i) {
        reset();peer.fail_read=i;NativeAdrenoA830HwInit hw;
        CHECK(hw.configure(&io,&io,16,0x100000,0x180000)==kIOReturnIOError && hw.poisoned() && peer.reads==i);
    }
    for(unsigned i=1;i<=config_writes;++i) {
        reset();peer.fail_write=i;NativeAdrenoA830HwInit hw;
        CHECK(hw.configure(&io,&io,16,0x100000,0x180000)==kIOReturnIOError && hw.poisoned() && peer.writes==i);
    }
    reset();peer.ignored=A830_CP_PROTECT_GLOBAL+2;NativeAdrenoA830HwInit rejected;
    CHECK(rejected.configure(&io,&io,16,0x100000,0x180000)==kIOReturnIOError && !peer.regs[0x815]);
    for(unsigned failure=0;failure<5;++failure) {
        reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;start(ring,cp,hw);
        if(failure==1)peer.auth_result=kIOReturnIOError;
        if(failure==2)peer.hang=true;
        if(failure==3)peer.clock_stuck=true;
        if(failure==4)peer.publish_fail=true;
        const auto result=finish(ring,cp,hw);
        if(!failure) {
            CHECK(!result && hw.ready() && hw.result().stage==8 && peer.secure_packets==1 && peer.restore_published==1);
            CHECK(pwrup[12]==sizeof(NativeAdrenoA830Ifpc)/4 && pwrup[13]==sizeof(NativeAdrenoA830Pwrup)/4);
            CHECK(pwrup[14]>28 && !pwrup[15] && !peer.aperture);
            CHECK(peer.regs[A830_RBBM_CLOCK_CNTL_GLOBAL]==0);
        } else {
            CHECK(result && hw.poisoned() && !hw.ready());
            if(failure==1)CHECK(!peer.secure_packets && !peer.restore_published);
            if(failure==2)CHECK(!peer.restore_published);
        }
    }
    std::printf("PASS: A830 protected initialization, %u read/%u write failure points, authenticated secure transition, restore image and bounded failures; software CP only\n",config_reads,config_writes);
}
