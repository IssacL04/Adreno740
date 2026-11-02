/* SPDX-License-Identifier: MIT */
#ifndef NATIVE_ADRENO_DIAGNOSTICS_H
#define NATIVE_ADRENO_DIAGNOSTICS_H
#include <stdint.h>

struct NativeAdrenoDiagnostics {
    uint32_t opens,closes,busy,calls,last_method,last_status,last_error_method,last_error;
    uint32_t submissions,completed,poll_misses,poisoned,cached;
    char opener[17];
};
/* Approximate counters; cached=1 retains the last sample while the GPU lock is busy. */
extern "C" bool native_adreno_diagnostics(NativeAdrenoDiagnostics *);
#endif
