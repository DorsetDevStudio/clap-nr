/*
 * clap_nr.c  -  CLAP plugin incorporating DSP noise-reduction algorithms
 *
 * Copyright (C) 2025  Station Master
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * NR modes (param id 0):
 *   0 = off
 *   1 = NR1  (ANR  - Adaptive LMS)
 *   2 = NR2  (EMNR - Spectral MMSE)
 *   3 = NR3  (RNNR - RNNoise neural net)   [UI disabled for now]
 *   4 = NR4  (SBNR - libspecbleach)        [UI disabled for now]
 *
 * Extensions implemented:
 *   clap.params       - full parameter set for NR1/NR2
 *   clap.audio-ports  - 1x1 mono
 *   clap.state        - save / load all param values
 *   clap.gui          - Win32 embedded window (gui_win32.c)
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

/* System headers required by NR algorithm structs */
#include <Windows.h>
#include "fftw3.h"
#include "rnnoise.h"
#include <specbleach_adenoiser.h>

#include "clap/clap.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/gui.h"

/* NR algorithm headers */
#include "nr/anr.h"
#include "nr/emnr.h"
#include "nr/rnnr.h"
#include "nr/sbnr.h"

/* Platform GUI */
#include "gui.h"
#include "version.h"

/* -----------------------------------------------------------------------
 * Plugin identity
 * --------------------------------------------------------------------- */
#define PLUGIN_ID      "com.dorsetdevstufio.clap-nr"
#define PLUGIN_NAME    "CLAP NR"
#define PLUGIN_VENDOR  "Station Master"
#define PLUGIN_VERSION CLAP_NR_VERSION_STR
#define PLUGIN_URL     ""

/* EMNR_CHUNK: number of samples fed to xemnr per call.
 * Must be smaller than fsize (4096) so the STFT accumulates real audio
 * across multiple process() blocks.  128 works for any practical host
 * block size (hosts typically use 64-512 frames per block). */
#define EMNR_CHUNK 128

/* -----------------------------------------------------------------------
 * Parameter IDs  (must stay stable - used in saved state)
 * --------------------------------------------------------------------- */
enum {
    PARAM_NR_MODE           = 0,  /* 0-4 stepped: Off/NR1/NR2/NR3/NR4        */
    PARAM_NR4_REDUCTION     = 1,  /* 0-20 dB  (NR4 reduction amount)          */

    /* NR1 - Adaptive LMS (ANR) */
    PARAM_ANR_TAPS          = 2,  /* 16-2048 stepped                           */
    PARAM_ANR_DELAY         = 3,  /* 1-512 stepped                             */
    PARAM_ANR_GAIN          = 4,  /* two_mu:  1e-6 - 0.01                      */
    PARAM_ANR_LEAKAGE       = 5,  /* gamma:   0.0  - 1.0                       */

    /* NR2 - Spectral MMSE (EMNR) */
    PARAM_EMNR_GAIN_METHOD  = 6,  /* 0=RROE, 1=MEPSE, 2=MM-LSA (stepped)     */
    PARAM_EMNR_NPE_METHOD   = 7,  /* 0=OSMS, 1=MMSE           (stepped)       */
    PARAM_EMNR_AE_RUN       = 8,  /* 0=off,  1=on             (stepped)       */

    PARAM_COUNT
};

/* -----------------------------------------------------------------------
 * State save/load format  (bump version when layout changes)
 * --------------------------------------------------------------------- */
#define STATE_VERSION  1U

typedef struct {
    uint32_t version;
    int32_t  nr_mode;
    float    nr4_reduction;
    int32_t  anr_taps;
    int32_t  anr_delay;
    double   anr_gain;
    double   anr_leakage;
    int32_t  emnr_gain_method;
    int32_t  emnr_npe_method;
    int32_t  emnr_ae_run;
} clap_nr_state_t;

/* -----------------------------------------------------------------------
 * Plugin instance data
 * --------------------------------------------------------------------- */
typedef struct {
    clap_plugin_t        plugin;   /* must be first */
    const clap_host_t   *host;

    double   sample_rate;
    uint32_t block_size;

    /* NR algorithm instances — index 0 = L (or mono), index 1 = R.
     * Each channel has its own fully independent NR pipeline so that
     * two SDR receivers on L/R are processed without crosstalk. */
    ANR  anr[2];   /* NR1 */
    EMNR emnr[2];  /* NR2 */
    RNNR rnnr[2];  /* NR3 */
    SBNR sbnr[2];  /* NR4 */

    /* Per-channel double-precision in-place buffers */
    double *buf[2];

    /* ---- Parameter values ---- */
    int    nr_mode;
    float  nr4_reduction;
    int    anr_taps;
    int    anr_delay;
    double anr_gain;
    double anr_leakage;
    int    emnr_gain_method;
    int    emnr_npe_method;
    int    emnr_ae_run;

    /* ---- GUI ---- */
    clap_nr_gui_t *gui;
    bool           gui_floating;

    /* Pending param->host notification (set by GUI, cleared by flush) */
    bool param_dirty[PARAM_COUNT];

    /* Set by handle_param_event (any thread); cleared at the start of
     * process() after applying changes to the live NR instances.
     * Keeping all apply_*() calls inside process() means NR algorithm
     * state is only ever modified from the audio thread, eliminating
     * the race between the GUI/main thread and the audio thread. */
    bool params_dirty;

    /* True between a successful activate() and the subsequent deactivate().
     * process() checks this flag and does passthrough if false, guarding
     * against hosts that call process() outside the active_state window. */
    bool active;

    /* Incremented by process() on entry, decremented on exit.  deactivate()
     * spins on this reaching zero before returning, guaranteeing that
     * plugin_destroy() will never run concurrently with process().  This is
     * the only safe way to allow destroy_emnr() (which calls
     * fftw_destroy_plan()) to run without racing the audio thread. */
    volatile LONG process_depth;
} clap_nr_t;

/* -----------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------- */
static bool                plugin_init(const clap_plugin_t *);
static void                plugin_destroy(const clap_plugin_t *);
static bool                plugin_activate(const clap_plugin_t *, double, uint32_t, uint32_t);
static void                plugin_deactivate(const clap_plugin_t *);
static bool                plugin_start_processing(const clap_plugin_t *);
static void                plugin_stop_processing(const clap_plugin_t *);
static void                plugin_reset(const clap_plugin_t *);
static clap_process_status plugin_process(const clap_plugin_t *, const clap_process_t *);
static const void         *plugin_get_extension(const clap_plugin_t *, const char *);
static void                plugin_on_main_thread(const clap_plugin_t *);
static void                nr_log(const char *fmt, ...);

/* -----------------------------------------------------------------------
 * Helper: apply ANR parameter changes to the live instance
 * --------------------------------------------------------------------- */
static void apply_anr_params_ch(clap_nr_t *self, int ch)
{
    ANR a = self->anr[ch];
    if (!a) return;
    /* Only reset the delay lines when the filter structure changes (tap count
     * or decorrelation delay).  Gain and leakage can be updated in-place
     * without wiping history — doing so on every slider move was the cause
     * of audible glitches during parameter adjustment. */
    bool structural = (a->n_taps != self->anr_taps || a->delay != self->anr_delay);
    a->n_taps = self->anr_taps;
    a->delay  = self->anr_delay;
    a->two_mu = self->anr_gain;
    a->gamma  = self->anr_leakage;
    if (structural) {
        memset(a->d, 0, sizeof(a->d));
        memset(a->w, 0, sizeof(a->w));
        a->in_idx = 0;
    }
}

static void apply_anr_params(clap_nr_t *self)
{
    apply_anr_params_ch(self, 0);
    apply_anr_params_ch(self, 1);
}

/* -----------------------------------------------------------------------
 * Helper: apply EMNR parameter changes to the live instance
 * --------------------------------------------------------------------- */
static void apply_emnr_params(clap_nr_t *self)
{
    for (int ch = 0; ch < 2; ++ch) {
        EMNR e = self->emnr[ch];
        if (!e) continue;
        e->g.gain_method = self->emnr_gain_method;
        e->g.npe_method  = self->emnr_npe_method;
        e->g.ae_run      = self->emnr_ae_run;
    }
}

/* -----------------------------------------------------------------------
 * Helper: apply NR mode (run flags)
 * --------------------------------------------------------------------- */

/* Reset EMNR noise estimator state for a clean entry into NR2 mode.
 *
 * Root cause of NR2 silence: sigma2N is initialised to 0.5 for every bin.
 * Normal float audio has per-bin spectral power well below 0.5, so
 * gamma = lambda_y / sigma2N < 1 on the very first call.  The gain
 * methods interpret gamma < 1 as "more noise than signal" and apply maximum
 * suppression, producing zero output forever (the estimator never escapes
 * because there is no signal passing through to drive it).
 *
 * Fix: set sigma2N (and related tracking state) to near-zero so that
 * gamma >> 1 from the very first frame.  The Wiener filter then passes
 * audio at near-unity gain while the noise estimator adapts to the actual
 * noise floor over the next few seconds. */

static void reset_emnr_for_nr2_entry(EMNR e)
{
    int k, u;
    if (!e) return;
    /* Flush ring-buffer / overlap-add state for a clean restart */
    flush_emnr(e);
    /* Reset per-bin noise estimates and gain-smoother state */
    for (k = 0; k < e->np.msize; k++) {
        e->np.sigma2N[k]    = 1.0e-12;
        e->np.p[k]          = 1.0e-12;
        e->np.pbar[k]       = 1.0e-12;
        e->np.p2bar[k]      = 1.0e-24;
        e->np.pmin_u[k]     = 1.0e-12;
        e->np.actmin[k]     = 1.0e300;   /* restart running-minimum search */
        e->np.actmin_sub[k] = 1.0e300;
        e->g.prev_mask[k]   = 1.0;       /* start with unity gain */
        e->g.prev_gamma[k]  = 1.0;
        e->g.lambda_d[k]    = 1.0e-12;   /* shared lambda_d used by all NPE */
    }
    for (u = 0; u < e->np.U; u++)
        for (k = 0; k < e->np.msize; k++)
            e->np.actminbuff[u][k] = 1.0e300;
    e->np.subwc   = 1;
    e->np.amb_idx = 0;
    e->np.alphaC  = 1.0;
    /* Reset MMSE NPE noise estimates */
    for (k = 0; k < e->nps.msize; k++)
        e->nps.sigma2N[k] = 1.0e-12;
}

static void apply_nr_mode(clap_nr_t *self, int new_mode)
{
    /* Detect transition INTO NR2 before updating run flags */
    bool entering_nr2 = (new_mode == 2) &&
                        (self->emnr[0] ? (self->emnr[0]->run == 0) : false);
    self->nr_mode = new_mode;
    for (int ch = 0; ch < 2; ++ch) {
        if (self->anr[ch])  self->anr[ch]->run  = (new_mode == 1) ? 1 : 0;
        if (self->emnr[ch]) self->emnr[ch]->run = (new_mode == 2) ? 1 : 0;
        if (self->rnnr[ch]) self->rnnr[ch]->run = (new_mode == 3) ? 1 : 0;
        if (self->sbnr[ch]) self->sbnr[ch]->run = (new_mode == 4) ? 1 : 0;
        if (entering_nr2 && self->emnr[ch])
            reset_emnr_for_nr2_entry(self->emnr[ch]);
    }
    nr_log("apply_nr_mode: mode=%d  emnr[0].run=%d emnr[1].run=%d  reset=%d",
           new_mode,
           (self->emnr[0] ? self->emnr[0]->run : -1),
           (self->emnr[1] ? self->emnr[1]->run : -1),
           (int)entering_nr2);
}

/* -----------------------------------------------------------------------
 * Helper: dispatch a single incoming CLAP param event
 * --------------------------------------------------------------------- */
static void handle_param_event(clap_nr_t *self, const clap_event_param_value_t *pv)
{
    /* Update only the cached parameter values.  Do NOT call apply_*() here.
     * This function is called from both the main thread (GUI callback) and the
     * audio thread (in-band host events).  Touching live NR algorithm state
     * from two threads simultaneously was the primary cause of crashes.
     * All apply_*() calls are deferred to the start of plugin_process(). */
    switch (pv->param_id) {
    case PARAM_NR_MODE:          self->nr_mode          = (int)pv->value;  break;
    case PARAM_NR4_REDUCTION:    self->nr4_reduction    = (float)pv->value; break;
    case PARAM_ANR_TAPS:         self->anr_taps         = (int)pv->value;  break;
    case PARAM_ANR_DELAY:        self->anr_delay        = (int)pv->value;  break;
    case PARAM_ANR_GAIN:         self->anr_gain         = pv->value;       break;
    case PARAM_ANR_LEAKAGE:      self->anr_leakage      = pv->value;       break;
    case PARAM_EMNR_GAIN_METHOD: self->emnr_gain_method = (int)pv->value;  break;
    case PARAM_EMNR_NPE_METHOD:  self->emnr_npe_method  = (int)pv->value;  break;
    case PARAM_EMNR_AE_RUN:      self->emnr_ae_run      = (int)pv->value;  break;
    default: break;
    }
    self->params_dirty = true;
}

/* -----------------------------------------------------------------------
 * GUI -> plugin parameter callback  (called on main thread)
 * --------------------------------------------------------------------- */
static void on_gui_param_change(void *plugin_ptr, clap_id param_id, double value)
{
    clap_nr_t *self = (clap_nr_t *)plugin_ptr;

    /* Apply the value locally */
    clap_event_param_value_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.param_id = param_id;
    fake.value    = value;
    handle_param_event(self, &fake);

    /* Mark dirty so params_flush notifies the host */
    if (param_id < PARAM_COUNT)
        self->param_dirty[param_id] = true;

    /* Ask the host to call params_flush so it records the new value */
    const clap_host_params_t *hp =
        (const clap_host_params_t *)self->host->get_extension(self->host, CLAP_EXT_PARAMS);
    if (hp) hp->request_flush(self->host);
}

/* -----------------------------------------------------------------------
 * Plugin vtable
 * --------------------------------------------------------------------- */
static bool plugin_init(const clap_plugin_t *p) { (void)p; return true; }

static void plugin_destroy(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;

    /* Guard against hosts that call destroy() without a prior deactivate().
     * Setting active=false and draining process_depth ensures that
     * destroy_emnr() -> fftw_destroy_plan() never races fftw_execute(),
     * which would cause FFTW to call abort() and kill the host process.
     * When deactivate() was already called this drain returns instantly. */
    self->active = false;
    MemoryBarrier();
    while (InterlockedCompareExchange(&self->process_depth, 0, 0) != 0)
        Sleep(0);

    if (self->gui) { gui_destroy(self->gui); self->gui = NULL; }

    __try {
        for (int ch = 0; ch < 2; ++ch) {
            if (self->anr[ch])  { destroy_anr (self->anr[ch]);  self->anr[ch]  = NULL; }
            if (self->emnr[ch]) { destroy_emnr(self->emnr[ch]); self->emnr[ch] = NULL; }
            if (self->rnnr[ch]) { destroy_rnnr(self->rnnr[ch]); self->rnnr[ch] = NULL; }
            if (self->sbnr[ch]) { destroy_sbnr(self->sbnr[ch]); self->sbnr[ch] = NULL; }
            free(self->buf[ch]); self->buf[ch] = NULL;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        nr_log("destroy: SEH exception 0x%08lX", (unsigned long)GetExceptionCode());
    }

    free(self);
}

/* -----------------------------------------------------------------------
 * Diagnostic log helper  -  appends one line to %TEMP%\clap-nr.log
 * --------------------------------------------------------------------- */
static void nr_log(const char *fmt, ...)
{
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return;
    strncat_s(path, MAX_PATH, "clap-nr.log", _TRUNCATE);
    FILE *f = NULL;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static bool plugin_activate(const clap_plugin_t *p, double sr,
                             uint32_t min_frames, uint32_t max_frames)
{
    clap_nr_t *self = (clap_nr_t *)p;
    (void)min_frames;

    bool ok = false;
    __try {
        if (self->buf[0] == NULL) {
            /* ---- First activation: allocate buffers and create NR instances ---- */
            self->sample_rate = sr;
            self->block_size  = max_frames;

            for (int ch = 0; ch < 2; ++ch) {
                /* NR algorithms use interleaved complex (I/Q) layout:
                 * index [2*i+0] = real sample, [2*i+1] = imaginary (kept 0). */
                /* Allocate EMNR_CHUNK extra headroom so the EMNR chunked-stride
                 * loop never reads/writes past the end of the buffer when
                 * max_frames is not a multiple of EMNR_CHUNK (e.g. 480 at 48 kHz).
                 * n_rounded = ceil(max_frames/EMNR_CHUNK)*EMNR_CHUNK can exceed
                 * max_frames by up to EMNR_CHUNK-1 samples; the extra allocation
                 * covers that entire overflow region with valid zeroed memory. */
                self->buf[ch] = (double *)calloc(2 * (max_frames + EMNR_CHUNK), sizeof(double));
                if (!self->buf[ch]) { nr_log("activate: calloc failed ch=%d", ch); __leave; }

                nr_log("activate: creating ANR ch=%d sr=%.0f frames=%u", ch, sr, max_frames);
                self->anr[ch]  = create_anr(0, 0, (int)max_frames,
                                            self->buf[ch], self->buf[ch],
                                            2048,
                                            self->anr_taps, self->anr_delay,
                                            self->anr_gain, self->anr_leakage,
                                            120.0, 120.0, 200.0, 0.001, 6.25e-10, 1.0, 3.0);

                nr_log("activate: creating EMNR ch=%d", ch);
                /* EMNR must be created with a small fixed bsize that matches
                 * how many samples we feed it per call.  Using max_frames here
                 * was the NR2 silence bug: the host delivers ~480 samples per
                 * block, but EMNR thought it was processing 8192, so the STFT
                 * frames were almost entirely zero-padded and lambda_y stayed 0. */
                self->emnr[ch] = create_emnr(0, 0, EMNR_CHUNK,
                                             self->buf[ch], self->buf[ch],
                                             4096, 4, (int)sr, 0, 1.0,
                                             self->emnr_gain_method,
                                             self->emnr_npe_method,
                                             self->emnr_ae_run);

                nr_log("activate: creating RNNR ch=%d", ch);
                self->rnnr[ch] = create_rnnr(0, 0, (int)max_frames,
                                             self->buf[ch], self->buf[ch], (int)sr);

                nr_log("activate: creating SBNR ch=%d", ch);
                self->sbnr[ch] = create_sbnr(0, 0, (int)max_frames,
                                             self->buf[ch], self->buf[ch], (int)sr);

                if (!self->anr[ch] || !self->emnr[ch] || !self->rnnr[ch] || !self->sbnr[ch]) {
                    nr_log("activate: create_* returned NULL ch=%d anr=%p emnr=%p rnnr=%p sbnr=%p",
                           ch, (void*)self->anr[ch], (void*)self->emnr[ch],
                           (void*)self->rnnr[ch], (void*)self->sbnr[ch]);
                    __leave;
                }
            }
        } else {
            /* ---- Re-activation after a deactivate / enable-disable cycle ----
             *
             * Re-use existing NR instances rather than destroying and re-creating
             * them.  The critical reason: EMNR owns FFTW3 plans.  Calling
             * fftw_destroy_plan() on the main thread while the audio thread may
             * still be a few microseconds inside fftw_execute() causes FFTW3 to
             * call abort(), which kills the entire host process -- this cannot be
             * caught with Windows SEH.  By keeping the instances alive and just
             * flushing their state here, we avoid any plan teardown, which is the
             * most common cause of "enable/disable crashes the host".
             *
             * All NR memory is released in plugin_destroy() instead, which is
             * guaranteed to be called only after audio has fully stopped.        */

            if (max_frames != self->block_size) {
                /* Block size changed -- reallocate shared buffers and notify instances */
                nr_log("reactivate: block size %u -> %u", self->block_size, max_frames);
                for (int ch = 0; ch < 2; ++ch) {
                    free(self->buf[ch]);
                    /* Same EMNR_CHUNK headroom as the first-activation path. */
                    self->buf[ch] = (double *)calloc(2 * (max_frames + EMNR_CHUNK), sizeof(double));
                    if (!self->buf[ch]) { nr_log("reactivate: calloc failed ch=%d", ch); __leave; }

                    if (self->anr[ch]) {
                        setSize_anr   (self->anr[ch],  (int)max_frames);
                        setBuffers_anr(self->anr[ch],  self->buf[ch], self->buf[ch]);
                    }
                    if (self->emnr[ch]) {
                        setSize_emnr   (self->emnr[ch], (int)max_frames);
                        setBuffers_emnr(self->emnr[ch], self->buf[ch], self->buf[ch]);
                    }
                    if (self->rnnr[ch]) {
                        setSize_rnnr   (self->rnnr[ch], (int)max_frames);
                        setBuffers_rnnr(self->rnnr[ch], self->buf[ch], self->buf[ch]);
                    }
                    if (self->sbnr[ch]) {
                        setSize_sbnr   (self->sbnr[ch], (int)max_frames);
                        setBuffers_sbnr(self->sbnr[ch], self->buf[ch], self->buf[ch]);
                    }
                }
                self->block_size = max_frames;
            }

            if ((int)sr != (int)self->sample_rate) {
                nr_log("reactivate: sample rate %.0f -> %.0f", self->sample_rate, sr);
                for (int ch = 0; ch < 2; ++ch) {
                    if (self->emnr[ch]) setSamplerate_emnr(self->emnr[ch], (int)sr);
                    if (self->rnnr[ch]) setSamplerate_rnnr(self->rnnr[ch], (int)sr);
                    if (self->sbnr[ch]) setSamplerate_sbnr(self->sbnr[ch], (int)sr);
                }
                self->sample_rate = sr;
            }

            /* Flush filter / accumulator state for a clean restart */
            for (int ch = 0; ch < 2; ++ch) {
                if (self->anr[ch])  flush_anr (self->anr[ch]);
                if (self->emnr[ch]) flush_emnr(self->emnr[ch]);
                /* RNNR / SBNR have no flush function; zeroing the shared buffer
                 * drains their ring buffers on the next process() call. */
                if (self->buf[ch])
                    memset(self->buf[ch], 0, 2 * (self->block_size + EMNR_CHUNK) * sizeof(double));
            }
            nr_log("reactivate: flushed, sr=%.0f frames=%u", sr, max_frames);
        }

        apply_nr_mode(self, self->nr_mode);
        nr_log("activate: success mode=%d", self->nr_mode);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        nr_log("activate: SEH exception 0x%08lX -- NR disabled",
               (unsigned long)GetExceptionCode());
        for (int ch = 0; ch < 2; ++ch) {
            self->anr[ch] = NULL; self->emnr[ch] = NULL;
            self->rnnr[ch] = NULL; self->sbnr[ch] = NULL;
            free(self->buf[ch]); self->buf[ch] = NULL;
        }
        self->nr_mode = 0;
        ok = true;
    }

    if (ok) self->active = true;
    return ok;
}

static void plugin_deactivate(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;

    /* Signal the audio thread to stop doing real work. */
    self->active = false;

    /* Drain: spin until any currently-executing process() call has returned.
     *
     * Why this matters: deactivate() is called on the main thread.  The host
     * then calls plugin_destroy() — also on the main thread — which calls
     * destroy_emnr() → fftw_destroy_plan().  If the audio thread is still
     * inside fftw_execute() at that moment, FFTW3 calls abort() and kills
     * the entire host process.  Windows SEH cannot catch abort().
     *
     * By waiting here for process_depth to hit zero we guarantee that no
     * process() call is in flight before control returns to the host.  The
     * wait is bounded by one audio buffer (typically 1-10 ms) so it does
     * not block the UI perceptibly. */
    while (InterlockedCompareExchange(&self->process_depth, 0, 0) != 0)
        Sleep(0);
}

static bool plugin_start_processing(const clap_plugin_t *p) { (void)p; return true; }
static void plugin_stop_processing (const clap_plugin_t *p) { (void)p; }

static void plugin_reset(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    /* Flush algorithm internal state so a transport jump or plugin
     * re-insertion starts clean without residual filter history. */
    __try {
        for (int ch = 0; ch < 2; ++ch) {
            if (self->anr[ch])  flush_anr(self->anr[ch]);
            if (self->emnr[ch]) flush_emnr(self->emnr[ch]);
            /* RNNR and SBNR have no flush function; zero the shared I/O
             * buffer which effectively drains their ring buffers on the
             * next process() call.  Clear the full allocation (including
             * the EMNR_CHUNK headroom) to avoid stale data in the overlap
             * region after a block-size or sample-rate change. */
            if (self->buf[ch] && self->block_size > 0)
                memset(self->buf[ch], 0, 2 * (self->block_size + EMNR_CHUNK) * sizeof(double));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        nr_log("reset: SEH exception 0x%08lX", (unsigned long)GetExceptionCode());
    }
}

static clap_process_status plugin_process(const clap_plugin_t *p, const clap_process_t *proc)
{
    clap_nr_t *self = (clap_nr_t *)p;

    /* Register this call so deactivate() can wait for us to finish. */
    InterlockedIncrement(&self->process_depth);

    /* Guard: passthrough if deactivate() has already been called or activate()
     * has never been called.  Handles hosts that call process() outside the
     * active_state window defined by the CLAP spec. */
    if (!self->active) {
        uint32_t nc = proc->frames_count;
        if (nc > 0 && proc->audio_inputs_count > 0 && proc->audio_outputs_count > 0) {
            const clap_audio_buffer_t *ib = &proc->audio_inputs[0];
            const clap_audio_buffer_t *ob = &proc->audio_outputs[0];
            uint32_t cch = ib->channel_count < ob->channel_count
                         ? ib->channel_count : ob->channel_count;
            for (uint32_t ch = 0; ch < cch; ++ch) {
                float *src = ib->data32[ch], *dst = ob->data32[ch];
                if (src && dst && src != dst) memcpy(dst, src, nc * sizeof(float));
            }
        }
        InterlockedDecrement(&self->process_depth);
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t n = proc->frames_count;

    /* Step 1: consume in-band parameter events from the host. */
    const clap_input_events_t *ev = proc->in_events;
    uint32_t nevents = ev->size(ev);
    for (uint32_t i = 0; i < nevents; ++i) {
        const clap_event_header_t *hdr = ev->get(ev, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE)
            handle_param_event(self, (const clap_event_param_value_t *)hdr);
    }

    /* Step 2: apply any pending parameter changes to the live NR instances.
     * This is the ONE place that modifies algorithm state, so it runs
     * exclusively on the audio thread with no concurrent main-thread access. */
    if (self->params_dirty) {
        apply_nr_mode(self, self->nr_mode);
        apply_anr_params(self);
        apply_emnr_params(self);
        for (int _ch = 0; _ch < 2; ++_ch)
            if (self->sbnr[_ch]) self->sbnr[_ch]->reduction_amount = self->nr4_reduction;
        self->params_dirty = false;
    }

    if (n == 0 || proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
        InterlockedDecrement(&self->process_depth);
        return CLAP_PROCESS_CONTINUE;
    }

    const clap_audio_buffer_t *in_buf  = &proc->audio_inputs[0];
    const clap_audio_buffer_t *out_buf = &proc->audio_outputs[0];
    uint32_t in_ch  = in_buf->channel_count;
    uint32_t out_ch = out_buf->channel_count;

    /* Bypass: pass every input channel straight to the matching output */
    if (self->nr_mode == 0) {
        uint32_t copy_ch = (in_ch < out_ch) ? in_ch : out_ch;
        for (uint32_t ch = 0; ch < copy_ch; ++ch) {
            float *src = in_buf->data32[ch];
            float *dst = out_buf->data32[ch];
            if (src && dst && src != dst)
                memcpy(dst, src, n * sizeof(float));
        }
        InterlockedDecrement(&self->process_depth);
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t proc_ch = (in_ch < out_ch) ? in_ch : out_ch;
    if (proc_ch > 2) proc_ch = 2;

    for (uint32_t ch = 0; ch < proc_ch; ++ch) {
        float *src = in_buf->data32[ch];
        float *dst = out_buf->data32[ch];

        /* Passthrough if audio pointers or NR buffer are missing */
        if (!src || !dst || !self->buf[ch]) {
            if (src && dst && src != dst) memcpy(dst, src, n * sizeof(float));
            continue;
        }

        __try {
            /* Load real samples into even slots.  Zero imaginary (odd) slots
             * explicitly on every call — previous NR output may have written
             * non-zero values there, which would corrupt the next frame. */
            for (uint32_t i = 0; i < n; ++i) {
                self->buf[ch][2 * i]     = (double)src[i];
                self->buf[ch][2 * i + 1] = 0.0;
            }

            /* Compute the EMNR stride ceiling before zero-padding so the
             * same value is used for both the pad extent and the loop limit.
             * n_rounded can exceed blk by up to EMNR_CHUNK-1 when max_frames
             * is not a multiple of EMNR_CHUNK (e.g. 480 at 48 kHz).  The
             * buffer was allocated with EMNR_CHUNK extra headroom exactly to
             * cover this, making the write to buf[ch][2*blk..2*n_rounded-1]
             * safe.  Zeroing up to pad_to ensures those extra slots hold
             * clean zeros rather than stale output from a previous call. */
            uint32_t blk      = self->block_size;
            uint32_t n_rounded = ((n + EMNR_CHUNK - 1) / EMNR_CHUNK) * EMNR_CHUNK;
            uint32_t pad_to   = (n_rounded > blk) ? n_rounded : blk;
            for (uint32_t i = n; i < pad_to; ++i) {
                self->buf[ch][2 * i]     = 0.0;
                self->buf[ch][2 * i + 1] = 0.0;
            }

            /* ANR is a sample-by-sample LMS filter: updating buff_size to
             * the actual frame count is safe and avoids looping over zeros. */
            if (self->anr[ch]) {
                self->anr[ch]->buff_size = (int)n;
                xanr(self->anr[ch], 0);
            }
            if (self->emnr[ch]) {
                /* EMNR processes audio in fixed EMNR_CHUNK-sample strides.
                 * buf[ch] is zero-padded to n_rounded (above) so every stride
                 * reads only zeroed or valid audio data within the allocation. */
                EMNR em = self->emnr[ch];
                uint32_t pos = 0;
                while (pos < n_rounded) {
                    setBuffers_emnr(em, self->buf[ch] + 2 * pos,
                                        self->buf[ch] + 2 * pos);
                    xemnr(em, 0);
                    pos += EMNR_CHUNK;
                }
                /* Restore nominal pointer for consistency */
                setBuffers_emnr(em, self->buf[ch], self->buf[ch]);
            }
            if (self->rnnr[ch]) xrnnr(self->rnnr[ch], 0);
            if (self->sbnr[ch]) xsbnr(self->sbnr[ch], 0);

            /* Read back the real (even) output slots only */
            for (uint32_t i = 0; i < n; ++i)
                dst[i] = (float)self->buf[ch][2 * i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            static BOOL logged = FALSE;
            if (!logged) {
                nr_log("process: SEH exception 0x%08lX ch=%u -- bypassing",
                       (unsigned long)GetExceptionCode(), ch);
                logged = TRUE;
            }
            if (src != dst) memcpy(dst, src, n * sizeof(float));
        }
    }

    InterlockedDecrement(&self->process_depth);
    return CLAP_PROCESS_CONTINUE;
}

/* -----------------------------------------------------------------------
 * params extension
 * --------------------------------------------------------------------- */
static uint32_t params_count(const clap_plugin_t *p) { (void)p; return PARAM_COUNT; }

static bool params_get_info(const clap_plugin_t *p, uint32_t idx, clap_param_info_t *info)
{
    (void)p;
    memset(info, 0, sizeof(*info));
    switch (idx) {
    case PARAM_NR_MODE:
        info->id = PARAM_NR_MODE;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR Mode");
        snprintf(info->module, sizeof(info->module), "");
        info->min_value = 0; info->max_value = 4; info->default_value = 0;
        return true;
    case PARAM_NR4_REDUCTION:
        info->id = PARAM_NR4_REDUCTION;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR4 Reduction (dB)");
        snprintf(info->module, sizeof(info->module), "NR4");
        info->min_value = 0; info->max_value = 20; info->default_value = 10;
        return true;
    case PARAM_ANR_TAPS:
        info->id = PARAM_ANR_TAPS;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR1 Taps");
        snprintf(info->module, sizeof(info->module), "NR1");
        info->min_value = 16; info->max_value = 2048; info->default_value = 64;
        return true;
    case PARAM_ANR_DELAY:
        info->id = PARAM_ANR_DELAY;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR1 Delay");
        snprintf(info->module, sizeof(info->module), "NR1");
        info->min_value = 1; info->max_value = 512; info->default_value = 16;
        return true;
    case PARAM_ANR_GAIN:
        info->id = PARAM_ANR_GAIN;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR1 Gain (two_mu)");
        snprintf(info->module, sizeof(info->module), "NR1");
        info->min_value = 1e-6; info->max_value = 0.01; info->default_value = 0.0001;
        return true;
    case PARAM_ANR_LEAKAGE:
        info->id = PARAM_ANR_LEAKAGE;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR1 Leakage (gamma)");
        snprintf(info->module, sizeof(info->module), "NR1");
        info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.1;
        return true;
    case PARAM_EMNR_GAIN_METHOD:
        info->id = PARAM_EMNR_GAIN_METHOD;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR2 Gain Method");
        snprintf(info->module, sizeof(info->module), "NR2");
        info->min_value = 0; info->max_value = 2; info->default_value = 2;
        return true;
    case PARAM_EMNR_NPE_METHOD:
        info->id = PARAM_EMNR_NPE_METHOD;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR2 NPE Method");
        snprintf(info->module, sizeof(info->module), "NR2");
        info->min_value = 0; info->max_value = 1; info->default_value = 0;
        return true;
    case PARAM_EMNR_AE_RUN:
        info->id = PARAM_EMNR_AE_RUN;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR2 Audio Enhance");
        snprintf(info->module, sizeof(info->module), "NR2");
        info->min_value = 0; info->max_value = 1; info->default_value = 1;
        return true;
    default: return false;
    }
}

static bool params_get_value(const clap_plugin_t *p, clap_id id, double *out)
{
    clap_nr_t *self = (clap_nr_t *)p;
    switch (id) {
    case PARAM_NR_MODE:          *out = (double)self->nr_mode;           return true;
    case PARAM_NR4_REDUCTION:    *out = (double)self->nr4_reduction;     return true;
    case PARAM_ANR_TAPS:         *out = (double)self->anr_taps;          return true;
    case PARAM_ANR_DELAY:        *out = (double)self->anr_delay;         return true;
    case PARAM_ANR_GAIN:         *out = self->anr_gain;                  return true;
    case PARAM_ANR_LEAKAGE:      *out = self->anr_leakage;               return true;
    case PARAM_EMNR_GAIN_METHOD: *out = (double)self->emnr_gain_method;  return true;
    case PARAM_EMNR_NPE_METHOD:  *out = (double)self->emnr_npe_method;   return true;
    case PARAM_EMNR_AE_RUN:      *out = (double)self->emnr_ae_run;       return true;
    default: return false;
    }
}

static const char *nr_mode_names[]         = { "Off", "NR1 (ANR)", "NR2 (EMNR)", "NR3 (RNNR)", "NR4 (SBNR)" };
static const char *emnr_gain_method_names[]= { "RROE", "MEPSE", "MM-LSA" };
static const char *emnr_npe_method_names[] = { "OSMS", "MMSE" };

static bool params_value_to_text(const clap_plugin_t *p, clap_id id, double val,
                                  char *buf, uint32_t size)
{
    (void)p;
    switch (id) {
    case PARAM_NR_MODE: {
        int i = (int)val;
        snprintf(buf, size, "%s", (i >= 0 && i <= 4) ? nr_mode_names[i] : "?");
        return true; }
    case PARAM_NR4_REDUCTION:
        snprintf(buf, size, "%.1f dB", val); return true;
    case PARAM_ANR_TAPS:
    case PARAM_ANR_DELAY:
        snprintf(buf, size, "%d", (int)val); return true;
    case PARAM_ANR_GAIN:
    case PARAM_ANR_LEAKAGE:
        snprintf(buf, size, "%.6f", val); return true;
    case PARAM_EMNR_GAIN_METHOD: {
        int i = (int)val;
        snprintf(buf, size, "%s", (i >= 0 && i <= 2) ? emnr_gain_method_names[i] : "?");
        return true; }
    case PARAM_EMNR_NPE_METHOD: {
        int i = (int)val;
        snprintf(buf, size, "%s", (i >= 0 && i <= 1) ? emnr_npe_method_names[i] : "?");
        return true; }
    case PARAM_EMNR_AE_RUN:
        snprintf(buf, size, "%s", (int)val ? "On" : "Off"); return true;
    default: return false;
    }
}

static bool params_text_to_value(const clap_plugin_t *p, clap_id id,
                                  const char *txt, double *out)
{
    (void)p;
    switch (id) {
    case PARAM_NR_MODE:
        for (int i = 0; i <= 4; ++i)
            if (strcmp(txt, nr_mode_names[i]) == 0) { *out = i; return true; }
        *out = atof(txt); return true;
    case PARAM_EMNR_GAIN_METHOD:
        for (int i = 0; i <= 2; ++i)
            if (strcmp(txt, emnr_gain_method_names[i]) == 0) { *out = i; return true; }
        *out = atof(txt); return true;
    case PARAM_EMNR_NPE_METHOD:
        for (int i = 0; i <= 1; ++i)
            if (strcmp(txt, emnr_npe_method_names[i]) == 0) { *out = i; return true; }
        *out = atof(txt); return true;
    default:
        *out = atof(txt); return true;
    }
}

/* Push a PARAM_VALUE event into out_events for a given parameter */
static void push_param_event(const clap_output_events_t *out,
                              clap_id param_id, double value)
{
    clap_event_param_value_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.header.size     = sizeof(ev);
    ev.header.type     = CLAP_EVENT_PARAM_VALUE;
    ev.header.flags    = 0;
    ev.header.time     = 0;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.param_id        = param_id;
    ev.value           = value;
    out->try_push(out, &ev.header);
}

static void params_flush(const clap_plugin_t *p,
                          const clap_input_events_t *in_ev,
                          const clap_output_events_t *out_ev)
{
    clap_nr_t *self = (clap_nr_t *)p;

    /* Apply any incoming host->plugin param changes */
    uint32_t n = in_ev->size(in_ev);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t *hdr = in_ev->get(in_ev, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE)
            handle_param_event(self, (const clap_event_param_value_t *)hdr);
    }

    /* Notify host of any GUI-driven changes */
    double val;
    for (clap_id id = 0; id < PARAM_COUNT; ++id) {
        if (self->param_dirty[id]) {
            self->param_dirty[id] = false;
            params_get_value(p, id, &val);
            push_param_event(out_ev, id, val);
        }
    }
}

static const clap_plugin_params_t s_params = {
    .count          = params_count,
    .get_info       = params_get_info,
    .get_value      = params_get_value,
    .value_to_text  = params_value_to_text,
    .text_to_value  = params_text_to_value,
    .flush          = params_flush,
};

/* -----------------------------------------------------------------------
 * state extension  (save / load)
 * --------------------------------------------------------------------- */
static bool state_save(const clap_plugin_t *p, const clap_ostream_t *stream)
{
    clap_nr_t *self = (clap_nr_t *)p;
    clap_nr_state_t s;
    s.version           = STATE_VERSION;
    s.nr_mode           = self->nr_mode;
    s.nr4_reduction     = self->nr4_reduction;
    s.anr_taps          = self->anr_taps;
    s.anr_delay         = self->anr_delay;
    s.anr_gain          = self->anr_gain;
    s.anr_leakage       = self->anr_leakage;
    s.emnr_gain_method  = self->emnr_gain_method;
    s.emnr_npe_method   = self->emnr_npe_method;
    s.emnr_ae_run       = self->emnr_ae_run;
    return stream->write(stream, &s, sizeof(s)) == (int64_t)sizeof(s);
}

static bool state_load(const clap_plugin_t *p, const clap_istream_t *stream)
{
    clap_nr_t *self = (clap_nr_t *)p;
    clap_nr_state_t s;
    if (stream->read(stream, &s, sizeof(s)) != (int64_t)sizeof(s)) return false;
    if (s.version != STATE_VERSION) return false;

    self->nr4_reduction     = s.nr4_reduction;
    self->anr_taps          = (s.anr_taps  >= 16  && s.anr_taps  <= 2048) ? s.anr_taps  : 64;
    self->anr_delay         = (s.anr_delay >= 1   && s.anr_delay <= 512)  ? s.anr_delay : 16;
    self->anr_gain          = (s.anr_gain  >= 1e-6 && s.anr_gain <= 0.01) ? s.anr_gain  : 0.0001;
    self->anr_leakage       = (s.anr_leakage >= 0.0 && s.anr_leakage <= 1.0) ? s.anr_leakage : 0.1;
    self->emnr_gain_method  = (s.emnr_gain_method >= 0 && s.emnr_gain_method <= 2) ? s.emnr_gain_method : 2;
    self->emnr_npe_method   = (s.emnr_npe_method  >= 0 && s.emnr_npe_method  <= 1) ? s.emnr_npe_method  : 0;
    self->emnr_ae_run       = (s.emnr_ae_run == 0 || s.emnr_ae_run == 1) ? s.emnr_ae_run : 1;

    apply_nr_mode(self, (s.nr_mode >= 0 && s.nr_mode <= 4) ? s.nr_mode : 0);
    apply_anr_params(self);
    apply_emnr_params(self);

    if (self->gui) {
        gui_set_param(self->gui, PARAM_NR_MODE,          (double)self->nr_mode);
        gui_set_param(self->gui, PARAM_ANR_TAPS,         (double)self->anr_taps);
        gui_set_param(self->gui, PARAM_ANR_DELAY,        (double)self->anr_delay);
        gui_set_param(self->gui, PARAM_ANR_GAIN,         self->anr_gain);
        gui_set_param(self->gui, PARAM_ANR_LEAKAGE,      self->anr_leakage);
        gui_set_param(self->gui, PARAM_EMNR_GAIN_METHOD, (double)self->emnr_gain_method);
        gui_set_param(self->gui, PARAM_EMNR_NPE_METHOD,  (double)self->emnr_npe_method);
        gui_set_param(self->gui, PARAM_EMNR_AE_RUN,      (double)self->emnr_ae_run);
    }
    return true;
}

static const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load,
};

/* -----------------------------------------------------------------------
 * gui extension
 * --------------------------------------------------------------------- */
static bool gui_is_api_supported(const clap_plugin_t *p, const char *api, bool is_floating)
{
    (void)p; (void)is_floating;
#ifdef _WIN32
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
    return false;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t *p, const char **api, bool *is_floating)
{
    (void)p;
#ifdef _WIN32
    *api = CLAP_WINDOW_API_WIN32;
    *is_floating = false;
    return true;
#else
    (void)api; (void)is_floating;
    return false;
#endif
}

static bool gui_plugin_create(const clap_plugin_t *p, const char *api, bool is_floating)
{
    clap_nr_t *self = (clap_nr_t *)p;
    (void)api;
    if (self->gui) return true;
    self->gui_floating = is_floating;
    self->gui = gui_create(self, on_gui_param_change);
    return self->gui != NULL;
}

static void gui_plugin_destroy(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (self->gui) { gui_destroy(self->gui); self->gui = NULL; }
}

static bool gui_set_scale(const clap_plugin_t *p, double scale) { (void)p; (void)scale; return false; }

static bool plugin_gui_get_size(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) { *w = 500; *h = 400; return true; }
    gui_get_size(self->gui, w, h);
    return true;
}

static bool gui_can_resize(const clap_plugin_t *p) { (void)p; return false; }

static bool gui_get_resize_hints(const clap_plugin_t *p, clap_gui_resize_hints_t *h)
{
    (void)p;
    memset(h, 0, sizeof(*h));
    h->can_resize_horizontally = false;
    h->can_resize_vertically   = false;
    h->preserve_aspect_ratio   = false;
    return false;
}

static bool gui_adjust_size(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
    (void)p;
    /* Enforce minimum size matching the fixed control layout */
    if (*w < 500) *w = 500;
    if (*h < 400) *h = 400;
    return true;
}

static bool gui_set_size(const clap_plugin_t *p, uint32_t w, uint32_t h)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return false;
    return gui_resize(self->gui, w, h);
}

static bool gui_plugin_set_parent(const clap_plugin_t *p, const clap_window_t *window)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return false;
    return gui_set_parent(self->gui, window);
}

static bool gui_set_transient(const clap_plugin_t *p, const clap_window_t *w)
{
    (void)p; (void)w; return false;
}

static void gui_suggest_title(const clap_plugin_t *p, const char *title)
{
    (void)p; (void)title;
}

static bool gui_plugin_show(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return false;
    /* Sync current values into the GUI before making it visible */
    gui_set_param(self->gui, PARAM_NR_MODE,          (double)self->nr_mode);
    gui_set_param(self->gui, PARAM_ANR_TAPS,         (double)self->anr_taps);
    gui_set_param(self->gui, PARAM_ANR_DELAY,        (double)self->anr_delay);
    gui_set_param(self->gui, PARAM_ANR_GAIN,         self->anr_gain);
    gui_set_param(self->gui, PARAM_ANR_LEAKAGE,      self->anr_leakage);
    gui_set_param(self->gui, PARAM_EMNR_GAIN_METHOD, (double)self->emnr_gain_method);
    gui_set_param(self->gui, PARAM_EMNR_NPE_METHOD,  (double)self->emnr_npe_method);
    gui_set_param(self->gui, PARAM_EMNR_AE_RUN,      (double)self->emnr_ae_run);
    return gui_show(self->gui);
}

static bool gui_plugin_hide(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return false;
    return gui_hide(self->gui);
}

static const clap_plugin_gui_t s_gui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_plugin_create,
    .destroy           = gui_plugin_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = plugin_gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_plugin_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_plugin_show,
    .hide              = gui_plugin_hide,
};

/* -----------------------------------------------------------------------
 * get_extension  -  main dispatch
 * --------------------------------------------------------------------- */
static const clap_plugin_audio_ports_t s_audio_ports;  /* forward decl */

static const void *plugin_get_extension(const clap_plugin_t *p, const char *id)
{
    (void)p;
    if (strcmp(id, CLAP_EXT_PARAMS)      == 0) return &s_params;
    if (strcmp(id, CLAP_EXT_STATE)       == 0) return &s_state;
    if (strcmp(id, CLAP_EXT_GUI)         == 0) return &s_gui;
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    return NULL;
}

static void plugin_on_main_thread(const clap_plugin_t *p) { (void)p; }

/* -----------------------------------------------------------------------
 * Audio ports  -  stereo in / stereo out
 *
 * We declare stereo so hosts route both L and R channels to us.  The
 * process() function averages the two input channels into a single
 * mono buffer before NR (handles two SDR receivers on L/R), then fans
 * the processed result back out to both output channels.
 *
 * A host feeding a true mono source will simply supply channel_count=1
 * and the process() path handles that gracefully.
 * --------------------------------------------------------------------- */
static uint32_t audio_ports_count(const clap_plugin_t *p, bool is_input)
{
    (void)p; (void)is_input; return 1;
}

static bool audio_ports_get(const clap_plugin_t *p, uint32_t idx,
                             bool is_input, clap_audio_port_info_t *info)
{
    (void)p;
    if (idx != 0) return false;
    memset(info, 0, sizeof(*info));
    info->id            = 0;
    info->channel_count = 2;
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    snprintf(info->name, sizeof(info->name), is_input ? "Stereo In" : "Stereo Out");
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get   = audio_ports_get,
};

/* -----------------------------------------------------------------------
 * Factory
 * --------------------------------------------------------------------- */
static const clap_plugin_descriptor_t s_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = PLUGIN_ID,
    .name         = PLUGIN_NAME,
    .vendor       = PLUGIN_VENDOR,
    .url          = PLUGIN_URL,
    .manual_url   = "",
    .support_url  = "",
    .version      = PLUGIN_VERSION,
    .description  = "NR1/NR2/NR3/NR4 noise reduction for ham radio",
    .features     = (const char *[]){ CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, NULL },
};

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t *f) { (void)f; return 1; }

static const clap_plugin_descriptor_t *factory_get_plugin_descriptor(
    const clap_plugin_factory_t *f, uint32_t idx)
{
    (void)f;
    return (idx == 0) ? &s_desc : NULL;
}

static const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *f,
                                                    const clap_host_t *host,
                                                    const char *plugin_id)
{
    (void)f;
    if (strcmp(plugin_id, PLUGIN_ID) != 0) return NULL;

    clap_nr_t *self = (clap_nr_t *)calloc(1, sizeof(clap_nr_t));
    if (!self) return NULL;

    clap_plugin_t *pl = &self->plugin;
    pl->desc              = &s_desc;
    pl->plugin_data       = self;
    pl->init              = plugin_init;
    pl->destroy           = plugin_destroy;
    pl->activate          = plugin_activate;
    pl->deactivate        = plugin_deactivate;
    pl->start_processing  = plugin_start_processing;
    pl->stop_processing   = plugin_stop_processing;
    pl->reset             = plugin_reset;
    pl->process           = plugin_process;
    pl->get_extension     = plugin_get_extension;
    pl->on_main_thread    = plugin_on_main_thread;

    self->host              = host;
    self->nr_mode           = 0;
    self->nr4_reduction     = 10.0f;
    self->anr_taps          = 64;
    self->anr_delay         = 16;
    self->anr_gain          = 0.0001;
    self->anr_leakage       = 0.1;
    self->emnr_gain_method  = 2;   /* MM-LSA */
    self->emnr_npe_method   = 0;   /* OSMS   */
    self->emnr_ae_run       = 1;

    return pl;
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count      = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin         = factory_create_plugin,
};

/* -----------------------------------------------------------------------
 * Entry point  (the symbol the host looks up: clap_entry)
 * --------------------------------------------------------------------- */
static bool entry_init(const char *path)
{
    /* Pre-load companion DLLs from the plugin's own directory so that
     * the delay-loaded imports resolve correctly regardless of whatever
     * DLL search path the host has configured.  This must happen before
     * any NR algorithm code runs (which uses fftw / rnnoise / specbleach). */
    if (path && *path) {
        char dir[MAX_PATH];
        strncpy(dir, path, MAX_PATH - 1);
        dir[MAX_PATH - 1] = '\0';
        /* Strip filename, keep trailing backslash */
        char *sep = strrchr(dir, '\\');
        if (!sep) sep = strrchr(dir, '/');
        if (sep) {
            *(sep + 1) = '\0';
            static const char *deps[] = {
                /* libfftw3f-3 must be loaded before specbleach.dll because
                 * specbleach imports it as a direct dependency. */
                "libfftw3-3.dll", "libfftw3f-3.dll", "rnnoise.dll", "specbleach.dll", NULL
            };
            char dll_path[MAX_PATH];
            for (int i = 0; deps[i]; ++i) {
                _snprintf_s(dll_path, MAX_PATH, _TRUNCATE, "%s%s", dir, deps[i]);
                LoadLibraryA(dll_path);
            }
        }
    }
    return true;
}
static void  entry_deinit(void) {}
static const void *entry_get_factory(const char *factory_id)
{
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) return &s_factory;
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
