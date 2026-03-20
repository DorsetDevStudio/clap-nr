/*
 * gui_win32.c  -  Win32 GUI implementation for clap-nr
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
 * Clean grid layout:
 *   - Sliders (trackbars) for all ANR range parameters
 *   - Edit boxes for precise numeric entry
 *   - Fixed-size window (500x400); no dynamic resize to avoid layout drift
 */

#include "gui.h"

#ifndef _WIN32
/* Stubs for non-Windows builds */
clap_nr_gui_t *gui_create(void *p, gui_param_cb_t cb) { (void)p;(void)cb; return NULL; }
void           gui_destroy(clap_nr_gui_t *g) { (void)g; }
bool           gui_set_parent(clap_nr_gui_t *g, const clap_window_t *w) { (void)g;(void)w; return false; }
void           gui_get_size(clap_nr_gui_t *g, uint32_t *w, uint32_t *h) { (void)g; *w=500; *h=400; }
bool           gui_show(clap_nr_gui_t *g) { (void)g; return false; }
bool           gui_hide(clap_nr_gui_t *g) { (void)g; return false; }
bool           gui_resize(clap_nr_gui_t *g, uint32_t w, uint32_t h) { (void)g;(void)w;(void)h; return false; }
void           gui_set_param(clap_nr_gui_t *g, clap_id id, double v) { (void)g;(void)id;(void)v; }
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward-declared param indices matching clap_nr.c */
enum {
    _GUI_PARAM_NR_MODE          = 0,
    _GUI_PARAM_NR4_REDUCTION    = 1,
    _GUI_PARAM_ANR_TAPS         = 2,
    _GUI_PARAM_ANR_DELAY        = 3,
    _GUI_PARAM_ANR_GAIN         = 4,
    _GUI_PARAM_ANR_LEAKAGE      = 5,
    _GUI_PARAM_EMNR_GAIN_METHOD = 6,
    _GUI_PARAM_EMNR_NPE_METHOD  = 7,
    _GUI_PARAM_EMNR_AE_RUN      = 8,
};

/* -----------------------------------------------------------------------
 * Window dimensions (fixed layout - no dynamic resize)
 * --------------------------------------------------------------------- */
#define GUI_W   500
#define GUI_H   400

/* -----------------------------------------------------------------------
 * Layout grid constants (all values in client-area pixels)
 * --------------------------------------------------------------------- */
#define M           12    /* outer horizontal margin */
#define GRP_W       (GUI_W - 2*M)  /* group box width = 476 */

/* NR mode strip */
#define MODE_Y      10
#define MODE2_Y     34
#define SEP_Y       56

/* NR1 group box */
#define GRP1_Y      62
#define GRP1_H      152

/* NR2 group box (8px gap after NR1) */
#define GRP2_Y      (GRP1_Y + GRP1_H + 8)
#define GRP2_H      118

/* Shared slider-row column geometry (absolute x coords) */
#define ROW_LBL_X   (M + 8)                              /* 20  */
#define ROW_LBL_W   82
#define ROW_SLD_X   (ROW_LBL_X + ROW_LBL_W + 4)         /* 106 */
#define ROW_VAL_W   84
#define ROW_SLD_W   (GUI_W - ROW_SLD_X - 4 - ROW_VAL_W - M - 8)  /* 282 */
#define ROW_VAL_X   (ROW_SLD_X + ROW_SLD_W + 4)         /* 392 */
#define ROW_H       30    /* row pitch */
#define SLD_H       26    /* trackbar height */
#define EDT_H       22    /* edit box height */
#define LBL_H       20    /* label height */

/* NR2 combo layout */
#define NR2_LBL_W   100
#define NR2_CMB_X   (ROW_LBL_X + NR2_LBL_W + 4)  /* 124 */
#define NR2_CMB_W   190

/* Hint strip below groups */
#define HINT_Y      (GRP2_Y + GRP2_H + 10)

/* -----------------------------------------------------------------------
 * Control IDs
 * --------------------------------------------------------------------- */
#define IDC_RADIO_OFF        101
#define IDC_RADIO_NR1        102
#define IDC_RADIO_NR2        103
#define IDC_RADIO_NR3        104
#define IDC_RADIO_NR4        105

#define IDC_GRP_NR1          110
#define IDC_SLD_TAPS         111
#define IDC_SLD_DELAY        112
#define IDC_SLD_GAIN         113
#define IDC_SLD_LEAK         114
#define IDC_EDIT_TAPS        115
#define IDC_EDIT_DELAY       116
#define IDC_EDIT_GAIN        117
#define IDC_EDIT_LEAK        118

#define IDC_GRP_NR2          120
#define IDC_COMBO_GMETHOD    121
#define IDC_COMBO_NPE        122
#define IDC_CHECK_AE         123

#define WC_CLAPNR           "ClapNrGui_v2"

/* -----------------------------------------------------------------------
 * Slider <-> parameter value converters
 *   Gain    : slider 1..10000  => value = pos * 1e-6   (1e-6 to 0.01)
 *   Leakage : slider 0..1000   => value = pos / 1000.0 (0.0  to 1.0)
 * --------------------------------------------------------------------- */
static int    gain_to_slider(double g) { int p=(int)(g*1e6+.5); return p<1?1:(p>10000?10000:p); }
static double slider_to_gain(int p)    { return p * 1e-6; }
static int    leak_to_slider(double v) { int p=(int)(v*1000.+.5); return p<0?0:(p>1000?1000:p); }
static double slider_to_leak(int p)    { return p / 1000.0; }

/* -----------------------------------------------------------------------
 * GUI struct
 * --------------------------------------------------------------------- */
struct clap_nr_gui_s {
    HWND  hwnd;
    void *plugin;
    gui_param_cb_t on_param_change;
    bool  embedded;
    bool  updating;

    /* NR mode radio buttons */
    HWND radio_off, radio_nr1, radio_nr2, radio_nr3, radio_nr4;

    /* NR1 (ANR) — label + trackbar + edit per parameter */
    HWND grp_nr1;
    HWND lbl_taps,   sld_taps,   edit_taps;
    HWND lbl_delay,  sld_delay,  edit_delay;
    HWND lbl_gain,   sld_gain,   edit_gain;
    HWND lbl_leak,   sld_leak,   edit_leak;

    /* NR2 (EMNR) */
    HWND grp_nr2;
    HWND lbl_gmethod, combo_gmethod;
    HWND lbl_npe,     combo_npe;
    HWND check_ae;

    /* Cached values */
    int    nr_mode;
    int    anr_taps;
    int    anr_delay;
    double anr_gain;
    double anr_leakage;
    int    emnr_gain_method;
    int    emnr_npe_method;
    int    emnr_ae_run;
};

/* -----------------------------------------------------------------------
 * Widget factory helpers
 * --------------------------------------------------------------------- */
static HWND mk_static(HWND p, const char *t, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "STATIC", t, WS_CHILD|WS_VISIBLE|SS_LEFT,
                           x, y, w, h, p, NULL, NULL, NULL);
}
static HWND mk_separator(HWND p, int x, int y, int w)
{
    return CreateWindowExA(0, "STATIC", "", WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                           x, y, w, 2, p, NULL, NULL, NULL);
}
static HWND mk_radio(HWND p, const char *t, int id, int x, int y, int w, int h, bool first)
{
    DWORD s = WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|(first?WS_GROUP:0);
    return CreateWindowExA(0, "BUTTON", t, s, x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_groupbox(HWND p, const char *t, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "BUTTON", t, WS_CHILD|WS_VISIBLE|BS_GROUPBOX|WS_GROUP,
                           x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_trackbar(HWND p, int id, int x, int y, int w, int h, int lo, int hi, int val)
{
    HWND tb = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                              WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
                              x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
    SendMessageA(tb, TBM_SETRANGEMIN, FALSE, lo);
    SendMessageA(tb, TBM_SETRANGEMAX, FALSE, hi);
    SendMessageA(tb, TBM_SETPOS,      TRUE,  val);
    return tb;
}
static HWND mk_edit(HWND p, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                           WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                           x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_combo(HWND p, int id, int x, int y, int w)
{
    return CreateWindowExA(0, "COMBOBOX", "",
                           WS_CHILD|WS_VISIBLE|WS_VSCROLL|CBS_DROPDOWNLIST,
                           x, y, w, 120, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_check(HWND p, const char *t, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "BUTTON", t, WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
                           x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}

/* -----------------------------------------------------------------------
 * Enable/disable interactive controls for the active NR mode
 * --------------------------------------------------------------------- */
static void update_group_state(clap_nr_gui_t *g)
{
    BOOL nr1 = (g->nr_mode == 1);
    EnableWindow(g->sld_taps,   nr1); EnableWindow(g->edit_taps,  nr1);
    EnableWindow(g->sld_delay,  nr1); EnableWindow(g->edit_delay, nr1);
    EnableWindow(g->sld_gain,   nr1); EnableWindow(g->edit_gain,  nr1);
    EnableWindow(g->sld_leak,   nr1); EnableWindow(g->edit_leak,  nr1);

    BOOL nr2 = (g->nr_mode == 2);
    EnableWindow(g->combo_gmethod, nr2);
    EnableWindow(g->combo_npe,     nr2);
    EnableWindow(g->check_ae,      nr2);
}

/* -----------------------------------------------------------------------
 * Sync slider + edit displays from cached values (no callback)
 * --------------------------------------------------------------------- */
static void sync_nr1_edits(clap_nr_gui_t *g)
{
    char buf[64];
    if (g->sld_taps)  SendMessageA(g->sld_taps,  TBM_SETPOS, TRUE, g->anr_taps);
    if (g->sld_delay) SendMessageA(g->sld_delay, TBM_SETPOS, TRUE, g->anr_delay);
    if (g->sld_gain)  SendMessageA(g->sld_gain,  TBM_SETPOS, TRUE, gain_to_slider(g->anr_gain));
    if (g->sld_leak)  SendMessageA(g->sld_leak,  TBM_SETPOS, TRUE, leak_to_slider(g->anr_leakage));
    snprintf(buf,sizeof(buf),"%d",    g->anr_taps);    SetWindowTextA(g->edit_taps,  buf);
    snprintf(buf,sizeof(buf),"%d",    g->anr_delay);   SetWindowTextA(g->edit_delay, buf);
    snprintf(buf,sizeof(buf),"%.6f",  g->anr_gain);    SetWindowTextA(g->edit_gain,  buf);
    snprintf(buf,sizeof(buf),"%.6f",  g->anr_leakage); SetWindowTextA(g->edit_leak,  buf);
}
static void sync_nr2_combos(clap_nr_gui_t *g)
{
    SendMessageA(g->combo_gmethod, CB_SETCURSEL, (WPARAM)g->emnr_gain_method, 0);
    SendMessageA(g->combo_npe,     CB_SETCURSEL, (WPARAM)g->emnr_npe_method,  0);
    SendMessageA(g->check_ae, BM_SETCHECK,
                 g->emnr_ae_run ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* -----------------------------------------------------------------------
 * Edit-box commit (on EN_KILLFOCUS) — also syncs the matching slider
 * --------------------------------------------------------------------- */
static void on_edit_commit(clap_nr_gui_t *g, HWND edit_hwnd)
{
    char buf[64] = {0};
    GetWindowTextA(edit_hwnd, buf, sizeof(buf));

    if (edit_hwnd == g->edit_taps) {
        int v = atoi(buf);
        if (v >= 16 && v <= 2048 && v != g->anr_taps) {
            g->anr_taps = v;
            SendMessageA(g->sld_taps, TBM_SETPOS, TRUE, v);
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_TAPS, (double)v);
        }
    } else if (edit_hwnd == g->edit_delay) {
        int v = atoi(buf);
        if (v >= 1 && v <= 512 && v != g->anr_delay) {
            g->anr_delay = v;
            SendMessageA(g->sld_delay, TBM_SETPOS, TRUE, v);
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_DELAY, (double)v);
        }
    } else if (edit_hwnd == g->edit_gain) {
        double v = atof(buf);
        if (v >= 1e-6 && v <= 0.01 && v != g->anr_gain) {
            g->anr_gain = v;
            SendMessageA(g->sld_gain, TBM_SETPOS, TRUE, gain_to_slider(v));
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_GAIN, v);
        }
    } else if (edit_hwnd == g->edit_leak) {
        double v = atof(buf);
        if (v >= 0.0 && v <= 1.0 && v != g->anr_leakage) {
            g->anr_leakage = v;
            SendMessageA(g->sld_leak, TBM_SETPOS, TRUE, leak_to_slider(v));
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_LEAKAGE, v);
        }
    }
    sync_nr1_edits(g); /* reformat to canonical form */
}

/* -----------------------------------------------------------------------
 * Window procedure
 * --------------------------------------------------------------------- */
static LRESULT CALLBACK gui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    clap_nr_gui_t *g = (clap_nr_gui_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_HSCROLL: {
        /* Trackbar thumb moved */
        if (!g || g->updating) break;
        HWND sld = (HWND)lp;
        int pos = (int)SendMessageA(sld, TBM_GETPOS, 0, 0);
        char buf[64];
        g->updating = true;
        if (sld == g->sld_taps && pos != g->anr_taps) {
            g->anr_taps = pos;
            snprintf(buf, sizeof(buf), "%d", pos);
            SetWindowTextA(g->edit_taps, buf);
            g->updating = false;
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_TAPS, (double)pos);
        } else if (sld == g->sld_delay && pos != g->anr_delay) {
            g->anr_delay = pos;
            snprintf(buf, sizeof(buf), "%d", pos);
            SetWindowTextA(g->edit_delay, buf);
            g->updating = false;
            g->on_param_change(g->plugin, _GUI_PARAM_ANR_DELAY, (double)pos);
        } else if (sld == g->sld_gain) {
            double v = slider_to_gain(pos);
            if (v != g->anr_gain) {
                g->anr_gain = v;
                snprintf(buf, sizeof(buf), "%.6f", v);
                SetWindowTextA(g->edit_gain, buf);
                g->updating = false;
                g->on_param_change(g->plugin, _GUI_PARAM_ANR_GAIN, v);
            }
        } else if (sld == g->sld_leak) {
            double v = slider_to_leak(pos);
            if (v != g->anr_leakage) {
                g->anr_leakage = v;
                snprintf(buf, sizeof(buf), "%.4f", v);
                SetWindowTextA(g->edit_leak, buf);
                g->updating = false;
                g->on_param_change(g->plugin, _GUI_PARAM_ANR_LEAKAGE, v);
            }
        } else {
            g->updating = false;
        }
        return 0;
    }

    case WM_COMMAND:
        if (!g || g->updating) break;
        {
            int ctrl_id = LOWORD(wp);
            int notif   = HIWORD(wp);

            if (ctrl_id >= IDC_RADIO_OFF && ctrl_id <= IDC_RADIO_NR4 && notif == BN_CLICKED) {
                int new_mode = ctrl_id - IDC_RADIO_OFF;
                if (new_mode != g->nr_mode) {
                    g->nr_mode = new_mode;
                    g->on_param_change(g->plugin, _GUI_PARAM_NR_MODE, (double)new_mode);
                    update_group_state(g);
                }
                return 0;
            }
            if (notif == EN_KILLFOCUS && g->nr_mode == 1) {
                on_edit_commit(g, (HWND)lp);
                return 0;
            }
            if (ctrl_id == IDC_COMBO_GMETHOD && notif == CBN_SELCHANGE) {
                int v = (int)SendMessageA(g->combo_gmethod, CB_GETCURSEL, 0, 0);
                if (v >= 0 && v != g->emnr_gain_method) {
                    g->emnr_gain_method = v;
                    g->on_param_change(g->plugin, _GUI_PARAM_EMNR_GAIN_METHOD, (double)v);
                }
                return 0;
            }
            if (ctrl_id == IDC_COMBO_NPE && notif == CBN_SELCHANGE) {
                int v = (int)SendMessageA(g->combo_npe, CB_GETCURSEL, 0, 0);
                if (v >= 0 && v != g->emnr_npe_method) {
                    g->emnr_npe_method = v;
                    g->on_param_change(g->plugin, _GUI_PARAM_EMNR_NPE_METHOD, (double)v);
                }
                return 0;
            }
            if (ctrl_id == IDC_CHECK_AE && notif == BN_CLICKED) {
                int v = (SendMessageA(g->check_ae, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                if (v != g->emnr_ae_run) {
                    g->emnr_ae_run = v;
                    g->on_param_change(g->plugin, _GUI_PARAM_EMNR_AE_RUN, (double)v);
                }
                return 0;
            }
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = GUI_W;
        mmi->ptMinTrackSize.y = GUI_H;
        return 0;
    }

    case WM_DESTROY:
        if (g) g->hwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* -----------------------------------------------------------------------
 * Register window class (idempotent)
 * --------------------------------------------------------------------- */
static void register_class(void)
{
    WNDCLASSEXA wc;
    if (GetClassInfoExA(GetModuleHandleA(NULL), WC_CLAPNR, &wc)) return;

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = gui_wndproc;
    wc.cbWndExtra    = sizeof(void *);
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WC_CLAPNR;
    RegisterClassExA(&wc);
}

/* -----------------------------------------------------------------------
 * Build all child controls with a clean grid layout
 * --------------------------------------------------------------------- */
static void create_controls(clap_nr_gui_t *g, HWND hwnd)
{
    /* --- NR Mode radio strip --- */
    mk_static(hwnd, "NR Mode:", M, MODE_Y + 3, 78, LBL_H);
    int rx = M + 82;
    g->radio_off = mk_radio(hwnd, "Off",         IDC_RADIO_OFF, rx,      MODE_Y,  44, 22, true);
    g->radio_nr1 = mk_radio(hwnd, "NR1 (ANR)",   IDC_RADIO_NR1, rx+46,  MODE_Y,  88, 22, false);
    g->radio_nr2 = mk_radio(hwnd, "NR2 (EMNR)",  IDC_RADIO_NR2, rx+136, MODE_Y,  96, 22, false);
    g->radio_nr3 = mk_radio(hwnd, "NR3 (RNNR)",  IDC_RADIO_NR3, rx+234, MODE_Y,  96, 22, false);
    g->radio_nr4 = mk_radio(hwnd, "NR4 (SBNR)",  IDC_RADIO_NR4, rx,     MODE2_Y, 96, 22, false);
    mk_static(hwnd, "(NR3 and NR4: coming soon)", rx+100, MODE2_Y+3, 240, LBL_H);
    mk_separator(hwnd, M, SEP_Y, GRP_W);
    EnableWindow(g->radio_nr3, FALSE);
    EnableWindow(g->radio_nr4, FALSE);

    /* --- NR1 group: Adaptive LMS (ANR) --- */
    g->grp_nr1 = mk_groupbox(hwnd, "NR1 - Adaptive LMS (ANR) Parameters",
                              IDC_GRP_NR1, M, GRP1_Y, GRP_W, GRP1_H);
    int ry = GRP1_Y + 22;

    /* Each row: label | trackbar | value-edit */
#define NR1_ROW(lh,sh,eh, txt, sid,eid, lo,hi, sval) \
    lh = mk_static  (hwnd, txt,  ROW_LBL_X, ry+4,   ROW_LBL_W, LBL_H);    \
    sh = mk_trackbar(hwnd, sid,  ROW_SLD_X, ry,     ROW_SLD_W, SLD_H, lo, hi, sval); \
    eh = mk_edit    (hwnd, eid,  ROW_VAL_X, ry+2,   ROW_VAL_W, EDT_H);    \
    ry += ROW_H;

    NR1_ROW(g->lbl_taps,  g->sld_taps,  g->edit_taps,
            "Taps:",    IDC_SLD_TAPS,  IDC_EDIT_TAPS,   16,  2048, g->anr_taps)
    NR1_ROW(g->lbl_delay, g->sld_delay, g->edit_delay,
            "Delay:",   IDC_SLD_DELAY, IDC_EDIT_DELAY,   1,   512, g->anr_delay)
    NR1_ROW(g->lbl_gain,  g->sld_gain,  g->edit_gain,
            "Gain:",    IDC_SLD_GAIN,  IDC_EDIT_GAIN,    1, 10000, gain_to_slider(g->anr_gain))
    NR1_ROW(g->lbl_leak,  g->sld_leak,  g->edit_leak,
            "Leakage:", IDC_SLD_LEAK,  IDC_EDIT_LEAK,    0,  1000, leak_to_slider(g->anr_leakage))
#undef NR1_ROW

    /* --- NR2 group: Spectral MMSE (EMNR) --- */
    g->grp_nr2 = mk_groupbox(hwnd, "NR2 - Spectral MMSE (EMNR) Parameters",
                              IDC_GRP_NR2, M, GRP2_Y, GRP_W, GRP2_H);
    ry = GRP2_Y + 22;

    g->lbl_gmethod   = mk_static(hwnd, "Gain Method:", ROW_LBL_X, ry+3, NR2_LBL_W, LBL_H);
    g->combo_gmethod = mk_combo (hwnd, IDC_COMBO_GMETHOD, NR2_CMB_X, ry, NR2_CMB_W);
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"RROE");
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"MEPSE");
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"MM-LSA");
    ry += ROW_H;

    g->lbl_npe   = mk_static(hwnd, "NPE Method:", ROW_LBL_X, ry+3, NR2_LBL_W, LBL_H);
    g->combo_npe = mk_combo (hwnd, IDC_COMBO_NPE, NR2_CMB_X, ry, NR2_CMB_W);
    SendMessageA(g->combo_npe, CB_ADDSTRING, 0, (LPARAM)"OSMS");
    SendMessageA(g->combo_npe, CB_ADDSTRING, 0, (LPARAM)"MMSE");
    ry += ROW_H;

    g->check_ae = mk_check(hwnd, "Audio Enhance (AE)", IDC_CHECK_AE,
                           ROW_LBL_X, ry, 200, 22);

    /* --- Bottom hint strip --- */
    mk_static(hwnd,
              "Gain: 1e-6 to 0.01   |   Leakage: 0.0 to 1.0   "
              "|   Taps: 16 to 2048   |   Delay: 1 to 512",
              M, HINT_Y, GRP_W, LBL_H);
    mk_static(hwnd, "Values can also be typed directly into the text boxes.",
              M, HINT_Y + 18, GRP_W, LBL_H);

    /* --- Set initial values --- */
    sync_nr1_edits(g);
    sync_nr2_combos(g);
    update_group_state(g);
    CheckRadioButton(hwnd, IDC_RADIO_OFF, IDC_RADIO_NR4,
                     IDC_RADIO_OFF + g->nr_mode);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */
clap_nr_gui_t *gui_create(void *plugin, gui_param_cb_t on_param_change)
{
    clap_nr_gui_t *g = (clap_nr_gui_t *)calloc(1, sizeof(clap_nr_gui_t));
    if (!g) return NULL;
    g->plugin           = plugin;
    g->on_param_change  = on_param_change;
    g->nr_mode          = 0;
    g->anr_taps         = 64;
    g->anr_delay        = 16;
    g->anr_gain         = 0.0001;
    g->anr_leakage      = 0.1;
    g->emnr_gain_method = 2;  /* MM-LSA */
    g->emnr_npe_method  = 0;  /* OSMS   */
    g->emnr_ae_run      = 1;
    return g;
}

void gui_destroy(clap_nr_gui_t *gui)
{
    if (!gui) return;
    if (gui->hwnd) { DestroyWindow(gui->hwnd); gui->hwnd = NULL; }
    free(gui);
}

bool gui_set_parent(clap_nr_gui_t *gui, const clap_window_t *window)
{
    if (!gui || !window) return false;
    register_class();
    gui->embedded = true;
    gui->hwnd = CreateWindowExA(
        0, WC_CLAPNR, "CLAP NR",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, GUI_W, GUI_H,
        (HWND)window->win32, NULL, GetModuleHandleA(NULL), gui);
    if (!gui->hwnd) return false;
    create_controls(gui, gui->hwnd);
    return true;
}

void gui_get_size(clap_nr_gui_t *gui, uint32_t *out_w, uint32_t *out_h)
{
    (void)gui;
    *out_w = GUI_W;
    *out_h = GUI_H;
}
bool gui_show(clap_nr_gui_t *gui)
{
    if (!gui) return false;

    if (!gui->hwnd) {
        /* No parent was set — create a floating popup.
         * Position it just below the current mouse cursor, clamped to the
         * monitor’s working area so it always appears fully on-screen. */
        register_class();
        gui->embedded = false;

        POINT pt = { 200, 200 };
        GetCursorPos(&pt);
        int wx = pt.x - GUI_W / 4;
        int wy = pt.y + 16;

        HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(hmon, &mi)) {
            if (wx + GUI_W  > mi.rcWork.right)  wx = mi.rcWork.right  - GUI_W;
            if (wy + GUI_H  > mi.rcWork.bottom) wy = mi.rcWork.bottom - GUI_H;
            if (wx < mi.rcWork.left)             wx = mi.rcWork.left;
            if (wy < mi.rcWork.top)              wy = mi.rcWork.top;
        }

        gui->hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW, WC_CLAPNR, "CLAP NR",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            wx, wy, GUI_W, GUI_H,
            NULL, NULL, GetModuleHandleA(NULL), gui);
        if (!gui->hwnd) return false;
        create_controls(gui, gui->hwnd);
    }

    /* Restore if minimised, then surface it */
    if (IsIconic(gui->hwnd))
        ShowWindow(gui->hwnd, SW_RESTORE);
    else
        ShowWindow(gui->hwnd, SW_SHOW);

    if (gui->embedded) {
        /* Child window: bring to top within its parent and claim keyboard focus */
        BringWindowToTop(gui->hwnd);
        SetFocus(gui->hwnd);
    } else {
        /* Floating top-level: force to the foreground regardless of focus rules */
        SetWindowPos(gui->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(gui->hwnd);
    }
    return true;
}

bool gui_hide(clap_nr_gui_t *gui)
{
    if (!gui || !gui->hwnd) return false;
    ShowWindow(gui->hwnd, SW_HIDE);
    return true;
}

bool gui_resize(clap_nr_gui_t *gui, uint32_t w, uint32_t h)
{
    /* Fixed-size layout — ignore host resize requests */
    (void)gui; (void)w; (void)h;
    return false;
}

void gui_set_param(clap_nr_gui_t *gui, clap_id param_id, double value)
{
    if (!gui) return;
    gui->updating = true;

    switch (param_id) {
    case _GUI_PARAM_NR_MODE:
        gui->nr_mode = (int)value;
        if (gui->hwnd)
            CheckRadioButton(gui->hwnd, IDC_RADIO_OFF, IDC_RADIO_NR4,
                             IDC_RADIO_OFF + gui->nr_mode);
        update_group_state(gui);
        break;
    case _GUI_PARAM_ANR_TAPS:
        gui->anr_taps = (int)value;
        if (gui->hwnd) sync_nr1_edits(gui);
        break;
    case _GUI_PARAM_ANR_DELAY:
        gui->anr_delay = (int)value;
        if (gui->hwnd) sync_nr1_edits(gui);
        break;
    case _GUI_PARAM_ANR_GAIN:
        gui->anr_gain = value;
        if (gui->hwnd) sync_nr1_edits(gui);
        break;
    case _GUI_PARAM_ANR_LEAKAGE:
        gui->anr_leakage = value;
        if (gui->hwnd) sync_nr1_edits(gui);
        break;
    case _GUI_PARAM_EMNR_GAIN_METHOD:
        gui->emnr_gain_method = (int)value;
        if (gui->hwnd) sync_nr2_combos(gui);
        break;
    case _GUI_PARAM_EMNR_NPE_METHOD:
        gui->emnr_npe_method = (int)value;
        if (gui->hwnd) sync_nr2_combos(gui);
        break;
    case _GUI_PARAM_EMNR_AE_RUN:
        gui->emnr_ae_run = (int)value;
        if (gui->hwnd) sync_nr2_combos(gui);
        break;
    default: break;
    }

    gui->updating = false;
}

#endif /* _WIN32 */
