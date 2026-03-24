/*
 * gui_imgui.cpp  -  Dear ImGui cross-platform GUI for clap-nr
 *
 * Implements the gui.h interface for all supported platforms:
 *   Windows  : Win32 platform layer + DirectX 11 renderer
 *   Linux    : GLFW platform layer + OpenGL 3.3 renderer (render thread)
 *   macOS    : GLFW platform layer + OpenGL 3.3 renderer (render thread)
 *
 * Threading model:
 *   All ImGui calls happen on a single thread that owns the window context.
 *   On Windows this is the host main thread (driven by WM_TIMER).
 *   On Linux/macOS this is a dedicated render thread so the host main thread
 *   is never blocked.
 *   The audio thread only writes parameters via gui_set_param(); the render
 *   thread reads those cached values each frame - no locking needed because
 *   all writes are 32/64-bit aligned and the worst case is one stale frame.
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
 */

/* -----------------------------------------------------------------------
 * Platform-specific system headers + ImGui backend headers
 * --------------------------------------------------------------------- */
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#  include <shellapi.h>
#  include <d3d11.h>
#  include "imgui_impl_win32.h"
#  include "imgui_impl_dx11.h"
   /* Forward-declare the ImGui Win32 message handler */
   extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#elif defined(__APPLE__)
    /* macOS: Cocoa NSWindow + NSOpenGLView, main-thread rendering via NSTimer */
    /* Silence deprecation warnings — OpenGL still works on macOS 15 */
#  define GL_SILENCE_DEPRECATION 1
#  import  <Cocoa/Cocoa.h>
#  include <OpenGL/gl3.h>
#  include <OpenGL/CGLContext.h>
#  include "imgui_impl_opengl3.h"
#else
    /* Linux: GLFW + OpenGL 3.3 */
#  include <GLFW/glfw3.h>
#  include <pthread.h>
#  include <time.h>
#  include <GL/gl.h>
#  include "imgui_impl_glfw.h"
#  include "imgui_impl_opengl3.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ImGui core */
#include "imgui.h"

/* Plugin headers */
#include "gui.h"
#include "version.h"

/* -----------------------------------------------------------------------
 * Param indices  (must stay in sync with clap_nr.c)
 * --------------------------------------------------------------------- */
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
    _GUI_PARAM_NR3_STRENGTH     = 10,
    _GUI_PARAM_NR0_AGGRESSION   = 11,
    _GUI_PARAM_NR0_MAX_NOTCHES  = 12,
    _GUI_PARAM_NR0_THRESHOLD    = 13,
};

/* -----------------------------------------------------------------------
 * Colours / theme  --  Dark Amber: near-true-black + rich warm amber
 *   COL_TEXT is warm cream (not accent) so labels/buttons have hierarchy.
 *   Buttons sit near-black by default; ButtonHovered jumps to dark amber
 *   glow for strong feedback; selected state = full amber + black text.
 * --------------------------------------------------------------------- */
static const ImVec4 COL_ACCENT   = {0.36f, 0.55f, 0.93f, 1.00f}; /* blue  */
static const ImVec4 COL_ACCENT2  = {0.24f, 0.81f, 0.56f, 1.00f}; /* green */
static const ImVec4 COL_BG       = {0.09f, 0.10f, 0.13f, 1.00f}; /* near-black */
static const ImVec4 COL_SURFACE  = {0.13f, 0.14f, 0.19f, 1.00f}; /* panel */
static const ImVec4 COL_BORDER   = {0.20f, 0.21f, 0.27f, 1.00f};
static const ImVec4 COL_TEXT     = {0.83f, 0.85f, 0.91f, 1.00f};
static const ImVec4 COL_MUTED    = {0.48f, 0.50f, 0.60f, 1.00f};

static void apply_theme()
{
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.GrabRounding      = 4.0f;
    s.PopupRounding     = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.TabRounding       = 4.0f;
    s.FramePadding      = {8.0f, 4.0f};
    s.ItemSpacing       = {8.0f, 6.0f};
    s.WindowPadding     = {12.0f, 10.0f};
    s.GrabMinSize       = 10.0f;
    s.ScrollbarSize     = 12.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;

    /* Every alpha is 1.0 -- no bleed-through anywhere */
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]            = COL_BG;
    c[ImGuiCol_ChildBg]             = COL_SURFACE;
    c[ImGuiCol_PopupBg]             = COL_SURFACE;
    c[ImGuiCol_Border]              = COL_BORDER;
    c[ImGuiCol_Text]                = COL_TEXT;
    c[ImGuiCol_TextDisabled]        = COL_MUTED;
    c[ImGuiCol_FrameBg]             = {0.16f, 0.17f, 0.23f, 1.00f};
    c[ImGuiCol_FrameBgHovered]      = {0.20f, 0.22f, 0.30f, 1.00f};
    c[ImGuiCol_FrameBgActive]       = {0.24f, 0.26f, 0.36f, 1.00f};
    c[ImGuiCol_TitleBg]             = {0.07f, 0.08f, 0.11f, 1.00f};
    c[ImGuiCol_TitleBgActive]       = {0.09f, 0.10f, 0.14f, 1.00f};
    c[ImGuiCol_Header]              = {0.18f, 0.30f, 0.58f, 0.80f};
    c[ImGuiCol_HeaderHovered]       = {0.26f, 0.40f, 0.70f, 0.90f};
    c[ImGuiCol_HeaderActive]        = COL_ACCENT;
    c[ImGuiCol_Button]              = {0.18f, 0.28f, 0.55f, 0.80f};
    c[ImGuiCol_ButtonHovered]       = {0.26f, 0.38f, 0.68f, 0.90f};
    c[ImGuiCol_ButtonActive]        = COL_ACCENT;
    c[ImGuiCol_SliderGrab]          = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive]    = {0.50f, 0.70f, 1.00f, 1.00f};
    c[ImGuiCol_CheckMark]           = COL_ACCENT2;
    c[ImGuiCol_Separator]           = COL_BORDER;
    c[ImGuiCol_Tab]                 = {0.12f, 0.14f, 0.20f, 1.00f};
    c[ImGuiCol_TabHovered]          = {0.26f, 0.38f, 0.68f, 0.90f};
    c[ImGuiCol_TabActive]           = {0.20f, 0.30f, 0.58f, 1.00f};
    c[ImGuiCol_ScrollbarBg]         = {0.07f, 0.08f, 0.11f, 1.00f};
    c[ImGuiCol_ScrollbarGrab]       = {0.22f, 0.24f, 0.32f, 1.00f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.30f, 0.33f, 0.44f, 1.00f};
    c[ImGuiCol_ScrollbarGrabActive] = COL_ACCENT;
    c[ImGuiCol_ResizeGrip]          = {0.28f, 0.21f, 0.06f, 1.00f};
    c[ImGuiCol_ResizeGripHovered]   = COL_ACCENT;
    c[ImGuiCol_ResizeGripActive]    = {1.00f, 0.75f, 0.12f, 1.00f};
    c[ImGuiCol_NavHighlight]        = COL_ACCENT;
}

/* -----------------------------------------------------------------------
 * GUI struct
 * --------------------------------------------------------------------- */
struct clap_nr_gui_s {
    /* Ownership */
    void               *plugin;
    gui_param_cb_t      on_param_change;
    gui_tooltips_cb_t   on_tooltips_change;
    gui_close_cb_t      on_close;

#ifdef _WIN32
    /* Window / D3D11 (Windows) */
    HWND                    hwnd;
    bool                    embedded;
    ID3D11Device           *d3d_device;
    ID3D11DeviceContext    *d3d_ctx;
    IDXGISwapChain         *swap_chain;
    ID3D11RenderTargetView *rtv;
    UINT                    sc_w, sc_h;  /* swapchain dimensions */
    UINT_PTR                timer_id;
#elif defined(__APPLE__)
    /* macOS: Cocoa NSWindow + NSOpenGLView rendered on the MAIN THREAD.
     * All GL calls happen on the main thread via an NSTimer, matching how
     * Qt6 drives its own OpenGL widgets — no concurrent GL state corruption. */
    NSWindow           *window;
    NSOpenGLView       *gl_view;
    NSTimer            *render_timer;
    volatile bool       visible;
#else
    /* Linux: GLFW + OpenGL 3.3 render thread */
    GLFWwindow     *glfw_win;
    pthread_t       render_thread;
    volatile bool   running;
    volatile bool   visible;
    volatile bool   thread_ready;
#endif

    /* Cached parameter values (written by gui_set_param on main thread,
     * read by the render loop on the same thread -- no locking needed). */
    int    nr_mode;
    int    anr_taps;
    int    anr_delay;
    double anr_gain;
    double anr_leakage;
    int    emnr_gain_method;
    int    emnr_npe_method;
    int    emnr_ae_run;
    float  nr4_reduction;
    int    nr3_model;
    float  nr3_strength;
    float  nr0_aggression;       /* 0.0-100.0 */
    int    nr0_max_notches;      /* 1-10       */
    float  nr0_threshold;        /* 3.0-40.0 dB above local floor */
    volatile int nr0_active_notches; /* tones being notched (written by audio thread) */

    /* Per-instance ImGui context.  Each plugin instance creates its own
     * ImGuiContext so that multiple instances can coexist in the same process
     * without sharing / overwriting the global GImGui pointer. */
    ImGuiContext *imgui_ctx;

    /* Prevent feedback when we write to a widget from gui_set_param */
    bool   updating;
    bool   tooltips_on;   /* show on-hover tooltips */
    bool   open_website;  /* deferred ShellExecute after Present() */

    /* Enforced minimum client-area size (updated each frame) */
    int    min_w;
    int    min_h;

    /* Floating window title constructed at create time; may be overridden
     * later by the host via gui_suggest_title(). */
    char   window_title[256];

#ifdef _WIN32
    /* Owner window: set via gui_set_transient() so the floating window
     * minimises/restores with the host application and stays above it.
     * nullptr until the host calls set_transient. */
    HWND   owner_hwnd;
#endif
};

/* -----------------------------------------------------------------------
 * Fixed logical size (pre-DPI; scaled by ImGui font/DPI internally)
 * --------------------------------------------------------------------- */
#define GUI_BASE_W  750
#define GUI_BASE_H  300   /* height adapts per-mode at runtime */

/* -----------------------------------------------------------------------
 * D3D11 helpers  (Windows only)
 * --------------------------------------------------------------------- */
#ifdef _WIN32
static bool d3d_create(clap_nr_gui_s *g, HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g->swap_chain, &g->d3d_device, &fl, &g->d3d_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D *back = nullptr;
    g->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    g->d3d_device->CreateRenderTargetView(back, nullptr, &g->rtv);
    back->Release();

    RECT rc; GetClientRect(hwnd, &rc);
    g->sc_w = (UINT)(rc.right  - rc.left);
    g->sc_h = (UINT)(rc.bottom - rc.top);
    return true;
}

static void d3d_resize(clap_nr_gui_s *g, UINT w, UINT h)
{
    if (g->rtv) { g->rtv->Release(); g->rtv = nullptr; }
    g->swap_chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D *back = nullptr;
    g->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    g->d3d_device->CreateRenderTargetView(back, nullptr, &g->rtv);
    back->Release();
    g->sc_w = w; g->sc_h = h;
}

static void d3d_destroy(clap_nr_gui_s *g)
{
    if (g->rtv)        { g->rtv->Release();        g->rtv        = nullptr; }
    if (g->swap_chain) { g->swap_chain->Release();  g->swap_chain = nullptr; }
    if (g->d3d_ctx)    { g->d3d_ctx->Release();     g->d3d_ctx    = nullptr; }
    if (g->d3d_device) { g->d3d_device->Release();  g->d3d_device = nullptr; }
}
#endif /* _WIN32 */

/* -----------------------------------------------------------------------
 * ImGui helper widgets
 * --------------------------------------------------------------------- */

/* per-frame tooltip visibility flag (set at top of render_frame).
 * Declared here so show_tooltip_text can read it; written at the start of
 * each render_frame() call from the per-instance g->tooltips_on field.
 * On Windows this is safe: render_frame runs on the single main thread and
 * SetCurrentContext is called first.  On Linux/macOS each instance has its
 * own render thread, which also owns the ImGui context for that instance, so
 * there is no cross-instance data race.  The variable is thread_local so
 * each render thread has its own copy. */
static thread_local bool s_show_tooltips = true;

/* Show a wrapped tooltip for the last-drawn widget if tooltips are enabled. */
static void show_tooltip_text(const char *text)
{
    if (!s_show_tooltips || !text) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/* Labelled horizontal slider that displays a formatted value.
 * Returns true when the value changed. */
static bool param_slider_float(const char *label, const char *tooltip,
                                float *v, float lo, float hi,
                                const char *fmt, float width = -1.0f)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    if (tooltip) show_tooltip_text(tooltip);
    ImGui::TableSetColumnIndex(1);
    if (width > 0) ImGui::SetNextItemWidth(width);
    else           ImGui::SetNextItemWidth(-1.0f);
    changed = ImGui::SliderFloat("##v", v, lo, hi, fmt);
    if (tooltip) show_tooltip_text(tooltip);
    ImGui::PopID();
    return changed;
}

static bool param_slider_int(const char *label, const char *tooltip,
                              int *v, int lo, int hi,
                              const char *fmt = "%d", float width = -1.0f)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    if (tooltip) show_tooltip_text(tooltip);
    ImGui::TableSetColumnIndex(1);
    if (width > 0) ImGui::SetNextItemWidth(width);
    else           ImGui::SetNextItemWidth(-1.0f);
    changed = ImGui::SliderInt("##v", v, lo, hi, fmt);
    if (tooltip) show_tooltip_text(tooltip);
    ImGui::PopID();
    return changed;
}

/* Begin a two-column parameter table (label col | control col) */
static void begin_param_table(const char *id)
{
    ImGui::BeginTable(id, 2,
        ImGuiTableFlags_None,
        ImVec2(-1, 0));
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("ctrl",  ImGuiTableColumnFlags_WidthStretch);
}

/* -----------------------------------------------------------------------
 * Render one frame of the plugin GUI
 * --------------------------------------------------------------------- */
static void render_frame(clap_nr_gui_s *g)
{
    ImGui::SetCurrentContext(g->imgui_ctx);
    s_show_tooltips = g->tooltips_on;
#ifdef _WIN32
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
#elif defined(__APPLE__)
    /* DisplaySize/DeltaTime already set by mac_update_io() before this call */
    ImGui_ImplOpenGL3_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
#endif
    ImGui::NewFrame();

    /* Full-window child so we fill the swapchain surface */
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    bool changed   = false;
    int  param_id  = 0;
    double new_val = 0.0;

    /* ---- Mode strip -------------------------------------------------- */
    const char *mode_labels[] = { "NR Off", "NR 0", "NR 1", "NR 2", "NR 3", "NR 4" };
    const int   mode_values[] = { 0,        5,      1,      2,      3,      4      };

    /* Compute minimum window width from the header row every frame.
     * Sum: WindowPadding*2 + 6 mode buttons + spacing between them
     *    + spacing + Tips button (72) + spacing + About button (28). */
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        float w = st.WindowPadding.x * 2.0f;
        for (int i = 0; i <= 5; ++i) {
            w += ImGui::CalcTextSize(mode_labels[i]).x + st.FramePadding.x * 2.0f;
            if (i < 5) w += st.ItemSpacing.x;
        }
        w += st.ItemSpacing.x + 72.0f + st.ItemSpacing.x + 28.0f;
        g->min_w = (int)ceilf(w);
    }
    for (int i = 0; i <= 5; ++i) {
        if (i > 0) ImGui::SameLine();
        bool sel = (g->nr_mode == mode_values[i]);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_Text,          {0.00f, 0.00f, 0.00f, 1.00f});
        }
        if (ImGui::Button(mode_labels[i]) && !g->updating) {
            if (g->nr_mode != mode_values[i]) {
                g->nr_mode = mode_values[i];
                changed = true; param_id = _GUI_PARAM_NR_MODE; new_val = mode_values[i];
            }
        }
        if (sel) ImGui::PopStyleColor(3);
        {
            const char *tt[] = {
                "Disable noise reduction - audio passes through unchanged.",
                "NR0: FFT spectral tone notcher. Eliminates carriers, heterodynes and whistles.",
                "NR1: Adaptive LMS (ANR). Fast, low-latency, effective on stationary tones.",
                "NR2: Spectral MMSE (EMNR). Broad-band noise floor reduction.",
                "NR3: RNNoise neural-net denoiser. Good general-purpose speech denoising.",
                "NR4: libspecbleach adaptive spectral denoiser."
            };
            show_tooltip_text(tt[i]);
        }
    }

    /* Right-aligned: Tips toggle + About button */
    {
        float sp      = ImGui::GetStyle().ItemSpacing.x;
        float tips_w  = 72.0f;
        float about_w = 28.0f;
        float total   = tips_w + sp + about_w;
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total);
        const char *tips_lbl = g->tooltips_on ? "Tips: ON " : "Tips: OFF";
        if (ImGui::Button(tips_lbl, {tips_w, 0})) {
            g->tooltips_on = !g->tooltips_on;
            if (g->on_tooltips_change)
                g->on_tooltips_change(g->plugin, g->tooltips_on);
        }
        ImGui::SameLine();
        if (ImGui::Button("?", {about_w, 0}))
            ImGui::OpenPopup("About##popup");
        show_tooltip_text("Show version and build information.");
    }

    ImGui::Separator();

    /* ---- Mode-specific panels ---------------------------------------- */

    /* -- Off ------------------------------------------------------------ */
    if (g->nr_mode == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_MUTED);
        ImGui::TextWrapped("Noise reduction disabled - audio passes through unchanged.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("Available modes:");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::BulletText("NR 0  FFT spectral tone notcher. Targets specific carriers, "
                          "heterodynes, and sweeping whistles.");
        ImGui::Spacing();
        ImGui::BulletText("NR 1  Adaptive LMS notch filter. Fast, low-latency. Best for "
                          "carriers and tones.");
        ImGui::Spacing();
        ImGui::BulletText("NR 2  Spectral subtraction (EMNR). Broadband noise floor "
                          "reduction. Best for white/band noise.");
        ImGui::Spacing();
        ImGui::BulletText("NR 3  RNNoise neural network denoiser. AI-based, excellent on "
                          "speech and mic noise.");
        ImGui::Spacing();
        ImGui::BulletText("NR 4  SpecBleach adaptive denoiser. Broadband, with adjustable "
                          "suppression strength.");
    }

    /* -- NR1 (ANR) ------------------------------------------------------ */
    if (g->nr_mode == 1) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SURFACE);
        ImGui::BeginChild("##nr1grp", {-1, 0}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("NR1 - Adaptive LMS (ANR)");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        begin_param_table("##nr1");

        int taps = g->anr_taps;
        if (param_slider_int("Taps", "Filter length (16-2048). More taps = narrower notch "
                             "but higher CPU cost. Default: 64.", &taps, 16, 2048)) {
            if (!g->updating) {
                g->anr_taps = taps;
                changed = true; param_id = _GUI_PARAM_ANR_TAPS; new_val = taps;
            }
        }

        int delay = g->anr_delay;
        if (param_slider_int("Delay", "Decorrelation delay in samples (1-512). Increase for "
                             "narrowband carriers; decrease for broadband hiss. Default: 16.",
                             &delay, 1, 512)) {
            if (!g->updating) {
                g->anr_delay = delay;
                changed = true; param_id = _GUI_PARAM_ANR_DELAY; new_val = delay;
            }
        }

        /* Gain uses a log slider: display as 1e-6..0.01, store internally
         * as log10 then convert back so small values are reachable. */
        float gain_log = (g->anr_gain > 0.0)
            ? (float)(log10(g->anr_gain) / log10(0.01) * -1.0 + 1.0) /* [0,1] mapped */
            : 0.0f;
        /* Simpler: just show a linear slider with scientific notation. */
        float gain_f = (float)g->anr_gain;
        if (param_slider_float("Gain", "LMS step size / two_mu (1e-6 to 0.01). Higher = faster "
                               "adaptation but risk of instability. Default: 0.0001.",
                               &gain_f, 1e-6f, 0.01f, "%.2e")) {
            if (!g->updating) {
                g->anr_gain = (double)gain_f;
                changed = true; param_id = _GUI_PARAM_ANR_GAIN; new_val = gain_f;
            }
        }

        float leak_f = (float)g->anr_leakage;
        if (param_slider_float("Leakage", "Weight decay per sample (0.0-1.0). Prevents coefficient "
                               "blow-up on stationary noise. Default: 0.1.",
                               &leak_f, 0.0f, 1.0f, "%.3f")) {
            if (!g->updating) {
                g->anr_leakage = (double)leak_f;
                changed = true; param_id = _GUI_PARAM_ANR_LEAKAGE; new_val = leak_f;
            }
        }

        ImGui::EndTable();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_MUTED);
        ImGui::TextUnformatted("Taps 16-2048  |  Delay 1-512  |  Gain 1e-6 to 0.01  |  Leakage 0.0-1.0");
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /* -- NR2 (EMNR) ----------------------------------------------------- */
    if (g->nr_mode == 2) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SURFACE);
        ImGui::BeginChild("##nr2grp", {-1, 0}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("NR2 - Spectral MMSE (EMNR)");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        /* Gain Method combo */
        ImGui::BeginTable("##nr2", 2, ImGuiTableFlags_None, {-1, 0});
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("ctrl",  ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Gain Method");
        show_tooltip_text("Spectral gain function. MM-LSA (recommended) gives least "
                          "musical noise. RROE and MEPSE are alternatives.");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        const char *gm[] = { "RROE", "MEPSE", "MM-LSA" };
        int gmethod = g->emnr_gain_method;
        if (ImGui::Combo("##gm", &gmethod, gm, 3) && !g->updating) {
            g->emnr_gain_method = gmethod;
            changed = true; param_id = _GUI_PARAM_EMNR_GAIN_METHOD; new_val = gmethod;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("NPE Method");
        show_tooltip_text("Noise power estimation algorithm. OSMS tracks the noise "
                          "floor more quickly; MMSE is smoother.");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        const char *nm[] = { "OSMS", "MMSE" };
        int npe = g->emnr_npe_method;
        if (ImGui::Combo("##nm", &npe, nm, 2) && !g->updating) {
            g->emnr_npe_method = npe;
            changed = true; param_id = _GUI_PARAM_EMNR_NPE_METHOD; new_val = npe;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Audio Enhance");
        show_tooltip_text("Post-filter gain mask that sharpens transients and "
                          "improves intelligibility. Recommended: On.");
        ImGui::TableSetColumnIndex(1);
        bool ae = (g->emnr_ae_run != 0);
        if (ImGui::Checkbox("##ae", &ae) && !g->updating) {
            g->emnr_ae_run = ae ? 1 : 0;
            changed = true; param_id = _GUI_PARAM_EMNR_AE_RUN; new_val = g->emnr_ae_run;
        }

        ImGui::EndTable();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /* -- NR3 (RNNR) ----------------------------------------------------- */
    if (g->nr_mode == 3) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SURFACE);
        ImGui::BeginChild("##nr3grp", {-1, 0}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("NR3 - RNNoise Neural Net (RNNR)");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        /* Model radio buttons */
        const char *model_labels[] = { "Standard (built-in)", "Small", "Large" };
        const char *model_tips[] = {
            "Uses the default RNNoise model built into the DLL. Good general-purpose "
            "suppression. No external files needed.",
            "Loads rnnoise-small.bin from the install folder. Lower CPU cost.",
            "Loads rnnoise-large.bin from the install folder. Strongest suppression, higher CPU."
        };
        ImGui::TextUnformatted("Model:");
        ImGui::SameLine();
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::RadioButton(model_labels[i], g->nr3_model == i) && !g->updating) {
                g->nr3_model = i;
                changed = true; param_id = _GUI_PARAM_NR3_MODEL; new_val = i;
            }
            show_tooltip_text(model_tips[i]);
            ImGui::PopID();
        }

        ImGui::Spacing();

        begin_param_table("##nr3p");
        float str = g->nr3_strength * 100.0f;
        if (param_slider_float("Suppression",
                "Blends denoised output with the original signal. "
                "100% = full RNNoise suppression. 0% = bypass. "
                "Reduce if the denoiser removes too much signal. Default: 100%.",
                &str, 0.0f, 100.0f, "%.0f%%")) {
            if (!g->updating) {
                g->nr3_strength = str / 100.0f;
                changed = true; param_id = _GUI_PARAM_NR3_STRENGTH;
                new_val = (double)g->nr3_strength;
            }
        }
        ImGui::EndTable();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /* -- NR4 (SBNR) ----------------------------------------------------- */
    if (g->nr_mode == 4) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SURFACE);
        ImGui::BeginChild("##nr4grp", {-1, 0}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("NR4 - Adaptive Spectral Denoiser (SBNR)");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        begin_param_table("##nr4");
        float reduc = g->nr4_reduction;
        if (param_slider_float("Reduction",
                "Noise attenuation (0-20 dB). Higher values suppress more noise "
                "but may affect speech clarity. Default: 10 dB.",
                &reduc, 0.0f, 20.0f, "%.1f dB")) {
            if (!g->updating) {
                g->nr4_reduction = reduc;
                changed = true; param_id = _GUI_PARAM_NR4_REDUCTION; new_val = reduc;
            }
        }
        ImGui::EndTable();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /* -- NR0 (nr0) - Spectral Tone Notcher ----------------------------- */
    if (g->nr_mode == 5) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SURFACE);
        ImGui::BeginChild("##nr0grp", {-1, 0}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted("NR0 - Spectral Tone Notcher");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_MUTED);
        ImGui::TextWrapped("Detects and eliminates narrowband tone interference: constant carriers, "
                           "heterodynes, tuning tones, and sweeping whistles. "
                           "Broadband noise and speech are unaffected.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        /* Preset buttons: shortcuts that set aggressiveness and detection threshold */
        ImGui::TextUnformatted("Target:"); ImGui::SameLine();
        struct PresetDef { const char *label; float aggr; int max_notch; float thresh; const char *tip; };
        static const PresetDef presets[] = {
            { "Carriers / Heterodynes (default)", 2.0f, 2, 25.0f,
              "Fixed-frequency carriers and heterodynes. Maximum 30-frame hold prevents "
              "re-triggering on brief signal breaks. 20 dB threshold targets only the "
              "strongest narrowband spikes. Max notches 2 avoids false triggers." },
            { "General / Mixed", 20.0f, 4, 20.0f,
              "Mixed interference including carriers and moderate-speed tones. "
              "Balanced 10 dB threshold and 15-frame release." },
            { "Whistles", 80.0f, 5, 18.0f,
              "Fast-moving interference sweeping across the band (deliberate whistling, "
              "hand-key artefacts). Instant release tracks rapid movement. "
              "8 dB threshold catches weaker tones." },
        };
        for (int pi = 0; pi < 3; ++pi) {
            if (pi > 0) ImGui::SameLine();
            bool active = (fabsf(g->nr0_aggression - presets[pi].aggr) < 1.0f &&
                           g->nr0_max_notches == presets[pi].max_notch &&
                           fabsf(g->nr0_threshold  - presets[pi].thresh) < 0.5f);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT);
                ImGui::PushStyleColor(ImGuiCol_Text,          {0.00f, 0.00f, 0.00f, 1.00f});
            }
            ImGui::PushID(pi + 200);
            if (ImGui::Button(presets[pi].label) && !g->updating) {
                g->nr0_aggression  = presets[pi].aggr;
                g->nr0_max_notches = presets[pi].max_notch;
                g->nr0_threshold   = presets[pi].thresh;
                /* Fire all three param changes directly -- no ImGui re-entrancy risk here */
                if (g->on_param_change) {
                    g->on_param_change(g->plugin, _GUI_PARAM_NR0_AGGRESSION,  presets[pi].aggr);
                    g->on_param_change(g->plugin, _GUI_PARAM_NR0_MAX_NOTCHES, presets[pi].max_notch);
                    g->on_param_change(g->plugin, _GUI_PARAM_NR0_THRESHOLD,   presets[pi].thresh);
                }
            }
            ImGui::PopID();
            if (active) ImGui::PopStyleColor(3);
            show_tooltip_text(presets[pi].tip);
        }
        ImGui::Spacing();

        begin_param_table("##nr0p");
        float aggr = g->nr0_aggression;
        if (param_slider_float("Aggressiveness",
                "0 = most conservative (fixed carriers, slow release). "
                "100 = most aggressive (fast whistles, instant release). "
                "Use the preset buttons above as starting points.",
                &aggr, 0.0f, 100.0f, "%.0f")) {
            if (!g->updating) {
                g->nr0_aggression = aggr;
                changed = true; param_id = _GUI_PARAM_NR0_AGGRESSION; new_val = aggr;
            }
        }
        int max_notch = g->nr0_max_notches;
        if (param_slider_int("Max Notches",
                "Maximum simultaneous notch bins (1-10). Reduce if speech "
                "is accidentally notched. Default: 5.",
                &max_notch, 1, 10)) {
            if (!g->updating) {
                g->nr0_max_notches = max_notch;
                changed = true; param_id = _GUI_PARAM_NR0_MAX_NOTCHES; new_val = max_notch;
            }
        }
        float thresh = g->nr0_threshold;
        if (param_slider_float("Threshold",
                "Detection threshold: how far a spectral peak must exceed the local noise "
                "floor before it is notched (3-20 dB). Lower = detects weaker or obscured "
                "tones. Higher = only notches very prominent narrowband spikes. Default: 10 dB.",
                &thresh, 3.0f, 40.0f, "%.1f dB")) {
            if (!g->updating) {
                g->nr0_threshold = thresh;
                changed = true; param_id = _GUI_PARAM_NR0_THRESHOLD; new_val = thresh;
            }
        }
        ImGui::EndTable();

        /* Status line and active notch counter */
        {
            float a      = g->nr0_aggression / 100.0f;
            int   hold_fr = (int)(30.0f * (1.0f - a) + 0.5f);
            int   active  = g->nr0_active_notches;
            int   maxn    = g->nr0_max_notches;
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_MUTED);
            ImGui::Text("Release: %d frames  |  Threshold: %.1f dB above floor",
                        hold_fr, g->nr0_threshold);
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, active > 0 ? COL_ACCENT : COL_MUTED);
            ImGui::Text("Active notches: %d / %d", active, maxn);
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /* About popup modal - avoids the re-entrancy hazard of calling MessageBoxA
     * from inside the render loop (its own message pump fires WM_TIMER). */
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("About##popup", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        /* Compact item spacing for the whole popup */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 3.0f});

        ImGui::Text("CLAP NR  v" CLAP_NR_VERSION_STR);

        ImGui::PushStyleColor(ImGuiCol_Text, COL_MUTED);
        ImGui::TextUnformatted("Algorithms: Stuart Green G5STU / Warren Pratt NR0V / Richard Samphire MW0LGE");
        ImGui::PopStyleColor();

        ImGui::Separator();

        /* Library table: name | licence */
        ImGui::BeginTable("##about_libs", 2, ImGuiTableFlags_None, {340.0f, 0});
        ImGui::TableSetupColumn("lib", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("lic", ImGuiTableColumnFlags_WidthFixed, 82.0f);

        auto lib_row = [](const char *name, const char *lic) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.47f, 0.55f, 0.60f, 1.0f));
            ImGui::TextUnformatted(lic);
            ImGui::PopStyleColor();
        };

        lib_row("Dear ImGui " IMGUI_VERSION, "MIT");
        lib_row("FFTW3",                     "GPL v2");
        lib_row("RNNoise",                   "BSD 3-clause");
        lib_row("libspecbleach",             "GPL v2");
        lib_row("CLAP SDK",                  "MIT");

        ImGui::EndTable();

        ImGui::Separator();

        ImGui::PopStyleVar(); /* ItemSpacing */

        if (ImGui::Button("Visit clapnr.com", {160, 0})) {
            g->open_website = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", {80, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    /* Capture content height so window cannot be dragged shorter than
     * whatever controls the current mode displays. */
    g->min_h = (int)ceilf(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);

    ImGui::End(); /* ##root */

    /* Commit changed parameter after the frame is built so we are outside
     * any ImGui widget processing — avoids re-entrancy issues. */
    if (changed && g->on_param_change)
        g->on_param_change(g->plugin, (clap_id)param_id, new_val);

    /* Render */
    ImGui::Render();
#ifdef _WIN32
    float clear[4] = { COL_BG.x, COL_BG.y, COL_BG.z, 1.0f };
    g->d3d_ctx->OMSetRenderTargets(1, &g->rtv, nullptr);
    g->d3d_ctx->ClearRenderTargetView(g->rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g->swap_chain->Present(1, 0);

    /* Deferred actions that must not run inside the ImGui frame/message loop */
    if (g->open_website) {
        g->open_website = false;
        ShellExecuteA(nullptr, "open", "https://clapnr.com",
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    glClearColor(COL_BG.x, COL_BG.y, COL_BG.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#  ifdef __APPLE__
    /* Flush the double buffer.  The context is already current on the main
     * thread (set by ClapNRRenderer before calling render_frame). */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGLFlushDrawable((CGLContextObj)[[g->gl_view openGLContext] CGLContextObj]);
#pragma clang diagnostic pop
    if (g->open_website) {
        g->open_website = false;
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSWorkspace sharedWorkspace]
                openURL:[NSURL URLWithString:@"https://clapnr.com"]];
        });
    }
#  else
    glfwSwapBuffers(g->glfw_win);
    if (g->open_website) {
        g->open_website = false;
        system("xdg-open https://clapnr.com");
    }
#  endif
#endif
}

/* -----------------------------------------------------------------------
 * Win32 cleanup helper  (Windows only)
 *
 * Idempotent: safe to call from WM_DESTROY (fired when the host destroys
 * its parent window before calling CLAP gui.destroy()) OR from gui_destroy
 * (normal CLAP lifecycle).  Guards on d3d_device ensure the D3D11/ImGui
 * teardown only runs once even if called from both paths.
 * NOTE: Does NOT call DestroyWindow - callers handle that themselves.
 * --------------------------------------------------------------------- */
#ifdef _WIN32
static void gui_win32_cleanup(clap_nr_gui_s *g)
{
    if (!g) return;
    if (g->timer_id) {
        if (g->hwnd) KillTimer(g->hwnd, g->timer_id);
        g->timer_id = 0;
    }
    /* d3d_device is non-null iff D3D11 + ImGui were fully initialised */
    if (g->d3d_device) {
        ImGui::SetCurrentContext(g->imgui_ctx);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g->imgui_ctx);
        g->imgui_ctx = nullptr;
        d3d_destroy(g);   /* releases all COM refs and sets pointers to nullptr */
    }
}
#endif

/* -----------------------------------------------------------------------
 * Window procedure + class registration  (Windows only)
 * --------------------------------------------------------------------- */
#ifdef _WIN32
#define WC_IMGUI_NR  "ClapNrImGui_v1"

static LRESULT CALLBACK imgui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    clap_nr_gui_s *g = (clap_nr_gui_s *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    /* Activate this instance's ImGui context before anything touches GImGui.
     * This is required for multi-instance safety: each plugin instance owns
     * a separate ImGuiContext.  We guard on imgui_ctx != nullptr so that
     * messages that arrive before WM_CREATE (none, in practice) or after
     * ImGui::DestroyContext() skip the call safely. */
    if (g && g->imgui_ctx)
        ImGui::SetCurrentContext(g->imgui_ctx);

    /* Guard: ImGui_ImplWin32_WndProcHandler dereferences the ImGui context
     * (calls ImGui::GetIO() which reads GImGui).  If gui_win32_cleanup() has
     * already called ImGui::DestroyContext(), GImGui is null and calling
     * ImGui_ImplWin32_WndProcHandler would immediately crash.  This happens
     * in the normal teardown path: gui_win32_cleanup() destroys the context,
     * then gui_destroy() calls DestroyWindow(), which synchronously fires
     * WM_DESTROY (and WM_NCDESTROY, WM_SIZE, WM_NCPAINT, ...) back through
     * this WndProc.  Skipping the handler when the context is gone is safe
     * because ImGui no longer needs to process input at that point. */
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 1;

    switch (msg) {
    case WM_CREATE: {
        auto *cs = (CREATESTRUCTA *)lp;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_SIZE:
        if (g && g->swap_chain) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != g->sc_w || h != g->sc_h))
                d3d_resize(g, w, h);
        }
        return 0;
    case WM_TIMER:
        if (g && g->d3d_device) render_frame(g);
        return 0;
    case WM_PAINT:
        if (g && g->d3d_device) render_frame(g);
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_DESTROY:
        /* Full cleanup here handles the case where the host destroys its
         * parent window before calling CLAP plugin.gui.destroy().  Without
         * this, the D3D11 swap chain would remain alive pointing at a dead
         * HWND, and any subsequent Present() call (e.g. from a queued
         * WM_TIMER) would crash inside DXGI. */
        if (g) {
            gui_win32_cleanup(g);
            g->hwnd = nullptr;
        }
        return 0;
    case WM_NCDESTROY:
        /* Last message delivered to this window.  Clear GWLP_USERDATA so
         * any stray message that arrives after WM_NCDESTROY (e.g. from a
         * hooked message pump in the host) cannot dereference a freed 'g'
         * pointer.  SetWindowLongPtrA is safe here because the window still
         * exists as an object even though it is no longer visible. */
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_SETICON:
    case WM_SETTEXT:
        /* Allow the host to override our icon/title at any time. */
        break;
    case WM_GETMINMAXINFO:
        /* Prevent the floating window from being dragged narrower/shorter
         * than the measured header row and current-mode content height. */
        if (g && g->min_w > 0 && !g->embedded) {
            DWORD wstyle   = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
            DWORD wexstyle = (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE);
            RECT  r = { 0, 0, (LONG)g->min_w, (LONG)g->min_h };
            AdjustWindowRectEx(&r, wstyle, FALSE, wexstyle);
            auto *mmi = (MINMAXINFO *)lp;
            mmi->ptMinTrackSize.x = r.right  - r.left;
            mmi->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }
        break;
    case WM_WINDOWPOSCHANGING:
        /* For embedded (child) windows the host drives sizing, so clamp via
         * WM_WINDOWPOSCHANGING rather than WM_GETMINMAXINFO. */
        if (g && g->min_w > 0 && g->embedded) {
            auto *wpos = (WINDOWPOS *)lp;
            if (!(wpos->flags & SWP_NOSIZE)) {
                if (wpos->cx < g->min_w) wpos->cx = g->min_w;
                if (wpos->cy < g->min_h) wpos->cy = g->min_h;
            }
            return 0;
        }
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void register_class_imgui()
{
    WNDCLASSEXA wc = {};
    if (GetClassInfoExA(GetModuleHandleA(nullptr), WC_IMGUI_NR, &wc)) return;
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = imgui_wndproc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WC_IMGUI_NR;
    /* Inherit the host application's icon so the taskbar shows something
     * sensible before the host pushes its own icon via WM_SETICON. */
    wc.hIcon   = (HICON)GetClassLongPtrA(GetForegroundWindow(), GCLP_HICON);
    wc.hIconSm = (HICON)GetClassLongPtrA(GetForegroundWindow(), GCLP_HICONSM);
    RegisterClassExA(&wc);
}

/* -----------------------------------------------------------------------
 * Create the Win32 window + ImGui context
 * --------------------------------------------------------------------- */
static bool create_window_and_imgui(clap_nr_gui_s *g, HWND parent,
                                     int x, int y, int w, int h,
                                     DWORD style, DWORD exstyle)
{
    register_class_imgui();

    g->hwnd = CreateWindowExA(exstyle, WC_IMGUI_NR,
                               g->embedded ? nullptr : g->window_title,
                               
                               style, x, y, w, h,
                               parent, nullptr, GetModuleHandleA(nullptr), g);
    if (!g->hwnd) return false;

    /* Request dark non-client frame (title bar + borders) on Windows 10 20H1+
     * and Windows 11. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    {
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(g->hwnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
            DwmSetWindowAttribute(g->hwnd, 19, &dark, sizeof(dark));
    }

    if (!d3d_create(g, g->hwnd)) {
        DestroyWindow(g->hwnd); g->hwnd = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    g->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_theme();

    ImGui_ImplWin32_Init(g->hwnd);
    ImGui_ImplDX11_Init(g->d3d_device, g->d3d_ctx);

    g->timer_id = SetTimer(g->hwnd, 1, 16, nullptr);
    return true;
}

#elif defined(__APPLE__)

/* -----------------------------------------------------------------------
 * macOS: NSWindow + NSOpenGLView, rendered on the MAIN THREAD via NSTimer.
 *
 * Root cause of all previous crashes: Apple's OpenGL-over-Metal driver
 * uses a process-global Metal device.  Calling CGLSetCurrentContext on ANY
 * background thread corrupts the GL state used by Qt6's own OpenGL widgets
 * (SpectrumWaterfallWidget etc.) running on the main thread → malloc
 * corruption → crash in the host's paint engine.
 *
 * Fix: all GL calls happen on the main thread, driven by an NSTimer at
 * ~60 fps — identical to how the Windows backend uses WM_TIMER.
 * Multiple plugin instances each have their own NSOpenGLContext and their
 * own NSTimer; zero shared state between instances.
 * --------------------------------------------------------------------- */

/* ---- Per-instance render-target helper object ---------------------- */
@interface ClapNRRenderer : NSObject
@property (assign) clap_nr_gui_s *gui;
- (void)timerFired:(NSTimer *)timer;
@end
@implementation ClapNRRenderer
- (void)timerFired:(NSTimer * __unused)timer
{
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->visible || !g->imgui_ctx) return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [[g->gl_view openGLContext] makeCurrentContext];
#pragma clang diagnostic pop
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    render_frame(g);   /* calls CGLFlushDrawable */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSOpenGLContext clearCurrentContext];
#pragma clang diagnostic pop
}
@end

/* ---- Window delegate ----------------------------------------------- */
@interface ClapNRWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) clap_nr_gui_s *gui;
@end
@implementation ClapNRWindowDelegate
- (BOOL)windowShouldBecomeKey:(NSWindow *)__unused sender  { return NO; }
- (BOOL)windowShouldBecomeMain:(NSWindow *)__unused sender { return NO; }
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    [sender orderOut:nil];
    clap_nr_gui_s *g = self.gui;
    if (g) {
        g->visible = false;
        if (g->on_close) g->on_close(g->plugin);
    }
    return NO;
}
@end

/* ---- GL view (input forwarding only — no rendering) ---------------- */
@interface ClapNRGLView : NSOpenGLView
@property (assign) clap_nr_gui_s *gui;
@end
@implementation ClapNRGLView

- (BOOL)acceptsFirstResponder                   { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)__unused e { return NO;  }

- (void)reshape
{
    [super reshape];
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->imgui_ctx) return;
    NSRect r = [self convertRectToBacking:[self bounds]];
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGui::GetIO().DisplaySize = ImVec2((float)r.size.width, (float)r.size.height);
}

- (void)forwardMouseEvent:(NSEvent *)event button:(int)btn down:(BOOL)down
{
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->imgui_ctx) return;
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    NSRect r  = [self convertRectToBacking:[self bounds]];
    float scale = (float)(r.size.height / self.bounds.size.height);
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)p.x * scale,
                        (float)(self.bounds.size.height - p.y) * scale);
    io.AddMouseButtonEvent(btn, down ? true : false);
}

- (void)mouseDown:(NSEvent *)e         { [self forwardMouseEvent:e button:ImGuiMouseButton_Left  down:YES]; }
- (void)mouseUp:(NSEvent *)e           { [self forwardMouseEvent:e button:ImGuiMouseButton_Left  down:NO];  }
- (void)rightMouseDown:(NSEvent *)e    { [self forwardMouseEvent:e button:ImGuiMouseButton_Right down:YES]; }
- (void)rightMouseUp:(NSEvent *)e      { [self forwardMouseEvent:e button:ImGuiMouseButton_Right down:NO];  }
- (void)mouseMoved:(NSEvent *)e        { [self forwardMouseEvent:e button:-1                     down:NO];  }
- (void)mouseDragged:(NSEvent *)e      { [self forwardMouseEvent:e button:ImGuiMouseButton_Left  down:YES]; }
- (void)rightMouseDragged:(NSEvent *)e { [self forwardMouseEvent:e button:ImGuiMouseButton_Right down:YES]; }

- (void)scrollWheel:(NSEvent *)event
{
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->imgui_ctx) return;
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGui::GetIO().AddMouseWheelEvent((float)[event scrollingDeltaX],
                                      (float)[event scrollingDeltaY]);
}

- (void)keyDown:(NSEvent *)event
{
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->imgui_ctx) return;
    ImGui::SetCurrentContext(g->imgui_ctx);
    NSString *chars = [event characters];
    if (chars.length > 0)
        ImGui::GetIO().AddInputCharactersUTF8([chars UTF8String]);
}

@end

/* ---- mac_create_window / mac_destroy_window ------------------------ */

static bool mac_create_window(clap_nr_gui_s *g)
{
    /* Must run on the main thread. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

    NSOpenGLPixelFormatAttribute attrs41[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
        NSOpenGLPFAColorSize,  24,
        NSOpenGLPFAAlphaSize,   8,
        NSOpenGLPFADepthSize,  24,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat *fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs41];
    if (!fmt) {
        NSOpenGLPixelFormatAttribute attrs32[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
            NSOpenGLPFAColorSize,  24,
            NSOpenGLPFAAlphaSize,   8,
            NSOpenGLPFADepthSize,  24,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            0
        };
        fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs32];
    }
    if (!fmt) { NSLog(@"clap-nr: NSOpenGLPixelFormat alloc failed"); return false; }

    NSRect frame = NSMakeRect(200, 200, GUI_BASE_W, GUI_BASE_H);
    ClapNRGLView *view = [[ClapNRGLView alloc] initWithFrame:frame pixelFormat:fmt];
    if (!view) { NSLog(@"clap-nr: ClapNRGLView alloc failed"); return false; }
    view.gui = g;
    [view setWantsBestResolutionOpenGLSurface:YES];

    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                     | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSWindow *win = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    if (!win) { NSLog(@"clap-nr: NSWindow alloc failed"); return false; }
#pragma clang diagnostic pop

    ClapNRWindowDelegate *delegate = [[ClapNRWindowDelegate alloc] init];
    delegate.gui = g;
    [win setDelegate:delegate];
    [win setTitle:[NSString stringWithUTF8String:g->window_title]];
    [win setContentView:view];
    [win setAcceptsMouseMovedEvents:YES];
    [win setMinSize:NSMakeSize(GUI_BASE_W, GUI_BASE_H)];
    [win setLevel:NSFloatingWindowLevel];
    [win setHidesOnDeactivate:NO];
    [win setExcludedFromWindowsMenu:YES];
    [win setCollectionBehavior: NSWindowCollectionBehaviorCanJoinAllSpaces
                               | NSWindowCollectionBehaviorStationary
                               | NSWindowCollectionBehaviorIgnoresCycle
                               | NSWindowCollectionBehaviorFullScreenAuxiliary];
    [win setReleasedWhenClosed:NO];
    [win orderFront:nil];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSOpenGLContext *glctx = [view openGLContext];
    if (!glctx) {
        [win orderOut:nil];
        NSLog(@"clap-nr: openGLContext nil after orderFront");
        return false;
    }
    GLint swapInterval = 0;
    [glctx setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];

    /* All GL init on the main thread — same thread as all future rendering. */
    [glctx makeCurrentContext];
#pragma clang diagnostic pop

    IMGUI_CHECKVERSION();
    g->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    NSRect backing = [view convertRectToBacking:[view bounds]];
    io.DisplaySize = ImVec2((float)backing.size.width, (float)backing.size.height);
    apply_theme();
    ImGui_ImplOpenGL3_Init("#version 410");

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSOpenGLContext clearCurrentContext];
#pragma clang diagnostic pop

    g->gl_view  = view;
    g->window   = win;
    g->visible  = true;

    /* Start 60 fps timer on the main run loop.
     * NSRunLoopCommonModes ensures the timer fires even while the host
     * is tracking mouse events in its own controls. */
    ClapNRRenderer *renderer = [[ClapNRRenderer alloc] init];
    renderer.gui = g;
    g->render_timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                       target:renderer
                                                     selector:@selector(timerFired:)
                                                     userInfo:nil
                                                      repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:g->render_timer forMode:NSRunLoopCommonModes];

    return true;
}

static void mac_destroy_window(clap_nr_gui_s *g)
{
    /* Must run on the main thread. */

    /* Stop the timer before any GL teardown so no more renders fire. */
    if (g->render_timer) {
        [g->render_timer invalidate];
        g->render_timer = nullptr;
    }

    /* GL shutdown on the main thread — same thread as all rendering. */
    if (g->imgui_ctx) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [[g->gl_view openGLContext] makeCurrentContext];
#pragma clang diagnostic pop
        ImGui::SetCurrentContext(g->imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(g->imgui_ctx);
        g->imgui_ctx = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSOpenGLContext clearCurrentContext];
#pragma clang diagnostic pop
    }

    g->gl_view = nullptr;

    if (g->window) {
        [g->window orderOut:nil];
        g->window = nullptr;
    }
}

#else /* Linux */

/* -----------------------------------------------------------------------
 * Linux: GLFW + OpenGL 3.3 render thread
 * --------------------------------------------------------------------- */
static void *glfw_render_thread(void *arg)
{
    clap_nr_gui_s *g = (clap_nr_gui_s *)arg;


    if (!glfwInit()) {
        g->thread_ready = true;
        return nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE,   GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    g->glfw_win = glfwCreateWindow(GUI_BASE_W, GUI_BASE_H,
                                    g->window_title, nullptr, nullptr);
    if (!g->glfw_win) {
        glfwTerminate();
        g->thread_ready = true;
        return nullptr;
    }

    glfwSetWindowUserPointer(g->glfw_win, g);
    glfwSetWindowSizeLimits(g->glfw_win,
                             GUI_BASE_W, GUI_BASE_H,
                             GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwMakeContextCurrent(g->glfw_win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    g->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_theme();

    ImGui_ImplGlfw_InitForOpenGL(g->glfw_win, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    g->thread_ready = true;

    while (g->running && !glfwWindowShouldClose(g->glfw_win)) {
        glfwPollEvents();
        if (g->visible)
            render_frame(g);
        else {
            struct timespec ts = { 0, 16000000L };
            nanosleep(&ts, nullptr);
        }
    }

    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(g->imgui_ctx);
    g->imgui_ctx = nullptr;
    glfwDestroyWindow(g->glfw_win);
    g->glfw_win = nullptr;
    glfwTerminate();
    return nullptr;
}

static bool wait_for_thread_ready(clap_nr_gui_s *g)
{
    for (int i = 0; i < 2000 && !g->thread_ready; ++i) {
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, nullptr);
    }
    return g->glfw_win != nullptr && g->imgui_ctx != nullptr;
}

#endif /* platform backend */

/* -----------------------------------------------------------------------
 * Public API that matches gui.h
 * --------------------------------------------------------------------- */

clap_nr_gui_t *gui_create(void *plugin, gui_param_cb_t on_param_change,
                          gui_tooltips_cb_t on_tooltips_change,
                          gui_close_cb_t on_close,
                          const char *title)
{
    auto *g = (clap_nr_gui_s *)calloc(1, sizeof(clap_nr_gui_s));
    if (!g) return nullptr;
    g->plugin              = plugin;
    g->on_param_change     = on_param_change;
    g->on_tooltips_change  = on_tooltips_change;
    g->on_close            = on_close;
    strncpy(g->window_title, (title && title[0]) ? title : "CLAP NR", 255);
    g->window_title[255] = '\0';
    /* Defaults matching factory_create_plugin in clap_nr.c */
    g->nr_mode          = 0;
    g->anr_taps         = 64;
    g->anr_delay        = 16;
    g->anr_gain         = 0.0001;
    g->anr_leakage      = 0.1;
    g->emnr_gain_method = 2;   /* MM-LSA */
    g->emnr_npe_method  = 0;   /* OSMS   */
    g->emnr_ae_run      = 1;
    g->nr4_reduction    = 10.0f;
    g->nr3_model        = 0;
    g->nr3_strength     = 0.5f;
    g->nr0_aggression   = 50.0f;
    g->nr0_max_notches  = 5;
    g->nr0_threshold    = 10.0f;
    g->tooltips_on      = true;
    g->min_w            = GUI_BASE_W;
    g->min_h            = GUI_BASE_H;
    return g;
}

void gui_destroy(clap_nr_gui_t *gui)
{
    if (!gui) return;
#ifdef _WIN32
    {
        HWND h = gui->hwnd;
        gui_win32_cleanup(gui);
        if (h) {
            DestroyWindow(h);
            gui->hwnd = nullptr;
        }
    }
#else
#  ifdef __APPLE__
    if ([NSThread isMainThread]) {
        mac_destroy_window(gui);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{ mac_destroy_window(gui); });
    }
#  else /* Linux */
    if (gui->render_thread) {
        gui->running = false;
        gui->visible = false;
        pthread_join(gui->render_thread, nullptr);
        gui->render_thread = 0;
    }
#  endif
#endif
    free(gui);
}

bool gui_set_transient(clap_nr_gui_t *gui, const clap_window_t *window)
{
    if (!gui || !window) return false;
#ifdef _WIN32
    /* Store the host's top-level HWND so gui_show() can pass it as the
     * owner window when creating the floating window via CreateWindowExA.
     * The owner relationship is established at window creation time only --
     * dynamically changing the owner of an existing top-level window via
     * SetWindowLongPtr(GWLP_HWNDPARENT) is not well-defined (per Raymond
     * Chen / MSDN) and can cause synchronous message dispatching that
     * re-enters host code while we are inside this callback, leading to
     * crashes.  If the window already exists (non-standard host call order),
     * just update the stored HWND; the change takes effect on the next
     * gui_show() → CreateWindowExA call. */
    gui->owner_hwnd = (HWND)window->win32;
    return true;
#else
    (void)window;
    return false;
#endif
}

void gui_suggest_title(clap_nr_gui_t *gui, const char *title)
{
    if (!gui || !title || !title[0]) return;
    /* The host may call this at any time to set a more descriptive title
     * (e.g. "StationMasterPro 2.1 | CLAP NR" including version numbers).
     * We update our stored title and, if the window already exists,
     * push the new text straight to the title bar. */
    snprintf(gui->window_title, sizeof(gui->window_title), "%s", title);
#ifdef _WIN32
    if (gui->hwnd)
        SetWindowTextA(gui->hwnd, gui->window_title);
#endif
}

bool gui_set_parent(clap_nr_gui_t *gui, const clap_window_t *window)
{
    if (!gui || !window) return false;
#ifdef _WIN32
    gui->embedded = true;
    HWND parent = (HWND)window->win32;
    RECT rc; GetClientRect(parent, &rc);
    int w = rc.right  - rc.left;
    int h = rc.bottom - rc.top;
    if (w < GUI_BASE_W) w = GUI_BASE_W;
    if (h < GUI_BASE_H) h = GUI_BASE_H;
    return create_window_and_imgui(gui, parent,
                                   0, 0, w, h,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0);
#else
    /* GLFW does not support parenting into an arbitrary X11/Cocoa window.
     * Return false so the host falls back to floating mode. */
    (void)window;
    return false;
#endif
}

void gui_get_size(clap_nr_gui_t *gui, uint32_t *out_w, uint32_t *out_h)
{
    (void)gui;
    *out_w = GUI_BASE_W;
    *out_h = GUI_BASE_H;
}

bool gui_show(clap_nr_gui_t *gui)
{
    if (!gui) return false;
#ifdef _WIN32
    if (!gui->hwnd) {
        /* Floating window.
         * Pass owner_hwnd (set by gui_set_transient) as the hWndParent
         * argument to CreateWindowEx.  For a WS_OVERLAPPED top-level window
         * Win32 treats a non-null hWndParent as the OWNER, not the parent:
         *   - The floating window always renders above the owner.
         *   - It minimises and restores together with the owner.
         *   - It does NOT get its own independent taskbar button.
         * WS_EX_APPWINDOW is intentionally omitted: that flag overrides the
         * owner relationship and forces an independent taskbar entry. */
        gui->embedded = false;
        POINT pt = { 200, 200 };
        GetCursorPos(&pt);
        DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
        DWORD exstyle = WS_EX_TOPMOST;
        RECT  r       = { 0, 0, GUI_BASE_W, GUI_BASE_H };
        AdjustWindowRectEx(&r, style, FALSE, exstyle);
        int ww = r.right - r.left, wh = r.bottom - r.top;
        int wx = pt.x - ww / 4, wy = pt.y + 16;

        HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoA(hmon, &mi)) {
            if (wx + ww > mi.rcWork.right)  wx = mi.rcWork.right  - ww;
            if (wy + wh > mi.rcWork.bottom) wy = mi.rcWork.bottom - wh;
            if (wx < mi.rcWork.left)        wx = mi.rcWork.left;
            if (wy < mi.rcWork.top)         wy = mi.rcWork.top;
        }
        if (!create_window_and_imgui(gui, gui->owner_hwnd, wx, wy, ww, wh, style, exstyle))
            return false;
    }

    if (IsIconic(gui->hwnd)) ShowWindow(gui->hwnd, SW_RESTORE);
    else                     ShowWindow(gui->hwnd, SW_SHOW);

    if (!gui->embedded) {
        SetWindowPos(gui->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(gui->hwnd);
    }
    return true;

#else  /* Linux / macOS */
#  ifdef __APPLE__
    /* macOS: create NSPanel + pthread render thread on the main thread.
     * CLAP hosts call gui_show() from the main thread, so dispatch_sync
     * to the main queue would deadlock.  Detect main thread and call
     * directly; fall back to dispatch_sync only if on another thread. */
    if (!gui->window) {
        bool ok = false;
        if ([NSThread isMainThread]) {
            ok = mac_create_window(gui);
        } else {
            __block bool _ok = false;
            dispatch_sync(dispatch_get_main_queue(), ^{ _ok = mac_create_window(gui); });
            ok = _ok;
        }
        if (!ok) return false;
        /* mac_create_window already shows the window and sets visible = true */
        return true;
    }
    /* Window already exists — just ensure it's on screen if not already visible. */
    gui->visible = true;
    NSWindow *win = gui->window;
    if ([NSThread isMainThread]) {
        if (![win isVisible]) [win orderFront:nil];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (![win isVisible]) [win orderFront:nil];
        });
    }
    return true;

#  else  /* Linux */
    if (!gui->glfw_win) {
        gui->running      = true;
        gui->visible      = false;
        gui->thread_ready = false;
        if (pthread_create(&gui->render_thread, nullptr, glfw_render_thread, gui) != 0)
            return false;
        if (!wait_for_thread_ready(gui))
            return false;
    }
    gui->visible = true;
    glfwShowWindow(gui->glfw_win);
    glfwFocusWindow(gui->glfw_win);
    return true;
#  endif /* __APPLE__ */
#endif
}

bool gui_hide(clap_nr_gui_t *gui)
{
    if (!gui) return false;
#ifdef _WIN32
    if (!gui->hwnd) return false;
    ShowWindow(gui->hwnd, SW_HIDE);
#else
    gui->visible = false;
#  ifdef __APPLE__
    if (gui->window) {
        NSWindow *win = gui->window;
        if ([NSThread isMainThread]) {
            [win orderOut:nil];
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{ [win orderOut:nil]; });
        }
    }
#  else
    if (gui->glfw_win) glfwHideWindow(gui->glfw_win);
#  endif
#endif
    return true;
}

bool gui_resize(clap_nr_gui_t *gui, uint32_t w, uint32_t h)
{
    if (!gui) return false;
#ifdef _WIN32
    if (!gui->hwnd || !gui->embedded) return false;
    UINT nw = (w < GUI_BASE_W) ? GUI_BASE_W : w;
    UINT nh = (h < GUI_BASE_H) ? GUI_BASE_H : h;
    SetWindowPos(gui->hwnd, nullptr, 0, 0, (int)nw, (int)nh,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
#else
    int nw = (int)((w < GUI_BASE_W) ? GUI_BASE_W : w);
    int nh = (int)((h < GUI_BASE_H) ? GUI_BASE_H : h);
#  ifdef __APPLE__
    if (!gui->window) return false;
    NSWindow *win = gui->window;
    dispatch_async(dispatch_get_main_queue(), ^{
        NSRect frame = [win frame];
        frame.size.width  = nw;
        frame.size.height = nh;
        [win setFrame:frame display:YES];
    });
#  else
    if (!gui->glfw_win) return false;
    glfwSetWindowSize(gui->glfw_win, nw, nh);
#  endif
#endif
    return true;
}

void gui_set_param(clap_nr_gui_t *gui, clap_id param_id, double value)
{
    if (!gui) return;
    gui->updating = true;
    switch (param_id) {
    case _GUI_PARAM_NR_MODE:          gui->nr_mode          = (int)value;   break;
    case _GUI_PARAM_ANR_TAPS:         gui->anr_taps         = (int)value;   break;
    case _GUI_PARAM_ANR_DELAY:        gui->anr_delay        = (int)value;   break;
    case _GUI_PARAM_ANR_GAIN:         gui->anr_gain         = value;         break;
    case _GUI_PARAM_ANR_LEAKAGE:      gui->anr_leakage      = value;         break;
    case _GUI_PARAM_EMNR_GAIN_METHOD: gui->emnr_gain_method = (int)value;   break;
    case _GUI_PARAM_EMNR_NPE_METHOD:  gui->emnr_npe_method  = (int)value;   break;
    case _GUI_PARAM_EMNR_AE_RUN:      gui->emnr_ae_run      = (int)value;   break;
    case _GUI_PARAM_NR4_REDUCTION:    gui->nr4_reduction    = (float)value; break;
    case _GUI_PARAM_NR3_MODEL:        gui->nr3_model        = (int)value;   break;
    case _GUI_PARAM_NR3_STRENGTH:     gui->nr3_strength     = (float)value; break;
    case _GUI_PARAM_NR0_AGGRESSION:   gui->nr0_aggression   = (float)value; break;
    case _GUI_PARAM_NR0_MAX_NOTCHES:  gui->nr0_max_notches  = (int)value;   break;
    case _GUI_PARAM_NR0_THRESHOLD:    gui->nr0_threshold    = (float)value; break;
    }
    gui->updating = false;
    /* No explicit redraw needed; the 60 fps timer will catch the next frame */
}

void gui_set_tooltips(clap_nr_gui_t *gui, bool on)
{
    if (!gui) return;
    gui->tooltips_on = on;
}

void gui_set_nr0_active_notches(clap_nr_gui_t *gui, int count)
{
    if (!gui) return;
    gui->nr0_active_notches = count;
}

