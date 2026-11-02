/* SPDX-License-Identifier: MIT */
/* Real allocator, SMMU tables and VM with software registers; no hardware DMA. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoGpuContext.h"
#include "ArmSmmu4KMmio.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
extern "C" {volatile uint32_t native_adreno_wc_enabled,native_adreno_table_switches[8];}
extern "C" int PE_parse_boot_argn(const char *,void *,int) {return 0;}
extern "C" unsigned int native_pmap_user_mappings(ppnum_t,void **,uint64_t *,char *) {return 0;}
void NativeAdrenoNotePhase(unsigned,size_t) {}
bool NativeAdrenoPagesUncached(const NativeAdrenoPhysicalSpan *,size_t) {return false;}
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *s) {*s={};return 0;}
IOReturn NativeAdrenoMaintainOwnedPages(void *,size_t,NativeAdrenoCacheOperation) {return 0;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t,size_t,NativeAdrenoCacheOperation) {return 0;}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *,size_t,bool) {return 0;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *,size_t,uint32_t *n) {*n=0;return 0;}
extern "C" ArmSmmu4KStatus ArmSmmu4KBindMmio(ArmSmmu4KMmio *,ArmSmmu4KIo *) {return ArmSmmu4KUnsupported;}
static bool bad_cbar;
alignas(8) static uint32_t regs[0x40000/4];
static int read32(void *,size_t at,uint32_t *v) {CHECK(at<sizeof regs);*v=regs[at/4];return 0;}
static int read64(void *,size_t at,uint64_t *v) {CHECK(at+8<=sizeof regs);std::memcpy(v,regs+at/4,8);return 0;}
static int write32(void *,size_t at,uint32_t v) {
    CHECK(at<sizeof regs);
    if(at>=0x20000 && (at&4095)==0x58)regs[at/4]&=~v;
    else regs[at/4]=(bad_cbar && at==0x1000)?0:v;
    return 0;
}
static int write64(void *,size_t at,uint64_t v) {CHECK(at+8<=sizeof regs);std::memcpy(regs+at/4,&v,8);return 0;}
static int barrier(void *) {return 0;}
static void reset() {
    std::memset(regs,0,sizeof regs);bad_cbar=false;
    regs[0]=0x002f0406;regs[0x20/4]=0x4c017e18;regs[0x24/4]=0x40000016;regs[0x28/4]=0x1433;
    for(unsigned i=0;i<24;++i)regs[(0xc00+4*i)/4]=0x20000;
}
int main() {
    const ArmSmmu4KIo io{nullptr,sizeof regs,read32,read64,write32,write64,barrier};
    const unsigned slots[2]={0,1};const uint16_t sids[2]={0,1};
    const uint64_t a740Iovas[]={GpuSqeIova,GpuRingIova,GpuMemptrsIova,GpuPwrupIova,GpuScratchIova,
        GpuPreemptRecordIova,GpuPreemptRecord1Iova,GpuPreemptSmmuIova,GpuPreemptSmmu1Iova,GpuRing1Iova,GpuMemptrs1Iova};
    const uint64_t a830Iovas[]={GpuSqeIova,GpuRingIova,GpuMemptrsIova,GpuPwrupIova,GpuScratchIova,GpuAqeIova};
    for(uint32_t chip:{0x43050a01u,0x44050001u}) {
        reset();NativeAdrenoGpuContext context;NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io,0,2,slots,sids,nullptr,&report,chip)==0);
        const unsigned count=chip==0x44050001?6:11;
        const uint64_t *iovas=count==6?a830Iovas:a740Iovas;
        CHECK(context.ready() && context.bufferCount()==count && report.routes_verified==2);
        CHECK(context.vm()->objectCount()==count && context.vm()->spanCount()==count);
        CHECK(context.buffer(0).bytes()==(count==6?GpuA830SqeBytes:GpuSqeBytes));
        for(unsigned i=0;i<count;++i) {
            uint64_t address=0;size_t bytes=0;unsigned perms=0;
            CHECK(context.vm()->describe(context.vm()->handleAt(iovas[i]),&address,&bytes,&perms)==0);
            CHECK(address==iovas[i] && bytes==context.buffer(i).bytes());
            CHECK(perms==((i==0 || (count==6 && i==5))?5u:3u));
        }
        CHECK(context.buffer(5).bytes()==(count==6?size_t(GpuAqeBytes):size_t(GpuPreemptRecordBytes)));
        CHECK(context.inputBits()==44 && !context.enableSwitching());
        NativeAdrenoProcessVm *process=nullptr;
        CHECK(!context.createVm(&process) && process && process->vm()->objectCount()==count);
        for(unsigned i=0;i<count;++i) {
            uint64_t address=0;size_t bytes=0;unsigned perms=0;
            CHECK(!process->vm()->describe(process->vm()->handleAt(iovas[i]),&address,&bytes,&perms));
            CHECK(address==iovas[i] && bytes==context.buffer(i).bytes() && perms==((i==0 || (count==6 && i==5))?5u:3u));
        }
        CHECK(!context.destroyVm(process) && context.liveVms()==0);
        CHECK(context.bringUp(&io,0,2,slots,sids,nullptr,&report,chip)==kIOReturnBusy);
    }
    reset();NativeAdrenoGpuContext rejected;NativeAdrenoGmuContextReport report;
    const unsigned allocations=fixture.created;
    CHECK(rejected.bringUp(&io,0,2,slots,sids,nullptr,&report,0)==kIOReturnUnsupported);
    CHECK(fixture.created==allocations && report.step==0);
    regs[0x20000/4]=1;
    CHECK(rejected.bringUp(&io,0,2,slots,sids,nullptr,&report,0x44050001)!=0);
    CHECK(!rejected.ready() && rejected.vm()==nullptr && report.program_writes==0);
    reset();bad_cbar=true;NativeAdrenoGpuContext mismatch;
    CHECK(mismatch.bringUp(&io,0,2,slots,sids,nullptr,&report,0x44050001)!=0);
    CHECK(!mismatch.ready() && report.mismatch==1 && report.program_status==ArmSmmu4KStateMismatch);
    std::puts("PASS: A740/A830 GPU contexts, six buffer extents and permissions, unsupported chip, live-bank refusal and actual CBAR mismatch report");
}
