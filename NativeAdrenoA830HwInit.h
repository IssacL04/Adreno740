/* SPDX-License-Identifier: MIT */
#pragma once
#include "NativeAdrenoCpInit.h"
#include "NativeAdrenoPwrup.h"

struct NativeAdrenoA830InitResult {
    uint32_t stage=0,reg=0,writes=0,reads=0,secure_fence=0;
    IOReturn status=kIOReturnSuccess;
};
/* A830v2 only, single BR/BV ring, all three slices and 12 MiB GMEM.
 * Caller holds the GMU GPU OOB vote and owns every MMIO/memory mapping.
 * Configure after ring attachment and verified firmware copies, then run
 * NativeAdrenoCpInit with sqe_base_programmed=1 and observe its fence.
 * Finish authenticates ZAP through the platform callback, switches security
 * through the CP and builds the restore list from actual register readback.
 * A failed attempt is permanent; the board must retain/fault its resources. */
class NativeAdrenoA830HwInit final {
    NativeAdrenoRingIo gpu_{},gmu_{};
    NativeAdrenoA830InitResult result_{};
    bool attempted_=false,prepared_=false,finished_=false,poisoned_=false;
    IOReturn fail(IOReturn);
    bool write(uint32_t reg,uint32_t value,bool gmu=false);
    bool read(uint32_t reg,uint32_t *value,bool gmu=false);
    bool pair(uint32_t reg,uint64_t value);
    bool aperture(unsigned pipe);
    bool checked(uint32_t reg,uint32_t value);
    IOReturn restoreList(uint8_t *,size_t,size_t *);
public:
    IOReturn configure(const NativeAdrenoRingIo *gpu,const NativeAdrenoRingIo *gmu,
        unsigned highest_bank_bit,uint64_t sqe_iova,uint64_t aqe_iova);
    IOReturn finish(NativeAdrenoRing *,const NativeAdrenoCpInit *,const NativeAdrenoRingClock *,
        uint32_t timeout_us,IOReturn (*authenticate)(void *),void *cookie,
        uint8_t *powerup,size_t powerup_bytes,const NativeAdrenoRingCache *visibility);
    bool ready() const {return finished_ && !poisoned_;}
    bool poisoned() const {return poisoned_;}
    const NativeAdrenoA830InitResult &result() const {return result_;}
};
