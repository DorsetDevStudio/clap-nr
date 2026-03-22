/*  comm.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2024, 2025 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at  

warren@wpratt.com

*/

#ifndef _comm_h
#define _comm_h

/* -----------------------------------------------------------------------
 * Platform-specific system headers
 * --------------------------------------------------------------------- */
#ifdef _WIN32
#  include <Windows.h>
#  include <process.h>
#  include <intrin.h>
#  include <avrt.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "fftw3.h"

/* -----------------------------------------------------------------------
 * Portable mutex abstraction
 * Used in place of Win32 CRITICAL_SECTION throughout the NR modules.
 * --------------------------------------------------------------------- */
#ifdef _WIN32
#  define NR_MUTEX                  CRITICAL_SECTION
#  define NR_MUTEX_INIT(m)          InitializeCriticalSection(&(m))
#  define NR_MUTEX_DESTROY(m)       DeleteCriticalSection(&(m))
#  define NR_MUTEX_LOCK(m)          EnterCriticalSection(&(m))
#  define NR_MUTEX_UNLOCK(m)        LeaveCriticalSection(&(m))
#else
#  include <pthread.h>
#  define NR_MUTEX                  pthread_mutex_t
#  define NR_MUTEX_INIT(m)          pthread_mutex_init(&(m), NULL)
#  define NR_MUTEX_DESTROY(m)       pthread_mutex_destroy(&(m))
#  define NR_MUTEX_LOCK(m)          pthread_mutex_lock(&(m))
#  define NR_MUTEX_UNLOCK(m)        pthread_mutex_unlock(&(m))
#endif

/* -----------------------------------------------------------------------
 * Portable aligned-memory free
 * _aligned_free() is MSVC CRT only; on POSIX the allocator (posix_memalign
 * or aligned_alloc) returns memory that must be released with plain free().
 * --------------------------------------------------------------------- */
#ifdef _WIN32
#  define nr_aligned_free(p)        _aligned_free(p)
#else
#  define nr_aligned_free(p)        free(p)
#endif

/* -----------------------------------------------------------------------
 * DLL export attribute
 * __declspec(dllexport) is MSVC/Windows only; GCC/Clang use a visibility
 * attribute.  When building as a CLAP plugin (CLAP_NR_STANDALONE) these
 * entry points are not exported at all.
 * --------------------------------------------------------------------- */
#ifndef CLAP_NR_STANDALONE
#  ifdef _WIN32
#    define PORT  __declspec(dllexport)
#  elif defined(__GNUC__) || defined(__clang__)
#    define PORT  __attribute__((visibility("default")))
#  else
#    define PORT
#  endif
#else
#  define PORT
#endif

/* -----------------------------------------------------------------------
 * Manage differences among consoles (Thetis SDR build flag)
 * --------------------------------------------------------------------- */
#define _Thetis

/* -----------------------------------------------------------------------
 * Channel / buffer definitions
 * --------------------------------------------------------------------- */
#define MAX_CHANNELS                32          // maximum number of supported channels
#define DSP_MULT                    2           // number of dsp_buffsizes held in an iobuff pseudo-ring
#define INREAL                      float       // data type for channel input buffer
#define OUTREAL                     float       // data type for channel output buffer

/* -----------------------------------------------------------------------
 * Display definitions
 * --------------------------------------------------------------------- */
#define dMAX_DISPLAYS               72          // maximum number of displays = max instances
#define dMAX_STITCH                 4           // maximum number of sub-spans to stitch together
#define dMAX_NUM_FFT                1           // maximum number of ffts for an elimination
#define dMAX_PIXELS                 16384       // maximum number of pixels that can be requested
#define dMAX_AVERAGE                60          // maximum number of pixel frames that will be window-averaged
#ifdef _Thetis
#  define dINREAL                   double
#else
#  define dINREAL                   float
#endif
#define dOUTREAL                    float
#define dSAMP_BUFF_MULT             2           // ratio of input sample buffer size to fft size (for overlap)
#define dNUM_PIXEL_BUFFS            3           // number of pixel output buffers
#define dMAX_M                      1           // number of variables to calibrate
#define dMAX_N                      100         // maximum number of frequencies at which to calibrate
#define dMAX_CAL_SETS               2           // maximum number of calibration data sets
#define dMAX_PIXOUTS                4           // maximum number of det/avg/outputs per display instance

/* -----------------------------------------------------------------------
 * Wisdom (FFTW plan cache) size limits
 * --------------------------------------------------------------------- */
#define MAX_WISDOM_SIZE_DISPLAY     262144
#define MAX_WISDOM_SIZE_FILTER      262144      // was 32769

/* -----------------------------------------------------------------------
 * Math constants
 * --------------------------------------------------------------------- */
#define PI                          3.1415926535897932
#define TWOPI                       6.2831853071795864

/* -----------------------------------------------------------------------
 * Miscellaneous utilities required by NR algorithm modules
 * --------------------------------------------------------------------- */
typedef double complex[2];
#define malloc0(n)  calloc(1, (n))
#define mlog10(x)   log10(x)
#ifndef min
#define min(a,b)    ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b)    ((a) > (b) ? (a) : (b))
#endif

#endif /* _comm_h */