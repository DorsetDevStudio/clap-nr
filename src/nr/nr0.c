/*  nr0.c - NR0: Spectral Tone Notcher

    Overlap-add FFT spectral notcher for narrowband tone interference.

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

#define _CRT_SECURE_NO_WARNINGS
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fftw3.h"
#include "nr0.h"

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

/* OLA normalization factor for 75% overlap Hann window.
 *
 * The COLA (Constant Overlap-Add) property of the Hann window means that
 * the sum of four shifted copies at 75% overlap equals a constant value:
 *   sum_{k=0}^{3} w[n - k*H] = 1.5   for all n  (H = N/4)
 *
 * FFTW's c2r transform is unnormalized (output = N * true IDFT).
 * Combined correction: each IFFT output sample must be scaled by
 *   1/N  (FFTW normalization)  *  1/1.5  (OLA overlap correction)
 *   = 2 / (3 * N)
 *
 * Applied as: scale_ifft = 1/N, then scale_ola = 2/3. */
#define NR0_OLA_SCALE  (2.0 / 3.0)  /* applied after dividing IFFT by N */

/* Spike detection tuning */
#define NR0_GUARD_BINS   2     /* bins each side excluded from floor estimate */
#define NR0_FLOOR_BINS  16     /* bins each side used for floor estimate       */
#define NR0_NOTCH_WIDTH  2     /* bins each side of detected peak to zero      */

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

/* Map aggressiveness (0-100) to per-bin hold counter initial value (FFT frames).
 * Hold controls how long a notch remains active after the tone disappears.
 *
 *   aggr=0:    hold = 30 frames (~320 ms @ 48 kHz) -- slow release, good for carriers
 *   aggr=50:   hold = 15 frames (~160 ms)
 *   aggr=100:  hold =  0 frames (instant release)  -- tracks fast whistles
 *
 * The detection threshold is a separate field (threshold_db) and is no longer
 * derived from aggressiveness -- they are independent controls.
 */
static int aggr_to_hold(float aggr)
{
    double a = (double)aggr;
    if (a < 0.0)   a = 0.0;
    if (a > 100.0) a = 100.0;
    a /= 100.0;
    return (int)(30.0 * (1.0 - a) + 0.5);
}

/* Simple descending insertion sort for small arrays (n_peaks typically < 20) */
typedef struct { int bin; double excess; } nr0_peak_t;

static void sort_peaks_desc(nr0_peak_t *peaks, int n)
{
    for (int i = 1; i < n; i++) {
        nr0_peak_t tmp = peaks[i];
        int j = i - 1;
        while (j >= 0 && peaks[j].excess < tmp.excess) {
            peaks[j + 1] = peaks[j];
            j--;
        }
        peaks[j + 1] = tmp;
    }
}

/* -----------------------------------------------------------------------
 * Core per-frame FFT processing
 *
 * Precondition: self->shift[] holds the most recent NR0_FFT_SIZE samples.
 * Postcondition: self->outq[0..NR0_STEP-1] contains the processed output;
 *                self->ola[] is advanced by NR0_STEP positions.
 * --------------------------------------------------------------------- */
static void nr0_process_frame(NR0 self)
{
    const int N  = NR0_FFT_SIZE;
    const int M  = NR0_MSIZE;
    const int H  = NR0_STEP;

    /* -- 1. Apply Hann window and forward FFT -- */
    for (int k = 0; k < N; k++)
        self->fft_in[k] = self->shift[k] * self->window[k];

    fftw_execute(self->plan_fwd);

    /* -- 2. Compute per-bin magnitudes -- */
    for (int k = 0; k < M; k++) {
        double re = self->fft_cmx[k][0];
        double im = self->fft_cmx[k][1];
        self->mag[k] = sqrt(re * re + im * im);
    }

    /* -- 3. Detect spikes and apply notches (only when running) -- */
    self->active_notches = 0;  /* reset per-frame; updated below when run==1 */
    if (self->run) {
        /* Threshold: directly from threshold_db parameter (3-20 dB range)     */
        double thresh_lin = pow(10.0, (double)self->threshold_db / 20.0);
        /* Hold: aggression controls hold duration only (not detection)         */
        int    hold_new   = aggr_to_hold(self->aggression);

        /* Scratch space for detected peaks (stack-allocated; ~12 KB, fine
         * on the audio thread — typical stack is 1 MB+). */
        nr0_peak_t peaks[NR0_MSIZE];
        int n_peaks = 0;

        /* Scan interior bins only: need FLOOR_BINS+GUARD_BINS clearance. */
        int lo = NR0_FLOOR_BINS + NR0_GUARD_BINS;
        int hi = M - NR0_FLOOR_BINS - NR0_GUARD_BINS - 1;

        for (int k = lo; k <= hi; k++) {
            /* a) Local spectral floor: mean of surrounding bins,
             *    excluding the guard zone to avoid self-contamination. */
            double sum = 0.0;
            int    cnt = 0;
            for (int j = k - NR0_FLOOR_BINS; j <= k - NR0_GUARD_BINS; j++) {
                sum += self->mag[j];
                cnt++;
            }
            for (int j = k + NR0_GUARD_BINS; j <= k + NR0_FLOOR_BINS; j++) {
                sum += self->mag[j];
                cnt++;
            }
            double floor_est = (cnt > 0) ? (sum / cnt) : 0.0;
            if (floor_est < 1e-30) continue;

            /* b) Threshold test: bin must exceed floor by enough dB. */
            if (self->mag[k] <= thresh_lin * floor_est) continue;

            /* c) Local maximum test: must be the tallest adjacent bin.
             *    Prevents the rising slope of a wide peak from triggering. */
            if (self->mag[k] < self->mag[k - 1] ||
                self->mag[k] < self->mag[k + 1])
                continue;

            /* Record this peak with its excess above the floor.
             * Note: the sharpness test (peak vs neighbour ratio) has been
             * intentionally removed.  Real-world tones fall between FFT bin
             * centres, splitting energy across two bins; the sharpness ratio
             * then fails even for strong narrowband interference.  The
             * threshold + local-maximum tests are sufficient to distinguish
             * tones from broadband noise. */
            peaks[n_peaks].bin    = k;
            peaks[n_peaks].excess = self->mag[k] / floor_est;
            n_peaks++;
        }

        /* Select the strongest peaks up to max_notches.  Sorting is O(n^2)
         * but n_peaks is typically < 10 so this is negligible. */
        sort_peaks_desc(peaks, n_peaks);
        if (n_peaks > self->max_notches)
            n_peaks = self->max_notches;
        self->active_notches = n_peaks;  /* publish to GUI display */

        /* Update per-bin hold counters and zero notched spectrum bins.
         * Notch width: NR0_NOTCH_WIDTH bins each side of the detected centre.
         * A width of 2 captures both the peak bin and its immediate neighbours
         * regardless of whether the tone falls exactly on a bin boundary. */
        for (int k = 0; k < M; k++) {
            int notch = 0;
            for (int p = 0; p < n_peaks; p++) {
                if (k >= peaks[p].bin - NR0_NOTCH_WIDTH &&
                    k <= peaks[p].bin + NR0_NOTCH_WIDTH) {
                    notch = 1;
                    break;
                }
            }
            if (notch) {
                /* Use hold_new+1 so that even at aggression=100 (hold_new=0)
                 * the bin is zeroed this frame before expiring next frame. */
                self->hold[k] = hold_new + 1;
            } else if (self->hold[k] > 0) {
                self->hold[k]--;
            }

            /* Zero the complex bin while hold counter is active. */
            if (self->hold[k] > 0) {
                self->fft_cmx[k][0] = 0.0;
                self->fft_cmx[k][1] = 0.0;
            }
        }

        /* Restore conjugate symmetry for DC and Nyquist bins — these are
         * real-valued and must never be zeroed independently of each other
         * to avoid introducing imaginary content after c2r. */
        /* DC  (k=0) and Nyquist (k=N/2) are already real in r2c output;
         * zeroing them is safe since tones rarely sit exactly there. */
    }

    /* -- 4. Inverse FFT -- */
    fftw_execute(self->plan_inv);

    /* -- 5. Overlap-add: accumulate scaled IFFT output -- */
    double inv_n = 1.0 / (double)N;
    for (int k = 0; k < N; k++)
        self->ola[k] += self->fft_out[k] * inv_n;

    /* -- 6. Extract H output samples, apply OLA gain correction, advance -- */
    for (int k = 0; k < H; k++)
        self->outq[k] = self->ola[k] * NR0_OLA_SCALE;

    memmove(self->ola, self->ola + H, (N - H) * sizeof(double));
    memset(self->ola + N - H, 0,       H       * sizeof(double));
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

NR0 create_nr0(float aggression, int max_notches, float threshold_db)
{
    NR0 self = (NR0)calloc(1, sizeof(nr0_t));
    if (!self) return NULL;

    self->aggression   = aggression;
    self->max_notches  = (max_notches < 1) ? 1 : ((max_notches > 10) ? 10 : max_notches);
    self->threshold_db = (threshold_db < 3.0f) ? 3.0f : (threshold_db > 20.0f ? 20.0f : threshold_db);
    self->run          = 0;

    const int N = NR0_FFT_SIZE;
    const int M = NR0_MSIZE;
    const int H = NR0_STEP;

    self->fft_in  = (double        *)fftw_malloc(N * sizeof(double));
    self->fft_cmx = (fftw_complex  *)fftw_malloc(M * sizeof(fftw_complex));
    self->fft_out = (double        *)fftw_malloc(N * sizeof(double));
    self->window  = (double        *)fftw_malloc(N * sizeof(double));
    self->mag     = (double        *)fftw_malloc(M * sizeof(double));
    self->ola     = (double        *)fftw_malloc(N * sizeof(double));
    self->shift   = (double        *)fftw_malloc(N * sizeof(double));
    self->inbuf   = (double        *)fftw_malloc(H * sizeof(double));
    self->outq    = (double        *)fftw_malloc(H * sizeof(double));
    self->hold    = (int           *)calloc(M, sizeof(int));

    if (!self->fft_in  || !self->fft_cmx || !self->fft_out ||
        !self->window  || !self->mag     || !self->ola      ||
        !self->shift   || !self->inbuf   || !self->outq     || !self->hold) {
        destroy_nr0(self);
        return NULL;
    }

    self->plan_fwd = fftw_plan_dft_r2c_1d(N, self->fft_in,  self->fft_cmx, FFTW_ESTIMATE);
    self->plan_inv = fftw_plan_dft_c2r_1d(N, self->fft_cmx, self->fft_out, FFTW_ESTIMATE);

    if (!self->plan_fwd || !self->plan_inv) {
        destroy_nr0(self);
        return NULL;
    }

    /* Build periodic Hann window: w[n] = 0.5 * (1 - cos(2*pi*n / N))
     * The periodic form (dividing by N, not N-1) satisfies the COLA property
     * at 75% overlap exactly, meaning the OLA normalization of 2/3 is correct.
     * The symmetric form (N-1 denominator) introduces a small COLA error that
     * causes amplitude ripple in the reconstructed output. */
    for (int n = 0; n < N; n++)
        self->window[n] = 0.5 * (1.0 - cos(2.0 * M_PI * n / (double)N));

    flush_nr0(self);
    return self;
}

void destroy_nr0(NR0 self)
{
    if (!self) return;
    if (self->plan_fwd) { fftw_destroy_plan(self->plan_fwd); self->plan_fwd = NULL; }
    if (self->plan_inv) { fftw_destroy_plan(self->plan_inv); self->plan_inv = NULL; }
    if (self->fft_in)   { fftw_free(self->fft_in);   self->fft_in   = NULL; }
    if (self->fft_cmx)  { fftw_free(self->fft_cmx);  self->fft_cmx  = NULL; }
    if (self->fft_out)  { fftw_free(self->fft_out);  self->fft_out  = NULL; }
    if (self->window)   { fftw_free(self->window);   self->window   = NULL; }
    if (self->mag)      { fftw_free(self->mag);      self->mag      = NULL; }
    if (self->ola)      { fftw_free(self->ola);      self->ola      = NULL; }
    if (self->shift)    { fftw_free(self->shift);    self->shift    = NULL; }
    if (self->inbuf)    { fftw_free(self->inbuf);    self->inbuf    = NULL; }
    if (self->outq)     { fftw_free(self->outq);     self->outq     = NULL; }
    if (self->hold)     { free(self->hold);           self->hold     = NULL; }
    free(self);
}

void flush_nr0(NR0 self)
{
    if (!self) return;
    const int N = NR0_FFT_SIZE;
    const int M = NR0_MSIZE;
    const int H = NR0_STEP;
    memset(self->ola,   0, N * sizeof(double));
    memset(self->shift, 0, N * sizeof(double));
    memset(self->mag,   0, M * sizeof(double));
    if (self->hold) memset(self->hold, 0, M * sizeof(int));
    memset(self->inbuf, 0, H * sizeof(double));
    memset(self->outq,  0, H * sizeof(double));
    self->in_count       = 0;
    self->out_head       = 0;
    self->out_avail      = 0;
    self->active_notches = 0;
}

void xnr0(NR0 self, const float *src, float *dst, int n)
{
    if (!self || n <= 0) return;

    const int H = NR0_STEP;
    const int N = NR0_FFT_SIZE;

    for (int i = 0; i < n; i++) {
        /* Accumulate one new input sample into the hop buffer */
        self->inbuf[self->in_count++] = (double)src[i];

        if (self->in_count == H) {
            /* Advance the shift register by one hop and append the new samples */
            memmove(self->shift, self->shift + H, (N - H) * sizeof(double));
            for (int k = 0; k < H; k++)
                self->shift[N - H + k] = self->inbuf[k];

            /* Run the FFT frame: modifies fft_cmx in-place and writes outq[] */
            nr0_process_frame(self);

            self->in_count  = 0;
            self->out_avail = H;
            self->out_head  = 0;
        }

        /* Drain one processed sample to the output.
         * While the pipeline is filling (first hop) passthrough the input
         * so we do not inject a hop's worth of silence. */
        if (self->out_avail > 0) {
            dst[i] = (float)self->outq[self->out_head++];
            self->out_avail--;
        } else {
            dst[i] = src[i];  /* latency fill: passthrough during first hop */
        }
    }
}
