/*  nr0.h - NR0: Spectral Tone Notcher

    Overlap-add FFT spectral notcher for narrowband tone interference.
    Automatically detects and attenuates sharp spectral spikes (constant
    carriers, heterodynes, and fast-moving whistles) without affecting
    broadband speech or music content.

    Algorithm:
      - 2048-point FFT, 75% overlap (512-sample hop), Hann window
      - Per-frame local spectral floor estimated from surrounding bins
      - Spikes detected by threshold-above-floor AND sharpness tests
      - Per-bin hold counter smooths transient detections
 *   - Aggressiveness 0-100 maps continuously to hold length (30-0 frames @ ~10 ms/frame)
 *   - Detection threshold (3-20 dB) set independently via the threshold_db field

    Copyright (C) 2026 - Stuart E. Green (G5STU)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef _nr0_h
#define _nr0_h

#include "fftw3.h"

/* FFT frame size: 2048 gives ~23 Hz/bin at 48 kHz.  Narrow enough to
 * notch a single tone with +/-1 bin killed without affecting adjacent speech.
 * Step size 512 gives 75% overlap for smooth reconstruction. */
#define NR0_FFT_SIZE  2048
#define NR0_MSIZE     (NR0_FFT_SIZE / 2 + 1)   /* 1025 complex r2c output bins */
#define NR0_STEP      (NR0_FFT_SIZE / 4)         /* 512 samples per hop (75% overlap) */

typedef struct _nr0 {
    int run;                /* 1 = active, 0 = passthrough */

    /* FFTW plans and aligned working buffers */
    fftw_plan       plan_fwd;   /* r2c: fft_in[N]  -> fft_cmx[M] */
    fftw_plan       plan_inv;   /* c2r: fft_cmx[M] -> fft_out[N] */
    double         *fft_in;     /* [NR0_FFT_SIZE] windowed time-domain input */
    fftw_complex   *fft_cmx;    /* [NR0_MSIZE]    complex spectrum (modified in-place) */
    double         *fft_out;    /* [NR0_FFT_SIZE] IFFT result */

    /* Hann analysis window */
    double         *window;     /* [NR0_FFT_SIZE] */

    /* Per-bin algorithm state */
    double         *mag;        /* [NR0_MSIZE] magnitude of current frame */
    int            *hold;       /* [NR0_MSIZE] per-bin hold-down counter */

    /* Overlap-add output accumulator */
    double         *ola;        /* [NR0_FFT_SIZE] */

    /* Input shift register: most recent NR0_FFT_SIZE samples */
    double         *shift;      /* [NR0_FFT_SIZE] */

    /* Sample-level staging: buffer NR0_STEP new samples before each frame */
    double         *inbuf;      /* [NR0_STEP] */
    int             in_count;

    /* Output queue: NR0_STEP processed samples ready after each frame */
    double         *outq;       /* [NR0_STEP] */
    int             out_head;
    int             out_avail;

    /* Parameters (may be updated between frames) */
    float           aggression;     /* 0.0 = slowest/most conservative (hold 30 frames)
                                     * 100.0 = fastest/most aggressive (instant release) */
    int             max_notches;    /* 1-10: max simultaneous notch bins                 */
    float           threshold_db;   /* dB above local floor to trigger a notch (3-20 dB)*/

    /* Read by the GUI render loop; audio thread writes a single int -- x86 atomic */
    volatile int    active_notches; /* number of tones being notched in the last frame   */

} nr0_t, *NR0;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/* Allocate and initialise an NR0 instance.  Creates FFTW plans which are
 * expensive to create but must survive across activate/deactivate cycles
 * (destroying and recreating plans while the audio thread may be inside
 * fftw_execute() will cause FFTW to abort()).  Call once at first activate;
 * call flush_nr0() on subsequent activations instead of re-creating. */
extern NR0  create_nr0 (float aggression, int max_notches, float threshold_db);

/* Free all resources.  Must only be called when the audio thread is
 * guaranteed not to be inside xnr0() (i.e. after deactivate() drains). */
extern void destroy_nr0(NR0 self);

/* Zero all history and staging buffers for a clean restart.  Call on
 * transport reset, re-activation, or when switching back into NR0 mode. */
extern void flush_nr0  (NR0 self);

/* -----------------------------------------------------------------------
 * Processing
 * --------------------------------------------------------------------- */

/* Process n float samples.  src and dst may alias.  Any host block size
 * is accepted; internally the algorithm runs in NR0_STEP=512-sample hops.
 * Latency = NR0_STEP samples (~10.7 ms @ 48 kHz).  During the first hop
 * the output is a passthrough copy; thereafter fully processed audio. */
extern void xnr0(NR0 self, const float *src, float *dst, int n);

#endif /* _nr0_h */
