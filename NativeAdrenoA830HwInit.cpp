/* SPDX-License-Identifier: MIT */
#include "NativeAdrenoA830HwInit.h"
#include "NativeAdrenoA830Regs.h"
#include "AdrenoPM4.h"

IOReturn NativeAdrenoA830HwInit::fail(IOReturn e)
{poisoned_=true;result_.status=e;return e;}
bool NativeAdrenoA830HwInit::write(uint32_t reg,uint32_t value,bool gmu)
{
    result_.reg=reg;++result_.writes;auto &io=gmu?gmu_:gpu_;
    return !io.write32(io.cookie,reg,value);
}
bool NativeAdrenoA830HwInit::read(uint32_t reg,uint32_t *value,bool gmu)
{
    result_.reg=reg;++result_.reads;auto &io=gmu?gmu_:gpu_;
    return !io.read32(io.cookie,reg,value);
}
bool NativeAdrenoA830HwInit::pair(uint32_t reg,uint64_t value)
{return write(reg,uint32_t(value)) && write(reg+1,uint32_t(value>>32));}
bool NativeAdrenoA830HwInit::aperture(unsigned pipe)
{return write(A830_CP_APERTURE_CNTL_HOST,pipe<<12) && !gpu_.barrier(gpu_.cookie);}
bool NativeAdrenoA830HwInit::checked(uint32_t reg,uint32_t value)
{uint32_t observed;return write(reg,value) && read(reg,&observed) && observed==value;}

IOReturn NativeAdrenoA830HwInit::configure(const NativeAdrenoRingIo *gpu,const NativeAdrenoRingIo *gmu,
    unsigned highest_bank_bit,uint64_t sqe_iova,uint64_t aqe_iova)
{
    if(attempted_)return kIOReturnBusy;
    if(!gpu || !gmu || !gpu->write32 || !gpu->read32 || !gpu->barrier ||
       !gmu->write32 || !gmu->read32 || !gmu->barrier || highest_bank_bit<13 || highest_bank_bit>20 ||
       !sqe_iova || !aqe_iova || ((sqe_iova|aqe_iova)&4095) || sqe_iova==aqe_iova ||
       sqe_iova>=UINT64_C(1)<<48 || aqe_iova>=UINT64_C(1)<<48)return kIOReturnBadArgument;
    attempted_=true;gpu_=*gpu;gmu_=*gmu;
    result_.stage=1;
    if(!aperture(0) || !checked(A830_GBIF_HALT,0) || !checked(A830_RBBM_GBIF_HALT,0) ||
       !write(A830_RBBM_SECVID_TSB_CNTL,0) || !pair(A830_RBBM_SECVID_TSB_TRUSTED_BASE,0) ||
       !write(A830_RBBM_SECVID_TSB_TRUSTED_SIZE,0))return fail(kIOReturnIOError);
    if(!write(A830_RBBM_PERFCTR_GPU_BUSY_MASKED,UINT32_MAX) ||
       !pair(A830_UCHE_CCHE_GC_GMEM_RANGE_MIN,0x4000000) || !pair(A830_SP_HLSQ_GC_GMEM_RANGE_MIN,0x4000000))
        return fail(kIOReturnIOError);
    const uint32_t traps[]={A830_UCHE_TRAP_BASE,A830_UCHE_WRITE_THRU_BASE,A830_UCHE_CCHE_TRAP_BASE,A830_UCHE_CCHE_WRITE_THRU_BASE};
    for(uint32_t reg:traps)
        if(!pair(reg,UINT64_C(0x1fffffffff000)))return fail(kIOReturnIOError);
    if(!write(A830_RBBM_PERFCTR_CNTL,1) || !write(A830_RBBM_SLICE_PERFCTR_CNTL,1) ||
       !write(A830_CP_PERFCTR_CP_SEL,1) || !write(A830_GMU_CX_GMU_POWER_COUNTER_SELECT_XOCLK_1,4,true))
        return fail(kIOReturnIOError);
    result_.stage=2;
    const uint32_t hbb=highest_bank_bit-13,lo=hbb&3,hi=hbb>>2;
    /* PKX110 DT: UBWC 5, 32-byte minimum access; swizzling levels 2/3 enabled. */
    for(unsigned pipe=1;pipe<=2;++pipe)
        if(!aperture(pipe) || !write(A830_GRAS_NC_MODE_CNTL,hbb<<5))return fail(kIOReturnIOError);
    if(!aperture(1) || !write(A830_RB_CCU_NC_MODE_CNTL,(hi<<3)|(lo<<1)) ||
       !write(A830_RB_CMP_NC_MODE_CNTL,0x20006) || !aperture(0) ||
       !write(A830_SP_NC_MODE_CNTL,(hi<<10)|(2u<<4)|(lo<<1)) ||
       !write(A830_TPL1_NC_MODE_CNTL,(hi<<4)|(lo<<1)))return fail(kIOReturnIOError);
    static const uint32_t weights[]={0,0x3fe05ff4,0x3fa0ebee,0x3f5193ed,0x3f0243f0,
        0,0x3fd093e8,0x3f4133dc,0x3ea1dfdb,0x3e0283e0,0xac2b,0xf01d,0x114412,0x21980a,
        0x51ec05,0x380e,0x3ff09001,0x3fc10bfa,0x3f9193f7,0x3f7227f7};
    for(unsigned i=0;i<20;++i)if(!write(A830_TPL1_BICUBIC_WEIGHTS_TABLE+i,weights[i]))return fail(kIOReturnIOError);
    if(!write(A830_UCHE_CLIENT_PF,0x81))return fail(kIOReturnIOError);
    for(unsigned pipe=0;pipe<8;++pipe) {
        if(pipe==3)continue;
        if(!aperture(pipe))return fail(kIOReturnIOError);
        for(const auto &row:NativeAdrenoA830Noncontext)
            if((row[2]&(1u<<pipe)) && row[0]!=A830_RB_GC_GMEM_PROTECT && !write(row[0],row[1]))
                return fail(kIOReturnIOError);
    }
    if(!aperture(0) || !write(A830_RBBM_INTERFACE_HANG_INT_CNTL,0x40cfffff) ||
       !write(A830_RBBM_SLICE_INTERFACE_HANG_INT_CNTL,1u<<30) ||
       !write(A830_GPU_GMU_AO_GPU_CX_BUSY_MASK,0xff000000,true))return fail(kIOReturnIOError);
    uint32_t counter;
    if(!read(A830_GMU_CX_GMU_POWER_COUNTER_SELECT_XOCLK_0,&counter,true) ||
       !write(A830_GMU_CX_GMU_POWER_COUNTER_SELECT_XOCLK_0,(counter&~255u)|32,true) ||
       !write(A830_GMU_CX_GMU_POWER_COUNTER_ENABLE,1,true))return fail(kIOReturnIOError);
    result_.stage=3;
    /* Reset unused slots as well: inherited protection state is not an input. */
    for(unsigned i=0;i<64;++i) {
        const uint32_t value=i<47?NativeAdrenoA830Protect[i]:i==63?NativeAdrenoA830Protect[47]:0;
        if(!checked(A830_CP_PROTECT_GLOBAL+i,value))return fail(kIOReturnIOError);
    }
    for(unsigned pipe=1;pipe<=2;++pipe)
        if(!aperture(pipe) || !checked(A830_CP_PROTECT_CNTL_PIPE,0xffff000b) ||
           !checked(A830_CP_PROTECT_PIPE+15,NativeAdrenoA830Protect[47]))return fail(kIOReturnIOError);
    if(!aperture(1) || !write(A830_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE_ENABLE,1))return fail(kIOReturnIOError);
    for(unsigned pipe=1;pipe<8;++pipe) {
        if(pipe==3)continue;
        if(!aperture(pipe) || !write(A830_CP_APRIV_CNTL_PIPE,pipe==1?0x6f:0xf) ||
           !write(A830_CP_INTERRUPT_STATUS_MASK_PIPE,0x07e83f7f) ||
           !write(A830_CP_HW_FAULT_STATUS_MASK_PIPE,0x77f))return fail(kIOReturnIOError);
    }
    if(!aperture(0) || !write(A830_CP_INTERRUPT_STATUS_MASK_GLOBAL,0x007f007f))return fail(kIOReturnIOError);
    result_.stage=4;
    if(!pair(0x816,sqe_iova) || !pair(0x818,aqe_iova) || gpu_.barrier(gpu_.cookie) ||
       !checked(A830_CP_SQE_CNTL,1))return fail(kIOReturnIOError);
    prepared_=true;return kIOReturnSuccess;
}

IOReturn NativeAdrenoA830HwInit::restoreList(uint8_t *out,size_t capacity,size_t *written)
{
    constexpr size_t ni=sizeof(NativeAdrenoA830Ifpc)/sizeof(uint32_t);
    constexpr size_t np=sizeof(NativeAdrenoA830Pwrup)/sizeof(uint32_t);
    uint32_t ifpc[ni],preempt[np],dynamic[28*7*3];unsigned count=0;
    if(!aperture(0))return kIOReturnIOError;
    for(size_t i=0;i<ni;++i)if(!read(NativeAdrenoA830Ifpc[i],&ifpc[i]))return kIOReturnIOError;
    for(size_t i=0;i<np;++i)if(!read(NativeAdrenoA830Pwrup[i],&preempt[i]))return kIOReturnIOError;
    for(unsigned pipe=1;pipe<8;++pipe) {
        if(!aperture(pipe))return kIOReturnIOError;
        for(const auto &row:NativeAdrenoA830Dynamic)if(row[2]&(1u<<pipe)) {
            dynamic[count*3]=pipe<<12;dynamic[count*3+1]=row[0];
            if(!read(row[0],&dynamic[count*3+2]))return kIOReturnIOError;
            ++count;
        }
    }
    if(!aperture(0))return kIOReturnIOError;
    const NativeAdrenoPwrupCounts counts={ni,np,count};
    return NativeAdrenoPwrupList::pack(counts,NativeAdrenoA830Ifpc,ifpc,NativeAdrenoA830Pwrup,preempt,
        dynamic,out,capacity,written);
}

IOReturn NativeAdrenoA830HwInit::finish(NativeAdrenoRing *ring,const NativeAdrenoCpInit *cp,
    const NativeAdrenoRingClock *clock,uint32_t timeout_us,IOReturn (*authenticate)(void *),void *cookie,
    uint8_t *powerup,size_t powerup_bytes,const NativeAdrenoRingCache *visibility)
{
    if(!prepared_ || poisoned_ || finished_)return kIOReturnNotReady;
    if(!ring || ring->chipId()!=NativeAdrenoChipA830 || ring->poisoned() || ring->outstanding() ||
       !cp || !cp->completed() || cp->poisoned() || !clock || !clock->now_us || !timeout_us || !authenticate ||
       !powerup || (uintptr_t(powerup)&3) || powerup_bytes<4096 || !visibility || !visibility->publish)
        return kIOReturnBadArgument;
    result_.stage=5;
    IOReturn e=authenticate(cookie);if(e)return fail(e);
    uint32_t words[2]={0,0};
    if(AdrenoPM4Type7(0x66,1,words)!=AdrenoPM4Success)return fail(kIOReturnIOError);
    if((e=ring->submit(words,2,&result_.secure_fence)))return fail(e);
    auto status=ring->wait(result_.secure_fence,clock,timeout_us,nullptr);
    if(status!=NativeAdrenoRingFenceComplete)
        return fail(status==NativeAdrenoRingFenceTimeout?kIOReturnTimeout:kIOReturnIOError);
    result_.stage=6;
    if(!aperture(1) || !write(A830_RB_GC_GMEM_PROTECT,0x0c000000) || !aperture(0))return fail(kIOReturnIOError);
    size_t written;
    if((e=restoreList(powerup,powerup_bytes,&written)))return fail(e);
    if(visibility->publish(visibility->cookie,reinterpret_cast<volatile uint32_t *>(powerup),written))
        return fail(kIOReturnIOError);
    result_.stage=7;
    /* AO clock values are the SM8750 vendor gen8_ao_hwcg_regs calibration. */
    if(!write(A830_RBBM_CGC_0_PC,0x702) || !write(A830_GPU_GMU_AO_GMU_CGC_MODE_CNTL,0x20000,true) ||
       !write(A830_GPU_GMU_AO_GMU_CGC_DELAY_CNTL,0x10111,true) ||
       !write(A830_GPU_GMU_AO_GMU_CGC_HYST_CNTL,0x5555,true) ||
       !write(A830_RBBM_CLOCK_CNTL_GLOBAL,1) || !write(A830_RBBM_CGC_GLOBAL_LOAD_CMD,1) ||
       !write(A830_RBBM_CGC_P2S_TRIG_CMD,1))return fail(kIOReturnIOError);
    const uint64_t started=clock->now_us(clock->cookie);
    bool loaded=false;
    for(unsigned n=0;n<1000;++n) {
        uint32_t v;if(!read(A830_RBBM_CGC_P2S_STATUS,&v))return fail(kIOReturnIOError);
        if(v&1) {loaded=true;break;}
        const uint64_t now=clock->now_us(clock->cookie);
        if(now<started || now-started>=1000)break;
    }
    if(!loaded)return fail(kIOReturnTimeout);
    if(!write(A830_RBBM_CLOCK_CNTL_GLOBAL,0) || gpu_.barrier(gpu_.cookie) || gmu_.barrier(gmu_.cookie))
        return fail(kIOReturnIOError);
    result_.stage=8;finished_=true;return kIOReturnSuccess;
}
