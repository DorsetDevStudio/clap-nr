/*
 * gui.h  –  Platform-agnostic GUI interface for clap-nr
 *
 * Platform implementation files:
 *   Windows  : gui_win32.c
 *   Linux    : gui_gtk.c    (future)
 *   macOS    : gui_cocoa.m  (future)
 */

#ifndef CLAP_NR_GUI_H
#define CLAP_NR_GUI_H

#include <stdint.h>
#include <stdbool.h>
#include "clap/clap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque GUI handle */
typedef struct clap_nr_gui_s clap_nr_gui_t;

/*
 * Callback invoked on the main thread when the user changes a parameter
 * via the GUI.  The plugin should update its internal state and notify
 * the host (request_flush / push PARAM_VALUE event).
 */
typedef void (*gui_param_cb_t)(void *plugin, clap_id param_id, double value);

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/* Allocate GUI resources.  The window may not be visible yet.
 * title is used as the floating window caption (e.g. "Host  |  Plugin"). */
clap_nr_gui_t *gui_create(void *plugin, gui_param_cb_t on_param_change,
                           const char *title);

/* Free all GUI resources.  Called after gui_hide(). */
void           gui_destroy(clap_nr_gui_t *gui);

/* -----------------------------------------------------------------------
 * CLAP host integration
 * --------------------------------------------------------------------- */

/* Embed the plugin window as a child of the given host window.
 * window->api == CLAP_WINDOW_API_WIN32 on Windows. */
bool     gui_set_parent(clap_nr_gui_t *gui, const clap_window_t *window);

/* Return the preferred (fixed) size in pixels. */
void     gui_get_size(clap_nr_gui_t *gui, uint32_t *out_w, uint32_t *out_h);

/* Show / hide the window. */
bool     gui_show(clap_nr_gui_t *gui);
bool     gui_hide(clap_nr_gui_t *gui);

/* Resize the window (host-driven; minimum size is enforced internally). */
bool     gui_resize(clap_nr_gui_t *gui, uint32_t w, uint32_t h);

/* -----------------------------------------------------------------------
 * Param sync  (plugin → GUI)
 * Called from the main thread to push a new value into the GUI controls
 * without triggering on_param_change (avoids feedback loops).
 * --------------------------------------------------------------------- */
void     gui_set_param(clap_nr_gui_t *gui, clap_id param_id, double value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CLAP_NR_GUI_H */
