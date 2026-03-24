/*
 * gui_mac.mm  –  macOS Cocoa/OpenGL GUI backend for clap-nr
 *
 * Uses a plain NSWindow + NSOpenGLView + CVDisplayLink.
 * No GLFW, no Qt conflict.  All Cocoa calls are dispatched to the
 * main thread via dispatch_async / dispatch_sync as required by AppKit.
 * Rendering (OpenGL draw calls) happens on the CVDisplayLink thread.
 *
 * Copyright (C) 2026 - Stuart E. Green (G5STU)
 * GPL v2 – see gui_imgui.cpp for full licence text.
 */

#import  <Cocoa/Cocoa.h>
#import  <OpenGL/gl3.h>
#import  <CoreVideo/CVDisplayLink.h>
#import  <dispatch/dispatch.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "gui.h"
#include "version.h"

/* -----------------------------------------------------------------------
 * Forward-declare the shared render_frame() from gui_imgui.cpp
 * The struct definition must match exactly.
 * --------------------------------------------------------------------- */
struct clap_nr_gui_s;
static void render_frame(clap_nr_gui_s *g);   /* defined in gui_imgui.cpp */

/* -----------------------------------------------------------------------
 * Param indices (must stay in sync with clap_nr.c and gui_imgui.cpp)
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

#define GUI_BASE_W  750
#define GUI_BASE_H  300

/* -----------------------------------------------------------------------
 * GUI struct  (macOS-only; mirrored from gui_imgui.cpp for the #else branch)
 * --------------------------------------------------------------------- */
struct clap_nr_gui_s {
    void               *plugin;
    gui_param_cb_t      on_param_change;
    gui_tooltips_cb_t   on_tooltips_change;

    /* Cocoa objects – only touched on main thread */
    NSWindow           *window;        /* __strong via ObjC ARC */
    NSOpenGLView       *gl_view;

    /* DisplayLink – fires on a private thread */
    CVDisplayLinkRef    display_link;

    /* Shared ImGui context */
    ImGuiContext       *imgui_ctx;

    /* Parameter cache */
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
    float  nr0_aggression;
    int    nr0_max_notches;
    float  nr0_threshold;
    volatile int nr0_active_notches;

    bool   updating;
    bool   tooltips_on;
    bool   open_website;
    int    min_w, min_h;
    char   window_title[256];
};

/* -----------------------------------------------------------------------
 * CVDisplayLink callback – runs on a private high-priority thread.
 * Make the NSOpenGLContext current here and call render_frame().
 * --------------------------------------------------------------------- */
static CVReturn display_link_cb(CVDisplayLinkRef   /*link*/,
                                const CVTimeStamp * /*now*/,
                                const CVTimeStamp * /*output*/,
                                CVOptionFlags       /*flagsIn*/,
                                CVOptionFlags      * /*flagsOut*/,
                                void               *ctx)
{
    clap_nr_gui_s *g = (clap_nr_gui_s *)ctx;
    if (!g || !g->gl_view || !g->imgui_ctx) return kCVReturnSuccess;

    NSOpenGLContext *glctx = [g->gl_view openGLContext];
    [glctx makeCurrentContext];

    /* Lock the context so AppKit resize operations don't race */
    CGLLockContext([glctx CGLContextObj]);
    render_frame(g);
    CGLUnlockContext([glctx CGLContextObj]);

    return kCVReturnSuccess;
}

/* -----------------------------------------------------------------------
 * NSOpenGLView subclass – handles resize and forwards mouse/key to ImGui
 * --------------------------------------------------------------------- */
@interface ClapNRGLView : NSOpenGLView
@property (assign) clap_nr_gui_s *gui;
@end

@implementation ClapNRGLView

- (void)reshape {
    [super reshape];
    NSRect b = [self bounds];
    CGLLockContext([[self openGLContext] CGLContextObj]);
    glViewport(0, 0, (GLsizei)b.size.width, (GLsizei)b.size.height);
    CGLUnlockContext([[self openGLContext] CGLContextObj]);
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return NO; }

/* Forward mouse events to ImGui via the context */
- (void)mouseMoved:(NSEvent *)e     { [self forwardEvent:e]; }
- (void)mouseDown:(NSEvent *)e      { [self forwardEvent:e]; }
- (void)mouseUp:(NSEvent *)e        { [self forwardEvent:e]; }
- (void)mouseDragged:(NSEvent *)e   { [self forwardEvent:e]; }
- (void)rightMouseDown:(NSEvent *)e { [self forwardEvent:e]; }
- (void)rightMouseUp:(NSEvent *)e   { [self forwardEvent:e]; }
- (void)scrollWheel:(NSEvent *)e    { [self forwardEvent:e]; }
- (void)keyDown:(NSEvent *)e        { [self forwardEvent:e]; }
- (void)keyUp:(NSEvent *)e          { [self forwardEvent:e]; }

- (void)forwardEvent:(NSEvent *)e {
    clap_nr_gui_s *g = self.gui;
    if (!g || !g->imgui_ctx) return;
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();

    NSEventType t = [e type];
    NSPoint loc = [self convertPoint:[e locationInWindow] fromView:nil];
    NSRect b    = [self bounds];
    /* Flip Y: NSView is bottom-origin, ImGui is top-origin */
    float mx = (float)loc.x;
    float my = (float)(b.size.height - loc.y);

    switch (t) {
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
        io.AddMousePosEvent(mx, my);
        break;
    case NSEventTypeLeftMouseDown:
        io.AddMousePosEvent(mx, my);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        break;
    case NSEventTypeLeftMouseUp:
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        break;
    case NSEventTypeRightMouseDown:
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        break;
    case NSEventTypeRightMouseUp:
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        break;
    case NSEventTypeScrollWheel:
        io.AddMouseWheelEvent(0.0f, (float)[e scrollingDeltaY] * 0.1f);
        break;
    case NSEventTypeKeyDown: {
        NSString *chars = [e characters];
        if ([chars length] > 0)
            io.AddInputCharactersUTF8([[chars UTF8String] ?: "" ]);
        break;
    }
    default: break;
    }
}

@end

/* -----------------------------------------------------------------------
 * Create the NSWindow and NSOpenGLView on the MAIN THREAD.
 * Initialise ImGui and start the CVDisplayLink.
 * --------------------------------------------------------------------- */
static bool mac_create_window(clap_nr_gui_s *g)
{
    NSAssert([NSThread isMainThread], @"mac_create_window must run on main thread");

    /* NSOpenGLPixelFormat */
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFAColorSize,     24,
        NSOpenGLPFAAlphaSize,     8,
        NSOpenGLPFADepthSize,     24,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!pf) { fprintf(stderr, "clap-nr: NSOpenGLPixelFormat failed\n"); return false; }

    /* Create view */
    NSRect frame = NSMakeRect(0, 0, GUI_BASE_W, GUI_BASE_H);
    ClapNRGLView *view = [[ClapNRGLView alloc] initWithFrame:frame pixelFormat:pf];
    if (!view) { fprintf(stderr, "clap-nr: ClapNRGLView alloc failed\n"); return false; }
    view.gui = g;
    [view setWantsBestResolutionOpenGLSurface:YES];

    /* Create floating window */
    NSUInteger style = NSWindowStyleMaskTitled
                     | NSWindowStyleMaskClosable
                     | NSWindowStyleMaskResizable
                     | NSWindowStyleMaskMiniaturizable;

    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!win) { fprintf(stderr, "clap-nr: NSWindow alloc failed\n"); return false; }

    [win setTitle:[NSString stringWithUTF8String:g->window_title]];
    [win setContentView:view];
    [win setReleasedWhenClosed:NO];
    [win setMinSize:NSMakeSize(GUI_BASE_W, GUI_BASE_H)];
    [win center];

    /* Make context current so we can call glViewport once now */
    [[view openGLContext] makeCurrentContext];
    glViewport(0, 0, GUI_BASE_W, GUI_BASE_H);

    /* Init ImGui */
    IMGUI_CHECKVERSION();
    g->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize  = ImVec2((float)GUI_BASE_W, (float)GUI_BASE_H);
    io.DeltaTime    = 1.0f / 60.0f;

    /* Apply theme (defined in gui_imgui.cpp, forward-declared via static linkage) */
    extern void apply_theme();
    apply_theme();

    ImGui_ImplOpenGL3_Init("#version 150");

    /* Release context so the DisplayLink thread can take it */
    [NSOpenGLContext clearCurrentContext];

    /* Store Cocoa objects */
    g->window  = win;
    g->gl_view = view;

    /* Start CVDisplayLink */
    CVDisplayLinkCreateWithActiveCGDisplays(&g->display_link);
    CVDisplayLinkSetOutputCallback(g->display_link, display_link_cb, g);

    CGLContextObj cglCtx = [[view openGLContext] CGLContextObj];
    CGLPixelFormatObj cglPF = [pf CGLPixelFormatObj];
    CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(g->display_link, cglCtx, cglPF);
    CVDisplayLinkStart(g->display_link);

    return true;
}

/* -----------------------------------------------------------------------
 * Destroy everything – MUST be called on main thread
 * --------------------------------------------------------------------- */
static void mac_destroy_window(clap_nr_gui_s *g)
{
    if (!g) return;

    if (g->display_link) {
        CVDisplayLinkStop(g->display_link);
        CVDisplayLinkRelease(g->display_link);
        g->display_link = nullptr;
    }

    if (g->imgui_ctx) {
        /* Make context current briefly for ImGui GL cleanup */
        if (g->gl_view)
            [[g->gl_view openGLContext] makeCurrentContext];
        ImGui::SetCurrentContext(g->imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(g->imgui_ctx);
        g->imgui_ctx = nullptr;
        [NSOpenGLContext clearCurrentContext];
    }

    if (g->window) {
        [g->window orderOut:nil];
        [g->window setContentView:nil];
        g->window  = nil;
        g->gl_view = nil;
    }
}

/* -----------------------------------------------------------------------
 * render_frame override for macOS – replaces the GLFW Present call.
 * Called from display_link_cb (already has GL context current + locked).
 * ImGui_ImplOpenGL3_NewFrame / RenderDrawData handle the rest.
 *
 * NOTE: We do NOT include imgui_impl_glfw here.  Instead we manually
 * drive ImGuiIO each frame.
 * --------------------------------------------------------------------- */
static void mac_render_frame(clap_nr_gui_s *g)
{
    if (!g || !g->imgui_ctx || !g->gl_view) return;

    ImGui::SetCurrentContext(g->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();

    /* Update display size from view bounds (handles Retina / resize) */
    NSRect b  = [g->gl_view bounds];
    NSRect fb = [g->gl_view convertRectToBacking:b];
    io.DisplaySize             = ImVec2((float)b.size.width, (float)b.size.height);
    io.DisplayFramebufferScale = ImVec2(
        b.size.width  > 0 ? (float)(fb.size.width  / b.size.width)  : 1.0f,
        b.size.height > 0 ? (float)(fb.size.height / b.size.height) : 1.0f);
    io.DeltaTime = 1.0f / 60.0f;

    glViewport(0, 0, (GLsizei)fb.size.width, (GLsizei)fb.size.height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    /* ---- delegate to the shared UI layout defined in gui_imgui.cpp ---- */
    extern void render_ui_contents(clap_nr_gui_s *g);
    render_ui_contents(g);
    /* ------------------------------------------------------------------- */

    static const ImVec4 COL_BG = {0.09f, 0.10f, 0.13f, 1.00f};
    ImGui::Render();
    glClearColor(COL_BG.x, COL_BG.y, COL_BG.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    [[g->gl_view openGLContext] flushBuffer];

    if (g->open_website) {
        g->open_website = false;
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSWorkspace sharedWorkspace]
                openURL:[NSURL URLWithString:@"https://clapnr.com"]];
        });
    }
}
