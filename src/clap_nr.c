/*
 * clap_nr.c  -  CLAP plugin incorporating DSP noise-reduction algorithms
 *
 * Copyright (C) 2026 - Stuart E. Green (G5STU)
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
 *   3 = NR3  (RNNR - RNNoise neural net)
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
#ifdef _WIN32
#  include <Windows.h>
#else
#  include <stdatomic.h>
#  include <dlfcn.h>
#  include <unistd.h>
#  include <limits.h>
#  ifndef PATH_MAX
#    define PATH_MAX 4096
#  endif
#endif
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

/* EMNR requires bsize >= incr = fsize/ovrlp = 4096/4 = 1024.
 * The host may deliver fewer than 1024 samples per block, so we
 * accumulate into a fixed 1024-sample working buffer and drain into
 * a matching output ring.  xemnr is called exactly once per 1024
 * input samples, exactly as Thetis uses dsp_size=1024. */
#define EMNR_BSIZE  1024

/* -----------------------------------------------------------------------
 * Plugin identity
 * --------------------------------------------------------------------- */
#define PLUGIN_ID      "com.dorsetdevstufio.clap-nr"
#define PLUGIN_NAME    "CLAP NR"
#define PLUGIN_VENDOR  "Stuart E. Green"
#define PLUGIN_VERSION CLAP_NR_VERSION_STR
#define PLUGIN_URL     ""

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

    /* NR3 - RNNoise neural net (RNNR) */
    PARAM_NR3_MODEL         = 9,  /* 0=Standard, 1=Small, 2=Large (stepped)   */
    PARAM_NR3_STRENGTH      = 10, /* 0.0=bypass, 1.0=full denoising           */

    PARAM_COUNT
};

/* -----------------------------------------------------------------------
 * State save/load format  (bump version when layout changes)
 * --------------------------------------------------------------------- */
#define STATE_VERSION  4U

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
    int32_t  nr3_model;       /* new in v2: 0=Standard, 1=Small, 2=Large */
    float    nr3_strength;    /* new in v3: 0.0=bypass, 1.0=full         */
    uint32_t tooltips_on;     /* new in v4: 1=tips enabled, 0=disabled   */
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

    /* EMNR uses a fixed 1024-sample bsize.  These rings decouple the
     * host block size from xemnr's required stride. */
    double  emnr_inbuf[2][2 * EMNR_BSIZE];   /* interleaved complex input for xemnr  */
    double  emnr_outbuf[2][2 * EMNR_BSIZE];  /* interleaved complex output from xemnr */
    double  emnr_outq[2][EMNR_BSIZE];        /* flat real output queue (pop from here) */
    int     emnr_in_count[2];                 /* samples fed into emnr_inbuf so far     */
    int     emnr_out_head[2];                 /* next unread index in emnr_outq          */
    int     emnr_out_avail[2];                /* samples ready to read in emnr_outq      */

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
    int    nr3_model;    /* 0=Standard (built-in), 1=Small, 2=Large */
    float  nr3_strength; /* 0.0=bypass, 1.0=full denoising           */

    /* ---- RNNR direct-call staging (bypasses xrnnr's internal ring buffer
     * so that any host block size works correctly, not just chunks of
     * exactly 480 samples).  Styled identically to the EMNR ring above. */
    float  rnnr_inbuf[2][480];  /* accumulate 480 input samples per frame  */
    float  rnnr_outbuf[2][480]; /* processed output, drained sample by sample */
    int    rnnr_in_count[2];    /* samples currently staged in rnnr_inbuf    */
    int    rnnr_out_head[2];    /* next unread index in rnnr_outbuf           */
    int    rnnr_out_avail[2];   /* samples ready to drain in rnnr_outbuf     */

    /* ---- GUI ---- */
    clap_nr_gui_t *gui;
    bool           gui_floating;
    bool           tooltips_on;   /* persisted UI preference */

    /* Pending param->host notification (set by GUI, cleared by flush) */
    bool param_dirty[PARAM_COUNT];

    /* Set by handle_param_event (any thread); cleared at the start of
     * process() after applying changes to the live NR instances.
     * Keeping all apply_*() calls inside process() means NR algorithm
     * state is only ever modified from the audio thread, eliminating
     * the race between the GUI/main thread and the audio thread. */
    bool params_dirty;
    bool nr3_model_dirty;  /* set when nr3_model changes; triggers RNNRloadModel */

    /* True between a successful activate() and the subsequent deactivate().
     * process() checks this flag and does passthrough if false, guarding
     * against hosts that call process() outside the active_state window. */
    volatile bool active;

    /* Incremented by process() on entry, decremented on exit.  deactivate()
     * spins on this reaching zero before returning, guaranteeing that
     * plugin_destroy() will never run concurrently with process().  This is
     * the only safe way to allow destroy_emnr() (which calls
     * fftw_destroy_plan()) to run without racing the audio thread. */
#ifdef _WIN32
    volatile LONG       process_depth;
#else
    _Atomic int         process_depth;
#endif
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
    /* Detect transition INTO NR3 before updating run flags */
    bool entering_nr3 = (new_mode == 3) &&
                        (self->rnnr[0] ? (self->rnnr[0]->run == 0) : false);
    self->nr_mode = new_mode;
    for (int ch = 0; ch < 2; ++ch) {
        if (self->anr[ch])  self->anr[ch]->run  = (new_mode == 1) ? 1 : 0;
        if (self->emnr[ch]) self->emnr[ch]->run = (new_mode == 2) ? 1 : 0;
        if (self->rnnr[ch]) self->rnnr[ch]->run = (new_mode == 3) ? 1 : 0;
        if (self->sbnr[ch]) self->sbnr[ch]->run = (new_mode == 4) ? 1 : 0;
        if (entering_nr2 && self->emnr[ch]) {
            reset_emnr_for_nr2_entry(self->emnr[ch]);
            /* Also reset the wrapper ring state so the EMNR ring buffer
             * in plugin_process starts clean when switching back to NR2. */
            self->emnr_in_count[ch]  = 0;
            self->emnr_out_head[ch]  = 0;
            self->emnr_out_avail[ch] = 0;
        }
        /* For NR3 entry, clear the staging buffers so the first output frame
         * isn't contaminated by stale samples from a previous NR3 session. */
        if (entering_nr3) {
            self->rnnr_in_count[ch]  = 0;
            self->rnnr_out_head[ch]  = 0;
            self->rnnr_out_avail[ch] = 0;
            memset(self->rnnr_inbuf[ch],  0, sizeof(self->rnnr_inbuf[ch]));
            memset(self->rnnr_outbuf[ch], 0, sizeof(self->rnnr_outbuf[ch]));
        }
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
/* -----------------------------------------------------------------------
 * Helper: load the NR3 (RNNR) model file matching self->nr3_model.
 * 0 = built-in  (NULL model)
 * 1 = rnnoise_weights_small.bin  (beside the .clap)
 * 2 = rnnoise_weights_large.bin  (beside the .clap)
 * If the file cannot be found, falls back to the built-in model silently.
 * --------------------------------------------------------------------- */
static void apply_nr3_model(clap_nr_t *self)
{
    if (self->nr3_model == 0) {
        RNNRloadModel(NULL);
        return;
    }
#ifdef _WIN32
    char path[MAX_PATH];
    HMODULE hmod = GetModuleHandleA("clap-nr.clap");
    if (!hmod || !GetModuleFileNameA(hmod, path, MAX_PATH)) {
        RNNRloadModel(NULL);
        return;
    }
    char *slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
#else
    char path[PATH_MAX];
    Dl_info di;
    if (!dladdr((void *)apply_nr3_model, &di) || !di.dli_fname) {
        RNNRloadModel(NULL);
        return;
    }
    strncpy(path, di.dli_fname, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    char *slash = strrchr(path, '/');
#endif
    if (!slash) { RNNRloadModel(NULL); return; }
    *(slash + 1) = '\0';
    const char *fname = (self->nr3_model == 1)
                        ? "rnnoise_weights_small.bin"
                        : "rnnoise_weights_large.bin";
    size_t dlen = strlen(path);
    size_t flen = strlen(fname);
#ifdef _WIN32
    if (dlen + flen < MAX_PATH)
#else
    if (dlen + flen < PATH_MAX)
#endif
        memcpy(path + dlen, fname, flen + 1);
    else {
        RNNRloadModel(NULL);
        return;
    }
    RNNRloadModel(path);
}

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
    case PARAM_NR3_MODEL:
        self->nr3_model  = (int)pv->value;
        self->nr3_model_dirty = true;
        break;
    case PARAM_NR3_STRENGTH: self->nr3_strength = (float)pv->value; break;
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

    /* NR3 model loading reads a file (rnnoise_model_from_filename).  Do it
     * here on the main thread so the audio thread is never blocked by I/O.
     * handle_param_event already set nr3_model and nr3_model_dirty; clear
     * the dirty flag so the audio-thread path (params_dirty block) won't
     * also try to call request_callback for a no-op reload. */
    if (param_id == PARAM_NR3_MODEL) {
        apply_nr3_model(self);
        self->nr3_model_dirty = false;
    }

    /* Ask the host to call params_flush so it records the new value */
    const clap_host_params_t *hp =
        (const clap_host_params_t *)self->host->get_extension(self->host, CLAP_EXT_PARAMS);
    if (hp) hp->request_flush(self->host);
}

static void on_gui_tooltips_change(void *plugin_ptr, bool tooltips_on)
{
    clap_nr_t *self   = (clap_nr_t *)plugin_ptr;
    self->tooltips_on = tooltips_on;

    /* Notify the host that our state has changed so it will re-save it. */
    const clap_host_state_t *hs =
        (const clap_host_state_t *)self->host->get_extension(self->host, CLAP_EXT_STATE);
    if (hs) hs->mark_dirty(self->host);
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

    /* Drain with timeout.  If the audio thread is stuck (e.g. an exception
     * escaped process() and was swallowed by the host callback frame without
     * decrementing process_depth), spinning forever would block the host's
     * shutdown watchdog.  After 2 s we give up and leak the DSP objects rather
     * than calling fftw_destroy_plan() concurrently with fftw_execute(). */
#ifdef _WIN32
    MemoryBarrier();
    {
        int budget = 2000;
        while (InterlockedCompareExchange(&self->process_depth, 0, 0) != 0) {
            Sleep(1);
            if (--budget <= 0) {
                nr_log("plugin_destroy: timed out waiting for audio thread -- "
                       "leaking DSP objects to avoid FFTW abort()");
                if (self->gui) { gui_destroy(self->gui); self->gui = NULL; }
                free(self);
                return;
            }
        }
    }
#else
    atomic_thread_fence(memory_order_seq_cst);
    {
        int budget = 2000;
        while (atomic_load(&self->process_depth) != 0) {
            struct timespec ts = { 0, 1000000L }; /* 1 ms */
            nanosleep(&ts, NULL);
            if (--budget <= 0) {
                nr_log("plugin_destroy: timed out waiting for audio thread -- "
                       "leaking DSP objects to avoid crash");
                if (self->gui) { gui_destroy(self->gui); self->gui = NULL; }
                free(self);
                return;
            }
        }
    }
#endif

    if (self->gui) { gui_destroy(self->gui); self->gui = NULL; }

    for (int ch = 0; ch < 2; ++ch) {
        if (self->anr[ch])  { destroy_anr (self->anr[ch]);  self->anr[ch]  = NULL; }
        if (self->emnr[ch]) { destroy_emnr(self->emnr[ch]); self->emnr[ch] = NULL; }
        if (self->rnnr[ch]) { destroy_rnnr(self->rnnr[ch]); self->rnnr[ch] = NULL; }
        if (self->sbnr[ch]) { destroy_sbnr(self->sbnr[ch]); self->sbnr[ch] = NULL; }
        free(self->buf[ch]); self->buf[ch] = NULL;
    }

    free(self);
}

/* -----------------------------------------------------------------------
 * Diagnostic log helper  -  appends one line to %TEMP%\clap-nr.log
 * --------------------------------------------------------------------- */
static void nr_log(const char *fmt, ...)
{
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return;
    strncat_s(path, MAX_PATH, "clap-nr.log", _TRUNCATE);
    FILE *f = NULL;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !tmp[0]) tmp = "/tmp";
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/clap-nr.log", tmp);
    FILE *f = fopen(path, "a");
    if (!f) return;
#endif
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
                self->buf[ch] = (double *)calloc(2 * max_frames, sizeof(double));
                if (!self->buf[ch]) { nr_log("activate: calloc failed ch=%d", ch); __leave; }

                nr_log("activate: creating ANR ch=%d sr=%.0f frames=%u", ch, sr, max_frames);
                self->anr[ch]  = create_anr(0, 0, (int)max_frames,
                                            self->buf[ch], self->buf[ch],
                                            2048,
                                            self->anr_taps, self->anr_delay,
                                            self->anr_gain, self->anr_leakage,
                                            120.0, 120.0, 200.0, 0.001, 6.25e-10, 1.0, 3.0);

                nr_log("activate: creating EMNR ch=%d", ch);
                /* Create EMNR with the actual host block size, exactly as
                 * Thetis does.  xemnr() is a stateful streaming processor
                 * with internal STFT ring buffers; it must be called once per
                 * block with the full buffer.  Calling it in small fixed-size
                 * chunks while shifting the in/out pointer each call causes
                 * the output accumulator to be read from the wrong offset,
                 * producing severe digital distortion on all signals. */
                self->emnr[ch] = create_emnr(0, 0, EMNR_BSIZE,
                                             self->emnr_inbuf[ch],
                                             self->emnr_outbuf[ch],
                                             4096, 4, (int)sr, 0, 1.0,
                                             self->emnr_gain_method,
                                             self->emnr_npe_method,
                                             self->emnr_ae_run);
                self->emnr_in_count[ch]  = 0;
                self->emnr_out_head[ch]  = 0;
                self->emnr_out_avail[ch] = 0;
                memset(self->emnr_inbuf[ch],  0, sizeof(self->emnr_inbuf[ch]));
                memset(self->emnr_outbuf[ch], 0, sizeof(self->emnr_outbuf[ch]));
                memset(self->emnr_outq[ch],   0, sizeof(self->emnr_outq[ch]));

                nr_log("activate: creating RNNR ch=%d", ch);
                self->rnnr[ch] = create_rnnr(0, 0, (int)max_frames,
                                             self->buf[ch], self->buf[ch], (int)sr);
                self->rnnr_in_count[ch]  = 0;
                self->rnnr_out_head[ch]  = 0;
                self->rnnr_out_avail[ch] = 0;
                memset(self->rnnr_inbuf[ch],  0, sizeof(self->rnnr_inbuf[ch]));
                memset(self->rnnr_outbuf[ch], 0, sizeof(self->rnnr_outbuf[ch]));

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
                    self->buf[ch] = (double *)calloc(2 * max_frames, sizeof(double));
                    if (!self->buf[ch]) { nr_log("reactivate: calloc failed ch=%d", ch); __leave; }

                    if (self->anr[ch]) {
                        setSize_anr   (self->anr[ch],  (int)max_frames);
                        setBuffers_anr(self->anr[ch],  self->buf[ch], self->buf[ch]);
                    }
                    if (self->emnr[ch]) {
                        /* EMNR bsize is always EMNR_BSIZE=1024 regardless of
                         * host block size.  Its in/out pointers are the
                         * dedicated emnr_inbuf/emnr_outbuf, not buf[ch]. */
                        self->emnr_in_count[ch]  = 0;
                        self->emnr_out_head[ch]  = 0;
                        self->emnr_out_avail[ch] = 0;
                        memset(self->emnr_inbuf[ch],  0, sizeof(self->emnr_inbuf[ch]));
                        memset(self->emnr_outbuf[ch], 0, sizeof(self->emnr_outbuf[ch]));
                        memset(self->emnr_outq[ch],   0, sizeof(self->emnr_outq[ch]));
                    }
                    if (self->rnnr[ch]) {
                        setSize_rnnr   (self->rnnr[ch], (int)max_frames);
                        setBuffers_rnnr(self->rnnr[ch], self->buf[ch], self->buf[ch]);
                    }
                    self->rnnr_in_count[ch]  = 0;
                    self->rnnr_out_head[ch]  = 0;
                    self->rnnr_out_avail[ch] = 0;
                    memset(self->rnnr_inbuf[ch],  0, sizeof(self->rnnr_inbuf[ch]));
                    memset(self->rnnr_outbuf[ch], 0, sizeof(self->rnnr_outbuf[ch]));
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
                if (self->emnr[ch]) {
                    flush_emnr(self->emnr[ch]);
                    self->emnr_in_count[ch]  = 0;
                    self->emnr_out_head[ch]  = 0;
                    self->emnr_out_avail[ch] = 0;
                    memset(self->emnr_inbuf[ch],  0, sizeof(self->emnr_inbuf[ch]));
                    memset(self->emnr_outbuf[ch], 0, sizeof(self->emnr_outbuf[ch]));
                    memset(self->emnr_outq[ch],   0, sizeof(self->emnr_outq[ch]));
                }
                /* RNNR / SBNR have no flush function; zeroing the shared buffer
                 * drains their ring buffers on the next process() call. */
                if (self->buf[ch])
                    memset(self->buf[ch], 0, 2 * self->block_size * sizeof(double));
                /* Reset RNNR plugin-level staging so the next process() call
                 * starts with empty accumulator and no stale output queued. */
                self->rnnr_in_count[ch]  = 0;
                self->rnnr_out_head[ch]  = 0;
                self->rnnr_out_avail[ch] = 0;
                memset(self->rnnr_inbuf[ch],  0, sizeof(self->rnnr_inbuf[ch]));
                memset(self->rnnr_outbuf[ch], 0, sizeof(self->rnnr_outbuf[ch]));
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
     * not block the UI perceptibly.
     *
     * Sleep(1) is used rather than Sleep(0): Sleep(0) only yields to threads
     * of equal or higher priority.  Because audio threads typically run at
     * THREAD_PRIORITY_TIME_CRITICAL (above the main thread), Sleep(0) from
     * the main thread will not yield to the audio thread, turning the spin
     * into a busy-wait that delays the audio thread completing its buffer
     * and decrementing process_depth.  Sleep(1) explicitly yields for one
     * scheduler timeslice, allowing the audio thread to run. */
#ifdef _WIN32
    MemoryBarrier();
    while (InterlockedCompareExchange(&self->process_depth, 0, 0) != 0)
        Sleep(1);
#else
    atomic_thread_fence(memory_order_seq_cst);
    while (atomic_load(&self->process_depth) != 0)
        sched_yield();
#endif
}

static bool plugin_start_processing(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    /* Re-enable DSP after a stop/start cycle.  Activate() set this to true
     * initially; stop_processing() cleared it; we restore it here so that
     * process() does real work again rather than passing audio through. */
    self->active = true;
    return true;
}

static void plugin_stop_processing(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    /* Called on the audio thread after the last process() call in this
     * processing session.  Clearing active means any process() call that
     * a non-compliant host fires between here and deactivate() will
     * passthrough without touching FFTW, RNNoise, or specbleach.  The
     * deactivate() drain-spin will then exit immediately (depth is already
     * 0 because this is called after the last process() returned). */
    self->active = false;
}

static void plugin_reset(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    /* Flush algorithm internal state so a transport jump or plugin
     * re-insertion starts clean without residual filter history. */
    {
        for (int ch = 0; ch < 2; ++ch) {
            if (self->anr[ch])  flush_anr(self->anr[ch]);
            if (self->emnr[ch]) {
                flush_emnr(self->emnr[ch]);
                self->emnr_in_count[ch]  = 0;
                self->emnr_out_head[ch]  = 0;
                self->emnr_out_avail[ch] = 0;
                memset(self->emnr_inbuf[ch],  0, sizeof(self->emnr_inbuf[ch]));
                memset(self->emnr_outbuf[ch], 0, sizeof(self->emnr_outbuf[ch]));
                memset(self->emnr_outq[ch],   0, sizeof(self->emnr_outq[ch]));
            }
            /* RNNR and SBNR have no flush function; zero the shared I/O
             * buffer to drain their ring buffers on the next process() call. */
            if (self->buf[ch] && self->block_size > 0)
                memset(self->buf[ch], 0, 2 * self->block_size * sizeof(double));
        }
        /* Reset RNNR staging buffers (NR already zeroed buf[ch]) */
        for (int ch = 0; ch < 2; ++ch) {
            self->rnnr_in_count[ch]  = 0;
            self->rnnr_out_head[ch]  = 0;
            self->rnnr_out_avail[ch] = 0;
            memset(self->rnnr_inbuf[ch],  0, sizeof(self->rnnr_inbuf[ch]));
            memset(self->rnnr_outbuf[ch], 0, sizeof(self->rnnr_outbuf[ch]));
        }
    }
}

static clap_process_status plugin_process(const clap_plugin_t *p, const clap_process_t *proc)
{
    clap_nr_t *self = (clap_nr_t *)p;

    /* Register this call so deactivate() can wait for us to finish. */
#ifdef _WIN32
    InterlockedIncrement(&self->process_depth);
#else
    atomic_fetch_add(&self->process_depth, 1);
#endif

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
#ifdef _WIN32
        InterlockedDecrement(&self->process_depth);
#else
        atomic_fetch_sub(&self->process_depth, 1);
#endif
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
        if (self->nr3_model_dirty) {
            /* Model loading is file I/O -- must not happen on the audio thread.
             * Request a main-thread callback; plugin_on_main_thread will call
             * apply_nr3_model() there.  Keep nr3_model_dirty set so the callback
             * handler knows work is pending.
             * Guard with active: if deactivate/stop_processing already cleared
             * the flag, calling back into the host during teardown can crash. */
            if (self->active)
                self->host->request_callback(self->host);
        }
        self->params_dirty = false;
    }

    if (n == 0 || proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
#ifdef _WIN32
        InterlockedDecrement(&self->process_depth);
#else
        atomic_fetch_sub(&self->process_depth, 1);
#endif
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
#ifdef _WIN32
        InterlockedDecrement(&self->process_depth);
#else
        atomic_fetch_sub(&self->process_depth, 1);
#endif
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t proc_ch = (in_ch < out_ch) ? in_ch : out_ch;
    if (proc_ch > 2) proc_ch = 2;

    /* Guard the entire per-channel DSP block with SEH (Windows only).
     * If any NR algorithm (fftw_execute, rnnoise_process_frame, xsbnr, ...)
     * throws a structured exception:
     *   - The __except handler disables NR and passthroughs the audio so the
     *     host stream is not silenced.
     *   - Execution falls through to the InterlockedDecrement BELOW the block,
     *     which is what was missing when the original SEH was removed during
     *     the cross-platform port.  Without this, any exception escaping
     *     process() leaves process_depth permanently stuck at 1, causing
     *     deactivate()/destroy() to spin forever until the host watchdog kills
     *     the process. */
#ifdef _WIN32
    __try {
#endif
    for (uint32_t ch = 0; ch < proc_ch; ++ch) {
        float *src = in_buf->data32[ch];
        float *dst = out_buf->data32[ch];

        /* Passthrough if audio pointers or NR buffer are missing */
        if (!src || !dst || !self->buf[ch]) {
            if (src && dst && src != dst) memcpy(dst, src, n * sizeof(float));
            continue;
        }

        {
            /* Load real samples into even slots.  Zero imaginary (odd) slots
             * explicitly on every call — previous NR output may have written
             * non-zero values there, which would corrupt the next frame. */
            for (uint32_t i = 0; i < n; ++i) {
                self->buf[ch][2 * i]     = (double)src[i];
                self->buf[ch][2 * i + 1] = 0.0;
            }

            /* Zero any tail samples between n and block_size so stale data
             * from a previous block doesn't bleed into the current one. */
            uint32_t blk = self->block_size;
            if (n < blk) {
                for (uint32_t i = n; i < blk; ++i) {
                    self->buf[ch][2 * i]     = 0.0;
                    self->buf[ch][2 * i + 1] = 0.0;
                }
            }

            /* ANR is a sample-by-sample LMS filter: updating buff_size to
             * the actual frame count is safe and avoids looping over zeros. */
            if (self->anr[ch]) {
                self->anr[ch]->buff_size = (int)n;
                xanr(self->anr[ch], 0);
            }
            if (self->emnr[ch] && self->emnr[ch]->run) {
                /* Push n real samples into the EMNR input ring, call xemnr
                 * in EMNR_BSIZE=1024 strides, collect output into a flat
                 * real output queue, then pop n samples back into buf[ch].
                 *
                 * EMNR has STFT latency (~3072 samples at fsize=4096/ovrlp=4
                 * = 64 ms at 48 kHz).  While the output queue has not yet
                 * filled we output silence, which is correct latency behaviour.
                 *
                 * The run guard above is CRITICAL: without it this block reads
                 * from buf[ch] into inbuf and writes zeros back to buf[ch]
                 * (out_avail stays 0 until 1024 samples are staged), silencing
                 * the buffer before any other NR mode gets to process it. */
                EMNR em = self->emnr[ch];
                double *inbuf  = self->emnr_inbuf[ch];
                double *outbuf = self->emnr_outbuf[ch];
                double *outq   = self->emnr_outq[ch];
                int *in_count  = &self->emnr_in_count[ch];
                int *out_head  = &self->emnr_out_head[ch];
                int *out_avail = &self->emnr_out_avail[ch];

                for (uint32_t s = 0; s < n; ++s) {
                    /* Feed one real sample (imaginary = 0) */
                    inbuf[2 * (*in_count) + 0] = self->buf[ch][2 * s];
                    inbuf[2 * (*in_count) + 1] = 0.0;
                    (*in_count)++;

                    if (*in_count == EMNR_BSIZE) {
                        /* Full 1024-sample block: run EMNR, extract real output */
                        xemnr(em, 0);
                        for (int k = 0; k < EMNR_BSIZE; ++k)
                            outq[k] = outbuf[2 * k];  /* real part only */
                        *in_count  = 0;
                        *out_head  = 0;
                        *out_avail = EMNR_BSIZE;
                    }

                    /* Pop one output sample back into the processing buffer */
                    if (*out_avail > 0) {
                        self->buf[ch][2 * s] = outq[(*out_head)++];
                        (*out_avail)--;
                    } else {
                        self->buf[ch][2 * s] = 0.0;  /* STFT latency fill */
                    }
                }
            }
            if (self->rnnr[ch] && self->rnnr[ch]->run) {
                /* RNNR: accumulate 480-sample frames and call rnnoise_process_frame
                 * directly, bypassing xrnnr's internal ring buffer.  xrnnr assumes
                 * buffer_size == 480 (one RNNoise frame), so when the host delivers
                 * any other block size (typically a power of two -- 256, 512, 1024)
                 * the output ring drains faster than it fills, causing periodic
                 * passthrough glitches every ~150-200 ms.  This staging loop works
                 * correctly for ANY host block size, mirroring what EMNR does.
                 *
                 * Scaling: rnnoise_process_frame expects float values in the 16-bit
                 * PCM range (~+-32 768).  Multiply by 32768 before, divide after. */
                float *inbuf   = self->rnnr_inbuf[ch];
                float *outbuf  = self->rnnr_outbuf[ch];
                int *in_count  = &self->rnnr_in_count[ch];
                int *out_head  = &self->rnnr_out_head[ch];
                int *out_avail = &self->rnnr_out_avail[ch];

                for (uint32_t s = 0; s < n; ++s) {
                    /* Stage one input sample at PCM scale */
                    inbuf[(*in_count)++] = (float)(self->buf[ch][2 * s] * 32768.0);

                    if (*in_count == 480) {
                        /* Full 480-sample frame: call RNNoise directly.
                         * Lock cs so RNNRloadModel can safely swap st. */
                        NR_MUTEX_LOCK(self->rnnr[ch]->cs);
                        rnnoise_process_frame(self->rnnr[ch]->st, outbuf, inbuf);
                        NR_MUTEX_UNLOCK(self->rnnr[ch]->cs);
                        if (self->nr3_strength < 1.0f) {
                            float s = self->nr3_strength;
                            float t = 1.0f - s;
                            for (int j = 0; j < 480; ++j)
                                outbuf[j] = s * outbuf[j] + t * inbuf[j];
                        }
                        *in_count  = 0;
                        *out_head  = 0;
                        *out_avail = 480;
                    }

                    /* Drain one sample back (latency = one 480-sample frame
                     * = 10 ms at 48 kHz until first output is ready) */
                    if (*out_avail > 0) {
                        self->buf[ch][2 * s] =
                            (double)outbuf[(*out_head)++] * (1.0 / 32768.0);
                        (*out_avail)--;
                    }
                    /* else: input passthrough for this sample while pipeline fills */
                }
            }
            if (self->sbnr[ch]) {
                /* Update buffer_size to the actual host frame count every call,
                 * just like ANR does with buff_size.  Without this, xsbnr loops
                 * over all max_frames (e.g. 8192) instead of just n (e.g. 480),
                 * and the real processed audio lands at indices >> n in buf[ch]
                 * which are never read back -- producing silence. */
                self->sbnr[ch]->buffer_size = (int)n;
                xsbnr(self->sbnr[ch], 0);
            }

            /* Read back the real (even) output slots only */
            for (uint32_t i = 0; i < n; ++i)
                dst[i] = (float)self->buf[ch][2 * i];
        }
    }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        nr_log("process: SEH 0x%08lX escaped DSP -- disabling NR, passthroughing",
               (unsigned long)GetExceptionCode());
        self->nr_mode    = 0;
        self->params_dirty = true;
        /* Overwrite any partial DSP output with clean passthrough audio. */
        if (proc->audio_inputs_count > 0 && proc->audio_outputs_count > 0) {
            const clap_audio_buffer_t *ib2 = &proc->audio_inputs[0];
            const clap_audio_buffer_t *ob2 = &proc->audio_outputs[0];
            uint32_t cc = (ib2->channel_count < ob2->channel_count)
                         ? ib2->channel_count : ob2->channel_count;
            for (uint32_t c = 0; c < cc; ++c)
                if (ib2->data32[c] && ob2->data32[c])
                    memcpy(ob2->data32[c], ib2->data32[c], n * sizeof(float));
        }
    }
#endif

#ifdef _WIN32
    InterlockedDecrement(&self->process_depth);
#else
    atomic_fetch_sub(&self->process_depth, 1);
#endif
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
    case PARAM_NR3_MODEL:
        info->id = PARAM_NR3_MODEL;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR3 Model");
        snprintf(info->module, sizeof(info->module), "NR3");
        info->min_value = 0; info->max_value = 2; info->default_value = 0;
        return true;
    case PARAM_NR3_STRENGTH:
        info->id = PARAM_NR3_STRENGTH;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name,   sizeof(info->name),   "NR3 Suppression");
        snprintf(info->module, sizeof(info->module), "NR3");
        info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.5;
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
    case PARAM_NR3_MODEL:        *out = (double)self->nr3_model;         return true;
    case PARAM_NR3_STRENGTH:     *out = (double)self->nr3_strength;      return true;
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
    case PARAM_NR3_MODEL: {
        static const char *names[] = { "Standard", "Small", "Large" };
        int i = (int)val;
        snprintf(buf, size, "%s", (i >= 0 && i <= 2) ? names[i] : "?");
        return true; }
    case PARAM_NR3_STRENGTH:
        snprintf(buf, size, "%.0f%%", val * 100.0); return true;
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
    s.nr3_model         = self->nr3_model;
    s.nr3_strength      = self->nr3_strength;
    s.tooltips_on       = self->tooltips_on ? 1u : 0u;
    return stream->write(stream, &s, sizeof(s)) == (int64_t)sizeof(s);
}

static bool state_load(const clap_plugin_t *p, const clap_istream_t *stream)
{
    clap_nr_t *self = (clap_nr_t *)p;
    clap_nr_state_t s;
    memset(&s, 0, sizeof(s));
    int64_t got = stream->read(stream, &s, sizeof(s));
    /* Minimum bytes for each version:
     *   v1 = 52 bytes  (no nr3_model)
     *   v2 = 56 bytes  (adds nr3_model)
     *   v3 = 60 bytes  (adds nr3_strength)
     *   v4 = 64 bytes  (adds tooltips_on) */
    if (got < (int64_t)sizeof(uint32_t)) return false;
    uint32_t min_bytes;
    switch (s.version) {
    case 1U: min_bytes = 52; break;
    case 2U: min_bytes = 56; break;
    case 3U: min_bytes = 60; break;
    case 4U: min_bytes = 64; break;
    default: return false;
    }
    if (got < (int64_t)min_bytes) return false;

    self->nr4_reduction     = s.nr4_reduction;
    self->anr_taps          = (s.anr_taps  >= 16  && s.anr_taps  <= 2048) ? s.anr_taps  : 64;
    self->anr_delay         = (s.anr_delay >= 1   && s.anr_delay <= 512)  ? s.anr_delay : 16;
    self->anr_gain          = (s.anr_gain  >= 1e-6 && s.anr_gain <= 0.01) ? s.anr_gain  : 0.0001;
    self->anr_leakage       = (s.anr_leakage >= 0.0 && s.anr_leakage <= 1.0) ? s.anr_leakage : 0.1;
    self->emnr_gain_method  = (s.emnr_gain_method >= 0 && s.emnr_gain_method <= 2) ? s.emnr_gain_method : 2;
    self->emnr_npe_method   = (s.emnr_npe_method  >= 0 && s.emnr_npe_method  <= 1) ? s.emnr_npe_method  : 0;
    self->emnr_ae_run       = (s.emnr_ae_run == 0 || s.emnr_ae_run == 1) ? s.emnr_ae_run : 1;

    /* nr3_model: only trust the field from v2 onwards; v1 trailing bytes are garbage */
    if (s.version >= 2U)
        self->nr3_model = (s.nr3_model >= 0 && s.nr3_model <= 2) ? s.nr3_model : 2;
    else
        self->nr3_model = 2;  /* default Large for upgraded v1 states */

    /* nr3_strength: from v3 onwards; default full suppression for older states */
    self->nr3_strength = (s.version >= 3U)
        ? ((s.nr3_strength >= 0.0f && s.nr3_strength <= 1.0f) ? s.nr3_strength : 1.0f)
        : 1.0f;

    /* tooltips_on: from v4 onwards; default on for older states */
    self->tooltips_on = (s.version >= 4U) ? (s.tooltips_on != 0u) : true;

    apply_nr_mode(self, (s.nr_mode >= 0 && s.nr_mode <= 4) ? s.nr_mode : 0);
    apply_anr_params(self);
    apply_emnr_params(self);
    apply_nr3_model(self);

    if (self->gui) {
        gui_set_param(self->gui, PARAM_NR_MODE,          (double)self->nr_mode);
        gui_set_param(self->gui, PARAM_ANR_TAPS,         (double)self->anr_taps);
        gui_set_param(self->gui, PARAM_ANR_DELAY,        (double)self->anr_delay);
        gui_set_param(self->gui, PARAM_ANR_GAIN,         self->anr_gain);
        gui_set_param(self->gui, PARAM_ANR_LEAKAGE,      self->anr_leakage);
        gui_set_param(self->gui, PARAM_EMNR_GAIN_METHOD, (double)self->emnr_gain_method);
        gui_set_param(self->gui, PARAM_EMNR_NPE_METHOD,  (double)self->emnr_npe_method);
        gui_set_param(self->gui, PARAM_EMNR_AE_RUN,      (double)self->emnr_ae_run);
        gui_set_param(self->gui, PARAM_NR3_MODEL,        (double)self->nr3_model);
        gui_set_param(self->gui, PARAM_NR3_STRENGTH,     (double)self->nr3_strength);
        gui_set_tooltips(self->gui, self->tooltips_on);
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
    (void)p;
#ifdef _WIN32
    /* Embedded (WS_CHILD) only.  Returning false for floating causes hosts
     * that probe floating first to fall back to embedded mode, where they
     * create a native parent window (e.g. a QDialog with transientParent
     * set to the host's main window), embed our WS_CHILD window into it,
     * and manage the title and window-manager relationship themselves.  That
     * parent relationship is what makes our window minimise with the host
     * and inherit the host's title prefix (e.g. "Station Master Pro | ").
     * A plugin-owned floating window has no owner HWND, so Windows never
     * minimises it in response to the host minimising. */
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
#else
    return false;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t *p, const char **api, bool *is_floating)
{
    (void)p;
#ifdef _WIN32
    *api = CLAP_WINDOW_API_WIN32;
    /* Prefer embedded.  The host creates a native window (e.g. QDialog)
     * with its main window as the OS owner/parent, embeds our WS_CHILD
     * control into it, and sets the title itself.  That gives us
     * minimize-with-host and the host's own title prefix for free.
     * is_api_supported returns false for floating so hosts that probe
     * floating first skip it and land here. */
    *is_floating = false;
    return true;
#elif defined(__linux__)
    *api = CLAP_WINDOW_API_X11;
    *is_floating = true;   /* GLFW floating window, no parent embedding */
    return true;
#elif defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
    *is_floating = true;
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
    char gui_title[256];
    const char *host_name = (self->host && self->host->name && self->host->name[0])
                            ? self->host->name : NULL;
    if (host_name)
        snprintf(gui_title, sizeof(gui_title), "%s  |  " PLUGIN_NAME, host_name);
    else
        snprintf(gui_title, sizeof(gui_title), "%s", PLUGIN_NAME);
    self->gui = gui_create(self, on_gui_param_change, on_gui_tooltips_change, gui_title);
    if (!self->gui) return false;

    /* Sync the plugin's current parameter state to the freshly-created GUI.
     * This is necessary because state_load skips gui_set_param when the GUI
     * does not yet exist (it is created later by the host). */
    gui_set_param(self->gui, PARAM_NR_MODE,          (double)self->nr_mode);
    gui_set_param(self->gui, PARAM_NR4_REDUCTION,    (double)self->nr4_reduction);
    gui_set_param(self->gui, PARAM_ANR_TAPS,         (double)self->anr_taps);
    gui_set_param(self->gui, PARAM_ANR_DELAY,        (double)self->anr_delay);
    gui_set_param(self->gui, PARAM_ANR_GAIN,         self->anr_gain);
    gui_set_param(self->gui, PARAM_ANR_LEAKAGE,      self->anr_leakage);
    gui_set_param(self->gui, PARAM_EMNR_GAIN_METHOD, (double)self->emnr_gain_method);
    gui_set_param(self->gui, PARAM_EMNR_NPE_METHOD,  (double)self->emnr_npe_method);
    gui_set_param(self->gui, PARAM_EMNR_AE_RUN,      (double)self->emnr_ae_run);
    gui_set_param(self->gui, PARAM_NR3_MODEL,        (double)self->nr3_model);
    gui_set_param(self->gui, PARAM_NR3_STRENGTH,     (double)self->nr3_strength);
    gui_set_tooltips(self->gui, self->tooltips_on);
    return true;
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

static bool gui_plugin_set_transient(const clap_plugin_t *p, const clap_window_t *w)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return false;
    return gui_set_transient(self->gui, w);
}

static void gui_plugin_suggest_title(const clap_plugin_t *p, const char *title)
{
    clap_nr_t *self = (clap_nr_t *)p;
    if (!self->gui) return;
    gui_suggest_title(self->gui, title);
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
    .set_transient     = gui_plugin_set_transient,
    .suggest_title     = gui_plugin_suggest_title,
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

static void plugin_on_main_thread(const clap_plugin_t *p)
{
    clap_nr_t *self = (clap_nr_t *)p;
    /* Apply any NR3 model change that was deferred from the audio thread
     * (host automation of PARAM_NR3_MODEL).  File I/O is safe here. */
    if (self->nr3_model_dirty) {
        apply_nr3_model(self);
        self->nr3_model_dirty = false;
    }
}

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
    self->nr3_strength      = 0.5f;
    self->tooltips_on       = true;

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
#ifdef _WIN32
    /* Pre-load companion DLLs from the plugin's own directory so that
     * the delay-loaded imports resolve correctly regardless of whatever
     * DLL search path the host has configured. */
    if (path && *path) {
        char dir[MAX_PATH];
        strncpy(dir, path, MAX_PATH - 1);
        dir[MAX_PATH - 1] = '\0';
        char *sep = strrchr(dir, '\\');
        if (!sep) sep = strrchr(dir, '/');
        if (sep) {
            *(sep + 1) = '\0';
            static const char *deps[] = {
                "libfftw3-3.dll", "libfftw3f-3.dll", "rnnoise.dll", "specbleach.dll", NULL
            };
            char dll_path[MAX_PATH];
            for (int i = 0; deps[i]; ++i) {
                _snprintf_s(dll_path, MAX_PATH, _TRUNCATE, "%s%s", dir, deps[i]);
                LoadLibraryA(dll_path);
            }
        }
    }
#else
    (void)path;  /* Unix: shared-library dependencies resolved by the dynamic linker */
#endif
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
