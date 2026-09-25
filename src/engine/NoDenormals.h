// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

#if defined (__SSE__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define SPM_DENORMALS_SSE 1
#endif

namespace spm::engine
{

/** Flushes denormal floats to zero while in scope (audio thread).

    Filters, reverbs and envelopes decaying towards silence otherwise produce denormal
    numbers, which are very slow on most CPUs and can cause dropouts.
*/
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
       #if SPM_DENORMALS_SSE
        previous = _mm_getcsr();
        _mm_setcsr (previous | 0x8040u);  // flush-to-zero and denormals-are-zero
       #elif defined (__aarch64__)
        std::uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
        previous = fpcr;
        asm volatile ("msr fpcr, %0" : : "r" (fpcr | (1ull << 24)));  // FZ
       #endif
    }

    ~ScopedNoDenormals() noexcept
    {
       #if SPM_DENORMALS_SSE
        _mm_setcsr ((unsigned int) previous);
       #elif defined (__aarch64__)
        asm volatile ("msr fpcr, %0" : : "r" (previous));
       #endif
    }

    ScopedNoDenormals (const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator= (const ScopedNoDenormals&) = delete;

private:
    [[maybe_unused]] std::uint64_t previous = 0;
};

} // namespace spm::engine
