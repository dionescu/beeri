// Copyright (C) 1999-2007 Google, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// All rights reserved.
// Extracted from base/timer.h by jrvb

// The implementation of CycleClock::Now()
// See cycleclock.h
//
// IWYU pragma: private, include "base/cycleclock.h"

// NOTE: only i386 and x86_64 have been well tested.
// PPC, sparc, alpha, and ia64 are based on
//    http://peter.kuscsik.com/wordpress/?p=14
// with modifications by m3b.  See also
//    https://setisvn.ssl.berkeley.edu/svn/lib/fftw-3.0.1/kernel/cycle.h

#ifndef SUPERSONIC_OPENSOURCE_AUXILIARY_CYCLECLOCK_INL_H_
#define SUPERSONIC_OPENSOURCE_AUXILIARY_CYCLECLOCK_INL_H_

#include <sys/time.h>

#include "base/port.h"

// Please do not nest #if directives.  Keep one section, and one #if per
// platform.

// For historical reasons, the frequency on some platforms is scaled to be
// close to the platform's core clock frequency.  This is not guaranteed by the
// interface, and may change in future implementations.

// ----------------------------------------------------------------
#if defined(__APPLE__)
#include <mach/mach_time.h>
inline uint64 CycleClock::Now() {
  // this goes at the top because we need ALL Macs, regardless of
  // architecture, to return the number of "mach time units" that
  // have passed since startup.  See sysinfo.cc where
  // InitializeSystemInfo() sets the supposed cpu clock frequency of
  // macs to the number of mach time units per second, not actual
  // CPU clock frequency (which can change in the face of CPU
  // frequency scaling).  Also note that when the Mac sleeps, this
  // counter pauses; it does not continue counting, nor does it
  // reset to zero.
  return mach_absolute_time();
}

// ----------------------------------------------------------------
#elif defined(__i386__)
inline uint64 CycleClock::Now() {
  uint64 ret;
  __asm__ volatile("rdtsc" : "=A" (ret));
  return ret;
}

// ----------------------------------------------------------------
#elif defined(__x86_64__) || defined(__amd64__)
inline uint64 CycleClock::Now() {
  uint64 low, high;
  __asm__ volatile("rdtsc" : "=a" (low), "=d" (high));
  return (high << 32) | low;
}

// ----------------------------------------------------------------
#elif defined(__powerpc__) || defined(__ppc__)
#define SPR_TB 268
#define SPR_TBU 269
inline uint64 CycleClock::Now() {
  uint64 time_base_value;
  if (sizeof(void*) == 8) {
    // On PowerPC64, time base can be read with one SPR read.
    asm volatile("mfspr %0, %1" : "=r" (time_base_value) : "i"(SPR_TB));
  } else {
    uint32 tbl, tbu0, tbu1;
    asm volatile (" mfspr %0, %3\n"
                  " mfspr %1, %4\n"
                  " mfspr %2, %3\n" :
                  "=r"(tbu0), "=r"(tbl), "=r"(tbu1) :
                  "i"(SPR_TBU), "i"(SPR_TB));
    // If there is a carry into the upper half, it is okay to return
    // (tbu1, 0) since it must be between the 2 TBU reads.
    tbl &= -static_cast<uint32>(tbu0 == tbu1);
    // high 32 bits in tbu1; low 32 bits in tbl  (tbu0 is garbage)
    time_base_value =
        (static_cast<uint64>(tbu1) << 32) | static_cast<uint64>(tbl);
  }
  return time_base_value;
}

// ----------------------------------------------------------------
#elif defined(__sparc__)
inline uint64 CycleClock::Now() {
  int64 tick;
  asm(".byte 0x83, 0x41, 0x00, 0x00");
  asm("mov   %%g1, %0" : "=r" (tick));
  return tick;
}

// ----------------------------------------------------------------
#elif defined(__ia64__)
inline uint64 CycleClock::Now() {
  uint64 itc;
  asm("mov %0 = ar.itc" : "=r" (itc));
  return itc;
}

// ----------------------------------------------------------------
#elif defined(_MSC_VER) && defined(_M_IX86)
inline uint64 CycleClock::Now() {
  // Older MSVC compilers (like 7.x) don't seem to support the
  // __rdtsc intrinsic properly, so I prefer to use _asm instead
  // when I know it will work.  Otherwise, I'll use __rdtsc and hope
  // the code is being compiled with a non-ancient compiler.
  _asm rdtsc
}

// ----------------------------------------------------------------
#elif defined(_MSC_VER)
// For MSVC, we want to use '_asm rdtsc' when possible (since it works
// with even ancient MSVC compilers), and when not possible the
// __rdtsc intrinsic, declared in <intrin.h>.  Unfortunately, in some
// environments, <windows.h> and <intrin.h> have conflicting
// declarations of some other intrinsics, breaking compilation.
// Therefore, we simply declare __rdtsc ourselves. See also
// http://connect.microsoft.com/VisualStudio/feedback/details/262047
extern "C" uint64 __rdtsc();
#pragma intrinsic(__rdtsc)
inline uint64 CycleClock::Now() {
  return __rdtsc();
}

// ----------------------------------------------------------------
#else
// The soft failover to a generic implementation is automatic only for some
// platforms.  For other platforms the developer is expected to make an attempt
// to create a fast implementation and use generic version if nothing better is
// available.
#error You need to define CycleTimer for your O/S and CPU
#endif

#endif  // SUPERSONIC_OPENSOURCE_AUXILIARY_CYCLECLOCK_INL_H_
