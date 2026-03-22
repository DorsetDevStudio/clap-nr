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
 * Rebuilt layout (2026):
 *   - Only the section for the active NR mode is shown; others are hidden.
 *   - Floating window height adapts automatically when the mode changes.
 *   - Window is resizable (WS_THICKFRAME) and always-on-top (WS_EX_TOPMOST).
 *   - Per-mode minimum size is enforced via WM_GETMINMAXINFO.
 *   - gui_resize() is implemented for embedded (host-driven) use.
 */

#include "gui.h"
#include "version.h"

#ifndef _WIN32
/* ---- Stubs for non-Windows builds ------------------------------------ */
clap_nr_gui_t *gui_create(void *p, gui_param_cb_t cb) { (void)p;(void)cb; return NULL; }
void           gui_destroy(clap_nr_gui_t *g)          { (void)g; }
bool gui_set_parent(clap_nr_gui_t *g, const clap_window_t *w) { (void)g;(void)w; return false; }
void gui_get_size(clap_nr_gui_t *g, uint32_t *w, uint32_t *h) { (void)g; *w=484; *h=240; }
bool gui_show(clap_nr_gui_t *g)   { (void)g; return false; }
bool gui_hide(clap_nr_gui_t *g)   { (void)g; return false; }
bool gui_resize(clap_nr_gui_t *g, uint32_t w, uint32_t h) { (void)g;(void)w;(void)h; return false; }
void gui_set_param(clap_nr_gui_t *g, clap_id id, double v) { (void)g;(void)id;(void)v; }
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Param indices matching clap_nr.c -------------------------------- */
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
    _GUI_PARAM_NR3_MODEL        = 9,
};

/* ---- Layout constants (all client-area pixels) ----------------------- */
#define GUI_W       514          /* client width                           */
#define M           12           /* outer horizontal margin                */
#define GRP_W       (GUI_W - 2*M) /* group-box width = 460                 */

/* Mode-strip (always visible) */
#define ROW1_Y      8            /* single radio row */
#define SEP_Y       34           /* separator line (ROW1_Y + 22 + 4)      */
#define CONT_Y      42           /* first content pixel below mode strip   */

/* Slider-row column geometry */
#define ROW_LBL_X   (M + 8)       /* 20  */
#define ROW_LBL_W   72
#define ROW_SLD_X   (ROW_LBL_X + ROW_LBL_W + 4)  /* 96  */
#define ROW_SLD_W   (GUI_W - ROW_SLD_X - M - 4)   /* 372 */
#define ROW_H       28           /* row pitch          */
#define SLD_H       24           /* trackbar height    */
#define LBL_H       18           /* static label height*/

/* NR2 combo layout */
#define NR2_LBL_W   90
#define NR2_CMB_X   (ROW_LBL_X + NR2_LBL_W + 4)  /* 114 */
#define NR2_CMB_W   190

/* NR1 group box */
#define NR1_GRP_Y   CONT_Y
#define NR1_GRP_H   140          /* 22 header + 4xROW_H(28) + 6 pad       */
#define NR1_HINT_Y  (NR1_GRP_Y + NR1_GRP_H + 4)   /* 206 */
#define H_NR1       (NR1_HINT_Y + LBL_H + 8)       /* 232 */

/* NR2 group box */
#define NR2_GRP_Y   CONT_Y
#define NR2_GRP_H   110          /* 22 header + 3xROW_H(28) + 4 pad       */
#define H_NR2       (NR2_GRP_Y + NR2_GRP_H + 8)    /* 180 */

/* NR4 group box */
#define NR4_GRP_Y   CONT_Y
#define NR4_GRP_H   58           /* 22 header + 1xROW_H(28) + 8 pad       */
#define H_NR4       (NR4_GRP_Y + NR4_GRP_H + 8)    /* 108 */

/* NR3 group box */
#define NR3_GRP_Y   CONT_Y
#define NR3_GRP_H   58           /* 22 header + 1 radio row(28) + 8 pad   */
#define H_NR3       (NR3_GRP_Y + NR3_GRP_H + 8)    /* 108 */

/* Off -- single message line */
#define H_SIMPLE    (CONT_Y + 42 + 8)               /* 112 */

static int mode_client_h(int mode)
{
    if (mode == 1) return H_NR1;
    if (mode == 2) return H_NR2;
    if (mode == 3) return H_NR3;
    if (mode == 4) return H_NR4;
    return H_SIMPLE;
}

/* ---- Control IDs ----------------------------------------------------- */
#define IDC_RADIO_OFF        101
#define IDC_RADIO_NR1        102
#define IDC_RADIO_NR2        103
#define IDC_RADIO_NR3        104
#define IDC_RADIO_NR4        105

#define IDC_LBL_MSG          108   /* mode-specific message (Off/NR3/NR4) */

#define IDC_GRP_NR1          110
#define IDC_SLD_TAPS         111
#define IDC_SLD_DELAY        112
#define IDC_SLD_GAIN         113
#define IDC_SLD_LEAK         114
#define IDC_HINT_NR1         119

#define IDC_GRP_NR2          120
#define IDC_COMBO_GMETHOD    121
#define IDC_COMBO_NPE        122
#define IDC_CHECK_AE         123
#define IDC_BTN_ABOUT        130

#define IDC_GRP_NR4          140
#define IDC_SLD_NR4_REDUCTION 141

#define IDC_GRP_NR3          150
#define IDC_RADIO_NR3_STD    151
#define IDC_RADIO_NR3_SMALL  152
#define IDC_RADIO_NR3_LARGE  153

#define WC_CLAPNR           "ClapNrGui_v3"

/* ---- Slider <-> parameter value converters --------------------------- */
/* Gain:    pos 1..10000  <->  value 1e-6 .. 0.01  (pos = value * 1e6)   */
/* Leakage: pos 0..1000   <->  value 0.0 .. 1.0    (pos = value * 1000)  */
static int    gain_to_slider(double g) { int p=(int)(g*1e6+.5); return p<1?1:(p>10000?10000:p); }
static double slider_to_gain(int   p)  { return p * 1e-6; }
static int    leak_to_slider(double v) { int p=(int)(v*1e3+.5); return p<0?0:(p>1000?1000:p); }
static double slider_to_leak(int   p)  { return p / 1000.0; }
static int    reduc_to_slider(float v) { int p=(int)(v*10.0f+.5f); return p<0?0:(p>200?200:p); }
static float  slider_to_reduc(int   p) { return p / 10.0f; }

/* ---- GUI struct ------------------------------------------------------- */
struct clap_nr_gui_s {
    HWND  hwnd;
    void *plugin;
    gui_param_cb_t on_param_change;
    bool  embedded;
    bool  updating;

    /* Mode strip */
    HWND radio_off, radio_nr1, radio_nr2, radio_nr3, radio_nr4;

    /* Mode-specific message label (Off / NR3 / NR4) */
    HWND lbl_msg;

    /* NR1 (ANR) controls */
    HWND grp_nr1;
    HWND lbl_taps,  sld_taps;
    HWND lbl_delay, sld_delay;
    HWND lbl_gain,  sld_gain;
    HWND lbl_leak,  sld_leak;
    HWND hint_nr1;

    /* NR2 (EMNR) controls */
    HWND grp_nr2;
    HWND lbl_gmethod, combo_gmethod;
    HWND lbl_npe,     combo_npe;
    HWND check_ae;

    /* NR4 (SBNR) controls */
    HWND grp_nr4;
    HWND lbl_nr4_reduction, sld_nr4_reduction;

    /* NR3 (RNNR) model selection */
    HWND grp_nr3;
    HWND radio_nr3_std, radio_nr3_small, radio_nr3_large;

    /* About button */
    HWND  btn_about;

    /* Tooltip window */
    HWND  tt;

    /* Cached parameter values */
    int    nr_mode;
    int    anr_taps;
    int    anr_delay;
    double anr_gain;
    double anr_leakage;
    int    emnr_gain_method;
    int    emnr_npe_method;
    int    emnr_ae_run;
    float  nr4_reduction;
    int    nr3_model;  /* 0=Standard, 1=Small, 2=Large */
};

/* ---- Tooltip helper -------------------------------------------------- */
static void tt_add(HWND tt, HWND ctrl, HWND parent, const char *text)
{
    if (!tt || !ctrl) return;
    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = parent;
    ti.uId      = (UINT_PTR)ctrl;
    ti.lpszText = (LPSTR)text;
    SendMessageA(tt, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

/* ---- Widget factory helpers ------------------------------------------ */
static HWND mk_static(HWND p, const char *t, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "STATIC", t, WS_CHILD|WS_VISIBLE|SS_LEFT,
                           x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_separator(HWND p, int x, int y, int w)
{
    return CreateWindowExA(0, "STATIC", "", WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                           x, y, w, 2, p, NULL, NULL, NULL);
}
static HWND mk_radio(HWND p, const char *t, int id, int x, int y, int w, int h, BOOL first)
{
    DWORD s = WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|(first ? WS_GROUP : 0);
    return CreateWindowExA(0, "BUTTON", t, s, x, y, w, h, p,
                           (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_groupbox(HWND p, const char *t, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "BUTTON", t, WS_CHILD|WS_VISIBLE|BS_GROUPBOX|WS_GROUP,
                           x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
}
static HWND mk_trackbar(HWND p, int id, int x, int y, int w, int h,
                        int lo, int hi, int val)
{
    HWND tb = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                              WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
                              x, y, w, h, p, (HMENU)(intptr_t)id, NULL, NULL);
    SendMessageA(tb, TBM_SETRANGEMIN, FALSE, lo);
    SendMessageA(tb, TBM_SETRANGEMAX, FALSE, hi);
    SendMessageA(tb, TBM_SETPOS,      TRUE,  val);
    return tb;
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

/* ---- Resize the OS window to fit the client height for the given mode  */
/*   For embedded (child) windows the layout fills whatever the host gave  */
/*   us without attempting to resize the parent dialog.                    */
static void resize_to_mode(clap_nr_gui_t *g, int mode)
{
    if (!g->hwnd || g->embedded) return;

    int     ch      = mode_client_h(mode);
    DWORD   style   = (DWORD)GetWindowLongA(g->hwnd, GWL_STYLE);
    DWORD   exstyle = (DWORD)GetWindowLongA(g->hwnd, GWL_EXSTYLE);
    RECT    r       = { 0, 0, GUI_W, ch };
    AdjustWindowRectEx(&r, style, FALSE, exstyle);
    SetWindowPos(g->hwnd, HWND_TOPMOST,
                 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOACTIVATE);
}

/* ---- Update the mode-specific message text --------------------------- */
static void update_mode_message(clap_nr_gui_t *g)
{
    if (!g->lbl_msg) return;
    switch (g->nr_mode) {
    case 0:
        SetWindowTextA(g->lbl_msg,
            "Noise reduction disabled — audio passes through unchanged.");
        break;

    default:
        SetWindowTextA(g->lbl_msg, "");
        break;
    }
}

/* ---- Show/hide content sections for the active mode ------------------ */
static void show_section_for_mode(clap_nr_gui_t *g, int mode)
{
    /* Off: single status message */
    int simple = (mode == 0);
    ShowWindow(g->lbl_msg, simple ? SW_SHOW : SW_HIDE);

    /* NR1 section */
    int nr1 = (mode == 1);
    HWND nr1_all[] = {
        g->grp_nr1,
        g->lbl_taps,  g->sld_taps,
        g->lbl_delay, g->sld_delay,
        g->lbl_gain,  g->sld_gain,
        g->lbl_leak,  g->sld_leak,
        g->hint_nr1,
        NULL
    };
    for (int i = 0; nr1_all[i]; ++i)
        ShowWindow(nr1_all[i], nr1 ? SW_SHOW : SW_HIDE);

    /* NR2 section */
    int nr2 = (mode == 2);
    HWND nr2_all[] = {
        g->grp_nr2,
        g->lbl_gmethod, g->combo_gmethod,
        g->lbl_npe,     g->combo_npe,
        g->check_ae,
        NULL
    };
    for (int i = 0; nr2_all[i]; ++i)
        ShowWindow(nr2_all[i], nr2 ? SW_SHOW : SW_HIDE);

    /* NR4 section */
    int nr4 = (mode == 4);
    HWND nr4_all[] = {
        g->grp_nr4,
        g->lbl_nr4_reduction, g->sld_nr4_reduction,
        NULL
    };
    for (int i = 0; nr4_all[i]; ++i)
        ShowWindow(nr4_all[i], nr4 ? SW_SHOW : SW_HIDE);

    /* NR3 section */
    int nr3 = (mode == 3);
    HWND nr3_all[] = {
        g->grp_nr3,
        g->radio_nr3_std, g->radio_nr3_small, g->radio_nr3_large,
        NULL
    };
    for (int i = 0; nr3_all[i]; ++i)
        ShowWindow(nr3_all[i], nr3 ? SW_SHOW : SW_HIDE);

    update_mode_message(g);
    resize_to_mode(g, mode);
}

/* ---- Sync slider + edit displays from cached values ------------------- */
static void sync_nr1_edits(clap_nr_gui_t *g)
{
    if (g->sld_taps)  SendMessageA(g->sld_taps,  TBM_SETPOS, TRUE, g->anr_taps);
    if (g->sld_delay) SendMessageA(g->sld_delay, TBM_SETPOS, TRUE, g->anr_delay);
    if (g->sld_gain)  SendMessageA(g->sld_gain,  TBM_SETPOS, TRUE, gain_to_slider(g->anr_gain));
    if (g->sld_leak)  SendMessageA(g->sld_leak,  TBM_SETPOS, TRUE, leak_to_slider(g->anr_leakage));
}

static void sync_nr2_combos(clap_nr_gui_t *g)
{
    SendMessageA(g->combo_gmethod, CB_SETCURSEL, (WPARAM)g->emnr_gain_method, 0);
    SendMessageA(g->combo_npe,     CB_SETCURSEL, (WPARAM)g->emnr_npe_method,  0);
    SendMessageA(g->check_ae, BM_SETCHECK,
                 g->emnr_ae_run ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void sync_nr4_slider(clap_nr_gui_t *g)
{
    if (g->sld_nr4_reduction)
        SendMessageA(g->sld_nr4_reduction, TBM_SETPOS, TRUE,
                     reduc_to_slider(g->nr4_reduction));
}

static void sync_nr3_radios(clap_nr_gui_t *g)
{
    if (!g->hwnd) return;
    int v = (g->nr3_model >= 0 && g->nr3_model <= 2) ? g->nr3_model : 2;
    CheckRadioButton(g->hwnd, IDC_RADIO_NR3_STD, IDC_RADIO_NR3_LARGE,
                     IDC_RADIO_NR3_STD + v);
}

/* ---- Window procedure ------------------------------------------------- */
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
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_HSCROLL: {
        if (!g || g->updating) break;
        HWND    sld = (HWND)lp;
        int     pos = (int)SendMessageA(sld, TBM_GETPOS, 0, 0);
        BOOL    changed     = FALSE;
        clap_id changed_id  = 0;
        double  changed_val = 0.0;

        g->updating = true;

        if (sld == g->sld_taps && pos != g->anr_taps) {
            g->anr_taps = pos;
            changed = TRUE; changed_id = _GUI_PARAM_ANR_TAPS; changed_val = (double)pos;

        } else if (sld == g->sld_delay && pos != g->anr_delay) {
            g->anr_delay = pos;
            changed = TRUE; changed_id = _GUI_PARAM_ANR_DELAY; changed_val = (double)pos;

        } else if (sld == g->sld_gain) {
            double v = slider_to_gain(pos);
            if (v != g->anr_gain) {
                g->anr_gain = v;
                changed = TRUE; changed_id = _GUI_PARAM_ANR_GAIN; changed_val = v;
            }
        } else if (sld == g->sld_leak) {
            double v = slider_to_leak(pos);
            if (v != g->anr_leakage) {
                g->anr_leakage = v;
                changed = TRUE; changed_id = _GUI_PARAM_ANR_LEAKAGE; changed_val = v;
            }
        } else if (sld == g->sld_nr4_reduction) {
            float v = slider_to_reduc(pos);
            if (v != g->nr4_reduction) {
                g->nr4_reduction = v;
                changed = TRUE; changed_id = _GUI_PARAM_NR4_REDUCTION; changed_val = (double)v;
            }
        }

        /* Always reset before callback to avoid the stuck-flag deadlock. */
        g->updating = false;
        if (changed)
            g->on_param_change(g->plugin, changed_id, changed_val);
        return 0;
    }

    case WM_COMMAND: {
        if (!g || g->updating) break;
        int ctrl_id = LOWORD(wp);
        int notif   = HIWORD(wp);

        /* About dialog */
        if (ctrl_id == IDC_BTN_ABOUT && notif == BN_CLICKED) {
            int r = MessageBoxA(hwnd,
                "CLAP NR  v" CLAP_NR_VERSION_STR "\n\n"
                "Learn more and read the credits at clapnr.com\n\n"
                "Click Yes to open clapnr.com in your browser.",
                "About CLAP NR",
                MB_YESNO | MB_ICONINFORMATION);
            if (r == IDYES)
                ShellExecuteA(NULL, "open", "https://clapnr.com", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }

        /* NR mode radio buttons */
        if (ctrl_id >= IDC_RADIO_OFF && ctrl_id <= IDC_RADIO_NR4
                && notif == BN_CLICKED) {
            int new_mode = ctrl_id - IDC_RADIO_OFF;
            if (new_mode != g->nr_mode) {
                g->nr_mode = new_mode;
                g->on_param_change(g->plugin, _GUI_PARAM_NR_MODE, (double)new_mode);
                show_section_for_mode(g, new_mode);
            }
            return 0;
        }

        /* NR3 model radio buttons */
        if (ctrl_id >= IDC_RADIO_NR3_STD && ctrl_id <= IDC_RADIO_NR3_LARGE
                && notif == BN_CLICKED) {
            int new_model = ctrl_id - IDC_RADIO_NR3_STD;
            if (new_model != g->nr3_model) {
                g->nr3_model = new_model;
                g->on_param_change(g->plugin, _GUI_PARAM_NR3_MODEL, (double)new_model);
            }
            return 0;
        }

        /* NR2 combos and checkbox */
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
        break;
    }

    case WM_GETMINMAXINFO: {
        if (!g) break;
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        /* Compute minimum window dimensions from the minimum client size
         * for the current mode, accounting for the non-client area. */
        int   ch      = mode_client_h(g->nr_mode);
        DWORD style   = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
        DWORD exstyle = (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE);
        RECT  r       = { 0, 0, GUI_W, ch };
        AdjustWindowRectEx(&r, style, FALSE, exstyle);
        mmi->ptMinTrackSize.x = r.right  - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_DESTROY:
        if (g) g->hwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---- Register window class (idempotent) ------------------------------- */
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
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WC_CLAPNR;
    RegisterClassExA(&wc);
}

/* ---- Build all child controls ---------------------------------------- */
static void create_controls(clap_nr_gui_t *g, HWND hwnd)
{
    /* --- Mode strip (single row) ---
     *  y=8: [None]  [NR1 (ANR)]  [NR2 (EMNR)]  [NR3 (RNNR)]  [NR4 (SBNR)]
     *  y=34: separator
     */
    g->radio_off = mk_radio(hwnd, "None",       IDC_RADIO_OFF, M,       ROW1_Y, 56, 22, TRUE);
    g->radio_nr1 = mk_radio(hwnd, "NR1 (ANR)",  IDC_RADIO_NR1, M+60,   ROW1_Y, 88, 22, FALSE);
    g->radio_nr2 = mk_radio(hwnd, "NR2 (EMNR)", IDC_RADIO_NR2, M+152,  ROW1_Y, 96, 22, FALSE);
    g->radio_nr3 = mk_radio(hwnd, "NR3 (RNNR)", IDC_RADIO_NR3, M+252,  ROW1_Y, 96, 22, FALSE);
    g->radio_nr4 = mk_radio(hwnd, "NR4 (SBNR)", IDC_RADIO_NR4, M+352,  ROW1_Y, 96, 22, FALSE);

    /* About button -- right-aligned in the mode strip row */
    g->btn_about = CreateWindowExA(0, "BUTTON", "?",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   GUI_W - M - 28, ROW1_Y, 28, 22,
                                   hwnd, (HMENU)(intptr_t)IDC_BTN_ABOUT,
                                   NULL, NULL);

    mk_separator(hwnd, M, SEP_Y, GRP_W);

    /* --- Mode-specific message (used for Off / NR3 / NR4) ------------- */
    g->lbl_msg = mk_static(hwnd, "", IDC_LBL_MSG,
                           M, CONT_Y + 12, GRP_W, LBL_H * 2);

    /* --- NR1 group: Adaptive LMS (ANR) -------------------------------- */
    g->grp_nr1 = mk_groupbox(hwnd, "NR1 - Adaptive LMS (ANR) Parameters",
                             IDC_GRP_NR1, M, NR1_GRP_Y, GRP_W, NR1_GRP_H);

    int ry = NR1_GRP_Y + 22;
#define NR1_ROW(lh, sh, txt, sid, lo, hi, sval) \
    lh = mk_static  (hwnd, txt, 0,  ROW_LBL_X, ry+3,  ROW_LBL_W, LBL_H); \
    sh = mk_trackbar(hwnd, sid,      ROW_SLD_X, ry,    ROW_SLD_W, SLD_H, lo, hi, sval); \
    ry += ROW_H;

    NR1_ROW(g->lbl_taps,  g->sld_taps,
            "Taps:",   IDC_SLD_TAPS,   16,  2048, g->anr_taps)
    NR1_ROW(g->lbl_delay, g->sld_delay,
            "Delay:",  IDC_SLD_DELAY,   1,   512, g->anr_delay)
    NR1_ROW(g->lbl_gain,  g->sld_gain,
            "Gain:",   IDC_SLD_GAIN,    1, 10000, gain_to_slider(g->anr_gain))
    NR1_ROW(g->lbl_leak,  g->sld_leak,
            "Leakage:",IDC_SLD_LEAK,    0,  1000, leak_to_slider(g->anr_leakage))
#undef NR1_ROW

    g->hint_nr1 = mk_static(hwnd,
        "Taps 16-2048  |  Delay 1-512  |  Gain 1e-6 to 0.01  |  Leakage 0.0-1.0",
        IDC_HINT_NR1, M, NR1_HINT_Y, GRP_W, LBL_H);

    /* --- NR2 group: Spectral MMSE (EMNR) ------------------------------ */
    g->grp_nr2 = mk_groupbox(hwnd, "NR2 - Spectral MMSE (EMNR) Parameters",
                             IDC_GRP_NR2, M, NR2_GRP_Y, GRP_W, NR2_GRP_H);

    ry = NR2_GRP_Y + 22;
    g->lbl_gmethod   = mk_static(hwnd, "Gain Method:", 0, ROW_LBL_X, ry+3, NR2_LBL_W, LBL_H);
    g->combo_gmethod = mk_combo (hwnd, IDC_COMBO_GMETHOD, NR2_CMB_X, ry, NR2_CMB_W);
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"RROE");
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"MEPSE");
    SendMessageA(g->combo_gmethod, CB_ADDSTRING, 0, (LPARAM)"MM-LSA");
    ry += ROW_H;

    g->lbl_npe   = mk_static(hwnd, "NPE Method:", 0, ROW_LBL_X, ry+3, NR2_LBL_W, LBL_H);
    g->combo_npe = mk_combo (hwnd, IDC_COMBO_NPE, NR2_CMB_X, ry, NR2_CMB_W);
    SendMessageA(g->combo_npe, CB_ADDSTRING, 0, (LPARAM)"OSMS");
    SendMessageA(g->combo_npe, CB_ADDSTRING, 0, (LPARAM)"MMSE");
    ry += ROW_H;

    g->check_ae = mk_check(hwnd, "Audio Enhance (AE)", IDC_CHECK_AE,
                           ROW_LBL_X, ry, 180, 22);

    /* --- NR4 group: Spectral denoiser (SBNR) -------------------------- */
    g->grp_nr4 = mk_groupbox(hwnd, "NR4 - Adaptive Spectral Denoiser (SBNR) Parameters",
                             IDC_GRP_NR4, M, NR4_GRP_Y, GRP_W, NR4_GRP_H);

    ry = NR4_GRP_Y + 22;
    g->lbl_nr4_reduction = mk_static(hwnd, "Reduction:", 0,
                                     ROW_LBL_X, ry+3, ROW_LBL_W, LBL_H);
    g->sld_nr4_reduction = mk_trackbar(hwnd, IDC_SLD_NR4_REDUCTION,
                                       ROW_SLD_X, ry, ROW_SLD_W, SLD_H,
                                       0, 200, reduc_to_slider(g->nr4_reduction));

    /* --- NR3 group: RNNoise neural net (RNNR) model selection ---------- */
    g->grp_nr3 = mk_groupbox(hwnd, "NR3 - RNNoise Neural Net (RNNR) Model",
                             IDC_GRP_NR3, M, NR3_GRP_Y, GRP_W, NR3_GRP_H);

    ry = NR3_GRP_Y + 26;
    g->radio_nr3_std   = mk_radio(hwnd, "Standard (built-in)", IDC_RADIO_NR3_STD,
                                  M + 10, ry, 165, 22, TRUE);
    g->radio_nr3_small = mk_radio(hwnd, "Small",               IDC_RADIO_NR3_SMALL,
                                  M + 185, ry, 70, 22, FALSE);
    g->radio_nr3_large = mk_radio(hwnd, "Large",               IDC_RADIO_NR3_LARGE,
                                  M + 265, ry, 70, 22, FALSE);

    /* --- Tooltips ----------------------------------------------------- */
    g->tt = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
                            WS_POPUP | TTS_ALWAYSTIP,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            hwnd, NULL, GetModuleHandleA(NULL), NULL);
    if (g->tt) {
        /* Allow long single-line text to wrap at word boundaries */
        SendMessageA(g->tt, TTM_SETMAXTIPWIDTH, 0, 300);
        /* Keep tooltip visible long enough to read */
        SendMessageA(g->tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);

        /* NR1 sliders */
        tt_add(g->tt, g->sld_taps, hwnd,
            "NR1 Taps (16-2048): Filter length of the adaptive LMS filter. "
            "More taps = better suppression of tonal noise but higher CPU cost. "
            "Default: 64.");
        tt_add(g->tt, g->sld_delay, hwnd,
            "NR1 Delay (1-512 samples): Decorrelation delay between the input and the "
            "reference signal. Increase for narrowband carriers/tones; decrease for "
            "broadband hiss. Default: 16.");
        tt_add(g->tt, g->sld_gain, hwnd,
            "NR1 Gain / two_mu (1e-6 to 0.01): LMS adaptation step size. "
            "Higher values adapt faster but risk instability. "
            "Lower values are more stable but slower to track changes. Default: 0.0001.");
        tt_add(g->tt, g->sld_leak, hwnd,
            "NR1 Leakage / gamma (0.0 to 1.0): Per-sample weight decay applied to filter "
            "coefficients. Prevents drift when noise is stationary. "
            "0 = no leakage (coefficients persist). Default: 0.1.");

        /* NR2 controls */
        tt_add(g->tt, g->combo_gmethod, hwnd,
            "NR2 Gain Method: spectral gain function used after noise estimation. "
            "RROE = Residual Output Only Estimator. "
            "MEPSE = Minimum Error Power Spectral Estimator. "
            "MM-LSA = Log-Spectral Amplitude (recommended, least musical noise). Default: MM-LSA.");
        tt_add(g->tt, g->combo_npe, hwnd,
            "NR2 NPE Method: algorithm used to estimate the noise power spectrum. "
            "OSMS = Optimally-Smoothed Minimum Statistics (tracks noise floor quickly). "
            "MMSE = Minimum Mean Square Error (smoother, slightly slower to adapt). Default: OSMS.");
        tt_add(g->tt, g->check_ae, hwnd,
            "NR2 Audio Enhance: applies a secondary gain mask after noise suppression "
            "to sharpen transients and improve intelligibility. "
            "Recommended: On.");
        tt_add(g->tt, g->sld_nr4_reduction, hwnd,
            "NR4 Reduction (0-20 dB): Amount of noise attenuation applied by the "
            "adaptive spectral denoiser. Higher values suppress more noise but "
            "may affect speech clarity. Default: 10 dB.");
        tt_add(g->tt, g->grp_nr3, hwnd,
            "NR3 Model: select the RNNoise neural network weight file used for "
            "noise suppression. Standard uses the model built into the DLL. "
            "Small and Large load an external .bin file from the CLAP install folder. "
            "Larger models give stronger suppression at higher CPU cost.");
        tt_add(g->tt, g->btn_about, hwnd,
            "Show version and build information.");
    }

    /* --- Populate values and apply initial mode visibility ------------ */
    sync_nr1_edits(g);
    sync_nr2_combos(g);
    sync_nr4_slider(g);
    sync_nr3_radios(g);
    CheckRadioButton(hwnd, IDC_RADIO_OFF, IDC_RADIO_NR4,
                     IDC_RADIO_OFF + g->nr_mode);
    show_section_for_mode(g, g->nr_mode);
}

/* ---- Public API ------------------------------------------------------- */

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
    g->nr4_reduction    = 10.0f;
    g->nr3_model        = 2;  /* Large */
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

    int ch = mode_client_h(gui->nr_mode);
    gui->hwnd = CreateWindowExA(
        0, WC_CLAPNR, "CLAP NR",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, GUI_W, ch,
        (HWND)window->win32, NULL, GetModuleHandleA(NULL), gui);
    if (!gui->hwnd) return false;
    create_controls(gui, gui->hwnd);
    return true;
}

void gui_get_size(clap_nr_gui_t *gui, uint32_t *out_w, uint32_t *out_h)
{
    *out_w = GUI_W;
    *out_h = (uint32_t)(gui ? mode_client_h(gui->nr_mode) : H_NR1);
}

bool gui_show(clap_nr_gui_t *gui)
{
    if (!gui) return false;

    if (!gui->hwnd) {
        /* No parent set -- create a floating tool window. */
        register_class();
        gui->embedded = false;

        /* Position near cursor, clamped to monitor working area. */
        POINT pt = { 200, 200 };
        GetCursorPos(&pt);
        int wx = pt.x - GUI_W / 4;
        int wy = pt.y + 16;

        /* Compute total window size from the initial client height. */
        int   ch      = mode_client_h(gui->nr_mode);
        DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
        DWORD exstyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        RECT  r       = { 0, 0, GUI_W, ch };
        AdjustWindowRectEx(&r, style, FALSE, exstyle);
        int ww = r.right  - r.left;
        int wh = r.bottom - r.top;

        HMONITOR   hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi  = { sizeof(mi) };
        if (GetMonitorInfoA(hmon, &mi)) {
            if (wx + ww > mi.rcWork.right)  wx = mi.rcWork.right  - ww;
            if (wy + wh > mi.rcWork.bottom) wy = mi.rcWork.bottom - wh;
            if (wx < mi.rcWork.left)        wx = mi.rcWork.left;
            if (wy < mi.rcWork.top)         wy = mi.rcWork.top;
        }

        gui->hwnd = CreateWindowExA(
            exstyle, WC_CLAPNR, "CLAP NR",
            style,
            wx, wy, ww, wh,
            NULL, NULL, GetModuleHandleA(NULL), gui);
        if (!gui->hwnd) return false;
        create_controls(gui, gui->hwnd);
    }

    if (IsIconic(gui->hwnd))
        ShowWindow(gui->hwnd, SW_RESTORE);
    else
        ShowWindow(gui->hwnd, SW_SHOW);

    if (gui->embedded) {
        BringWindowToTop(gui->hwnd);
        SetFocus(gui->hwnd);
    } else {
        /* Re-assert topmost and bring to foreground. */
        SetWindowPos(gui->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    if (!gui || !gui->hwnd) return false;
    /* For embedded (child) windows: honour host-driven resize.
     * For floating windows: we control our own size independently. */
    if (!gui->embedded) return false;
    int min_h = mode_client_h(gui->nr_mode);
    int new_w = (int)w < GUI_W   ? GUI_W   : (int)w;
    int new_h = (int)h < min_h   ? min_h   : (int)h;
    SetWindowPos(gui->hwnd, NULL, 0, 0, new_w, new_h,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    return true;
}

void gui_set_param(clap_nr_gui_t *gui, clap_id param_id, double value)
{
    if (!gui) return;
    gui->updating = true;

    switch (param_id) {
    case _GUI_PARAM_NR_MODE:
        gui->nr_mode = (int)value;
        if (gui->hwnd) {
            CheckRadioButton(gui->hwnd, IDC_RADIO_OFF, IDC_RADIO_NR4,
                             IDC_RADIO_OFF + gui->nr_mode);
            show_section_for_mode(gui, gui->nr_mode);
        }
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
    case _GUI_PARAM_NR4_REDUCTION:
        gui->nr4_reduction = (float)value;
        if (gui->hwnd) sync_nr4_slider(gui);
        break;
    case _GUI_PARAM_NR3_MODEL:
        gui->nr3_model = (int)value;
        if (gui->hwnd) sync_nr3_radios(gui);
        break;
    default:
        break;
    }

    gui->updating = false;
}

#endif /* _WIN32 */
