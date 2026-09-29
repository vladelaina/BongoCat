// Regression check for macOS pass-through: once the platform is told the pet window must ignore the
// mouse, SDL's own mouse-motion handling must not take that back. SDL derives
// NSWindow.ignoresMouseEvents from the window shape on every mouse move, so a click-through window
// has to describe itself to SDL through that shape as well; otherwise the next move makes the pet
// clickable again and the window server keeps routing clicks to it.
//
// It drives the real platform implementation over a real SDL window created like the pet window and
// asserts the user-visible outcome: the window server's own hit test at the window centre. The move
// event is posted to this process only (no event tap, no other application, no cursor movement).
//
// Needs a window server and the Cocoa driver. Only when those are unavailable does it exit 77, which
// ctest reports as a skip (SKIP_RETURN_CODE in cmake/Tests.cmake); a window that cannot be created or
// that is not a Cocoa window fails instead.

#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "bongo_cat/platform.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CHECK_SKIP = 77, PET_SIZE = 280 };

static int failures;

static void expect(int condition, const char *what) {
    printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) failures++;
}

static void skip(const char *reason) {
    printf("SKIP %s\n", reason);
    exit(CHECK_SKIP);
}

static NSWindow *cocoa_window(SDL_Window *window) {
    return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

// Window number of the topmost window at a screen point, i.e. the window the window server would
// hand the mouse to there.
static NSInteger mouse_target_at(NSPoint point) {
    return [NSWindow windowNumberAtPoint:point belowWindowWithWindowNumber:0];
}

static void pump_runloop(double seconds) {
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

static void pump_sdl(int milliseconds) {
    uint64_t deadline = SDL_GetTicksNS() + (uint64_t)milliseconds * 1000000ull;
    SDL_Event event;
    while (SDL_GetTicksNS() < deadline) {
        if (!SDL_WaitEventTimeout(&event, 10)) continue;
        while (SDL_PollEvent(&event)) { }
    }
}

// A window move AppKit would deliver to this window while the pointer is over it.
static void post_mouse_move(NSWindow *window) {
    NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
        location:NSMakePoint(PET_SIZE / 2, PET_SIZE / 2) modifierFlags:0
        timestamp:[[NSProcessInfo processInfo] systemUptime]
        windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:0 pressure:0];
    [NSApp postEvent:event atStart:NO];
    pump_sdl(300);
    pump_runloop(0.3);
}

// Before anything is concluded about the pet window, confirm the hit test reflects the flag here,
// with two throwaway plain windows of the same shape.
static void require_working_instrument(void) {
    NSRect frame = NSMakeRect(140, NSScreen.screens.firstObject.frame.size.height - 70, 80, 80);
    NSWindow *under = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    NSWindow *upper = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    for (NSWindow *window in @[under, upper]) {
        window.opaque = NO;
        window.backgroundColor = [NSColor colorWithCalibratedWhite:0 alpha:0.01];
    }
    upper.level = NSFloatingWindowLevel;
    [under orderFront:nil];
    [upper orderFront:nil];
    pump_runloop(0.4);
    NSPoint point = NSMakePoint(NSMidX(upper.frame), NSMidY(upper.frame));
    if (mouse_target_at(point) != upper.windowNumber) {
        [upper orderOut:nil]; [under orderOut:nil];
        skip("the hit test query does not report our own floating window here");
    }
    upper.ignoresMouseEvents = YES;
    pump_runloop(0.2);
    NSInteger skipped = mouse_target_at(point);
    [upper orderOut:nil]; [under orderOut:nil];
    pump_runloop(0.2);
    if (skipped == upper.windowNumber) skip("the hit test query cannot observe ignoresMouseEvents here");
}

// A missing window or a missing Cocoa window is a regression of what this check covers, not an
// environment limitation: those are only excused when SDL cannot use the Cocoa driver at all.
static void fail(const char *what) {
    expect(0, what);
    printf("click-through check failed\n");
    exit(1);
}

static void check_rendered_alpha(SDL_Window *window, SDL_GLContext context) {
    if (!context) fail("pixel hit testing requires an OpenGL context");
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) fail("allocate pointer hit test state");
    app->window = window;
    app->gl_context = context;
    app->platform.window = window;
    app->session.window.visible = true;
    app->pointer_known = true;
    int x, y, width, height;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &width, &height);
    app->pointer_x = x + width / 2;
    app->pointer_y = y + height / 2;
    expect(bongo_cat_platform_dynamic_hit_supported(),
        "pixel hit testing works without starting the Input Monitoring listener");
    NSWindow *native = cocoa_window(window);
    NSWindow *key = NSApp.keyWindow;
    const unsigned char values[] = {255, 0, 8, 9, 0, 255};
    for (size_t i = 0; i < sizeof(values); ++i) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, values[i] / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        bongo_cat_window_capture_pointer_hit(app, true);
        bongo_cat_window_sync_click_through(app);
        expect(native.ignoresMouseEvents == (values[i] <= 8),
            "current rendered alpha controls blank-area routing, including threshold edges");
        expect(SDL_GL_SwapWindow(window), "present the sampled frame");
        post_mouse_move(native);
        expect(native.ignoresMouseEvents == (values[i] <= 8),
            "automatic blank-area routing survives SDL motion");
        expect(NSApp.keyWindow == key, "alpha changes do not activate the pet");
    }
    app->left_mouse_down = true;
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_window_capture_pointer_hit(app, true);
    bongo_cat_window_sync_click_through(app);
    expect(!native.ignoresMouseEvents, "an active drag keeps model interaction");
    app->left_mouse_down = false;
    bongo_cat_window_capture_pointer_hit(app, true);
    bongo_cat_window_sync_click_through(app);
    expect(native.ignoresMouseEvents, "blank-area routing resumes after drag release");
    bongo_cat_platform_set_click_through(&app->platform, false, false);
    free(app);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    [NSApplication.sharedApplication setActivationPolicy:NSApplicationActivationPolicyAccessory];
    require_working_instrument();

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");  // a check must not steal focus
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        skip(SDL_GetError());
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    if (!driver || strcmp(driver, "cocoa") != 0) {
        char reason[96];
        snprintf(reason, sizeof(reason), "SDL video driver is %s, not cocoa", driver ? driver : "absent");
        skip(reason);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_Window *window = SDL_CreateWindow("bongo-cat-click-through-check", PET_SIZE, PET_SIZE,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY |
        SDL_WINDOW_HIDDEN | SDL_WINDOW_TRANSPARENT);
    if (!window) fail("SDL creates a transparent borderless window like the pet window");
    SDL_SetWindowPosition(window, 40, 40);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context) {
        if (!SDL_GL_MakeCurrent(window, context)) printf("note: no current GL context: %s\n",
            SDL_GetError());
    } else {
        printf("note: no GL context (%s); the window is still the one the pet uses\n", SDL_GetError());
    }

    NSWindow *native = cocoa_window(window);
    if (!native) fail("SDL hands the platform a Cocoa window");
    NSPoint centre = NSMakePoint(NSMidX(native.frame), NSMidY(native.frame));
    BongoCatPlatform platform;
    memset(&platform, 0, sizeof(platform));
    platform.window = window;

    // bongo_cat_window_apply() order: the pet is configured while hidden, then revealed.
    bongo_cat_platform_set_visible(&platform, false);
    bongo_cat_platform_set_click_through(&platform, true, false);
    bongo_cat_platform_set_always_on_top(&platform, true);
    bongo_cat_platform_set_visible(&platform, true);
    pump_runloop(0.5);
    expect(native.ignoresMouseEvents, "pass-through reaches the native window");
    expect(mouse_target_at(centre) != native.windowNumber,
        "the window server passes the mouse through the pet before any motion");

    post_mouse_move(native);
    expect(native.ignoresMouseEvents, "pass-through survives SDL mouse-motion handling");
    expect(mouse_target_at(centre) != native.windowNumber,
        "the window server still passes the mouse through the pet after motion");

    // Dynamic transparency (transparent pixel under the pointer) uses the same platform call.
    bongo_cat_platform_set_click_through(&platform, false, true);
    pump_runloop(0.2);
    expect(native.ignoresMouseEvents, "dynamic transparency reaches the native window");
    post_mouse_move(native);
    expect(native.ignoresMouseEvents, "dynamic transparency survives SDL mouse-motion handling");

    // Turning pass-through off must give the pet its window interaction back, and it must stay that
    // way: SDL rewrites ignoresMouseEvents from the window shape on the next mouse move.
    bongo_cat_platform_set_click_through(&platform, false, false);
    pump_runloop(0.2);
    expect(!native.ignoresMouseEvents, "pass-through off reaches the native window");
    expect(mouse_target_at(centre) == native.windowNumber,
        "the window server targets the pet again once pass-through is off");
    post_mouse_move(native);
    expect(!native.ignoresMouseEvents, "pass-through off survives SDL mouse-motion handling");
    expect(mouse_target_at(centre) == native.windowNumber,
        "the window server still targets the pet after motion once pass-through is off");

    // The same has to hold when the state is toggled repeatedly, not only on the first switch off.
    char label[128];
    for (int cycle = 1; cycle <= 2; cycle++) {
        bongo_cat_platform_set_click_through(&platform, true, false);
        pump_runloop(0.2);
        post_mouse_move(native);
        snprintf(label, sizeof(label), "pass-through on survives motion in cycle %d", cycle);
        expect(native.ignoresMouseEvents, label);
        snprintf(label, sizeof(label), "the window server passes the mouse through in cycle %d",
            cycle);
        expect(mouse_target_at(centre) != native.windowNumber, label);

        bongo_cat_platform_set_click_through(&platform, false, false);
        pump_runloop(0.2);
        post_mouse_move(native);
        snprintf(label, sizeof(label), "pass-through off survives motion in cycle %d", cycle);
        expect(!native.ignoresMouseEvents, label);
        snprintf(label, sizeof(label), "the window server targets the pet in cycle %d", cycle);
        expect(mouse_target_at(centre) == native.windowNumber, label);
    }

    check_rendered_alpha(window, context);
    if (context) SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    pump_runloop(0.2);
    printf("%s\n", failures ? "click-through check failed" : "click-through check passed");
    return failures ? 1 : 0;
}
