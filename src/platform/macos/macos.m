#include "bongo_cat/platform.h"
#include "macos_internal.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int instance_lock = -1;
static BongoCatPlatform *active_platform;
static NSObject *instance_observer;
static bool instance_show_pending;

@interface BongoCatModalTarget : NSObject { BongoCatModalTick callback_; void *userdata_; }
- (id)initWithCallback:(BongoCatModalTick)callback userdata:(void *)userdata;
- (void)tick:(NSTimer *)timer;
@end
@implementation BongoCatModalTarget
- (id)initWithCallback:(BongoCatModalTick)callback userdata:(void *)userdata {
    self = [super init];
    if (self) { callback_ = callback; userdata_ = userdata; }
    return self;
}
- (void)tick:(NSTimer *)timer { (void)timer; callback_(userdata_); }
@end

static NSWindow *native_window(BongoCatPlatform *platform) {
    return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

static void configure_capture_window(NSWindow *window) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [window setSharingType:NSWindowSharingReadOnly];
}

static void show_instance(void) {
    if (!active_platform) { instance_show_pending = true; return; }
    NSWindow *window = native_window(active_platform); configure_capture_window(window);
    [window orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];
    instance_show_pending = false;
}

@interface BongoCatInstanceObserver : NSObject
- (void)showWindow:(NSNotification *)notification;
@end
@implementation BongoCatInstanceObserver
- (void)showWindow:(NSNotification *)notification {
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(showWindow:)
            withObject:notification waitUntilDone:NO];
        return;
    }
    show_instance();
}
@end

static void observe_instance(void) {
    if (instance_observer) return;
    instance_observer = [[BongoCatInstanceObserver alloc] init];
    [[NSDistributedNotificationCenter defaultCenter] addObserver:instance_observer
        selector:@selector(showWindow:) name:@"com.bongocat.desktop.show" object:nil
        suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
}
BongoCatResult bongo_cat_platform_init(BongoCatPlatform *platform, SDL_Window *window,
    BongoCatInputState *input, BongoCatError *error) {
    memset(platform, 0, sizeof(*platform));
    platform->window = window;
    platform->input = input;
    platform->window_opacity = 1.0f;
    platform->wake_event_type = SDL_RegisterEvents(1);
    if (platform->wake_event_type == (Uint32)-1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot reserve the macOS input wake event");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    active_platform = platform;
    configure_capture_window(native_window(platform));
    BongoCatError input_error = {0};
    if (!bongo_cat_macos_input_start(platform, &input_error))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", input_error.message);
    if (instance_show_pending) show_instance();
    return BONGO_CAT_OK;
}
void bongo_cat_platform_shutdown(BongoCatPlatform *platform) {
    bongo_cat_macos_input_stop(platform);
    if (active_platform == platform) active_platform = NULL;
}
/* SDL rewrites NSWindow.ignoresMouseEvents from the window shape whenever a mouse move reaches the
   window, and derives "ignore the mouse" from a transparent shape pixel. Telling SDL the same state
   through that shape keeps the two from disagreeing, which would otherwise hand the pet back to the
   window server on the next move. The native property is written afterwards: SDL's answer depends on
   where the pointer is, while this state has to hold wherever the pointer is. */
static void apply_click_through_shape(BongoCatPlatform *platform, bool enabled) {
    if (!(SDL_GetWindowFlags(platform->window) & SDL_WINDOW_TRANSPARENT)) return;
    SDL_Surface *shape = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_ARGB32);
    if (!shape) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Click-through window has no shape SDL can follow: %s", SDL_GetError());
        return;
    }
    if (!SDL_WriteSurfacePixel(shape, 0, 0, 0, 0, 0,
            enabled ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE) ||
        !SDL_SetWindowShape(platform->window, shape))
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Click-through state is not described to SDL: %s", SDL_GetError());
    SDL_DestroySurface(shape);
}

void bongo_cat_platform_set_click_through(BongoCatPlatform *platform,
    bool forced, bool pointer_transparent) {
    if (!platform || !platform->window) return;
    bool enabled = forced || pointer_transparent;
    apply_click_through_shape(platform, enabled);
    [native_window(platform) setIgnoresMouseEvents:enabled];
}
bool bongo_cat_platform_set_opacity(BongoCatPlatform *platform, float opacity) {
    if (!platform || !platform->window) return false;
    opacity = SDL_clamp(opacity, 0.0f, 1.0f);
    if (opacity == platform->window_opacity) return true;
    if (!SDL_SetWindowOpacity(platform->window, opacity)) return false;
    platform->window_opacity = opacity;
    return true;
}
float bongo_cat_platform_get_opacity(const BongoCatPlatform *platform) {
    return platform ? platform->window_opacity : 1.0f;
}
bool bongo_cat_platform_present(BongoCatPlatform *platform, int width, int height) {
    (void)width; (void)height;
    return platform && platform->window && SDL_GL_SwapWindow(platform->window);
}
bool bongo_cat_platform_frame_alpha(const BongoCatPlatform *platform,
    int width, int height, int x, int y, uint8_t *alpha) {
    (void)platform; (void)width; (void)height; (void)x; (void)y; (void)alpha;
    return false;
}
void bongo_cat_platform_set_visible(BongoCatPlatform *platform, bool visible) {
    if (!platform || !platform->window) return;
    visible ? SDL_ShowWindow(platform->window) : SDL_HideWindow(platform->window); if (visible) configure_capture_window(native_window(platform));
}
bool bongo_cat_platform_pointer_local(BongoCatPlatform *platform, double screen_x,
    double screen_y, float *local_x, float *local_y) {
    int x, y, width, height;
    if (!platform || !local_x || !local_y ||
        !SDL_GetWindowPosition(platform->window, &x, &y) ||
        !SDL_GetWindowSize(platform->window, &width, &height)) return false;
    *local_x = (float)(screen_x - x); *local_y = (float)(screen_y - y);
    return *local_x >= 0 && *local_x < width && *local_y >= 0 && *local_y < height;
}
bool bongo_cat_platform_pointer_locked(BongoCatPlatform *platform) {
    (void)platform;
    return false;
}
bool bongo_cat_platform_relative_pointer(BongoCatPlatform *platform,
    double *x, double *y) {
    (void)platform; (void)x; (void)y;
    return false;
}
void bongo_cat_platform_relative_pointer_reset(BongoCatPlatform *platform) {
    (void)platform;
}
void bongo_cat_platform_relative_pointer_release(BongoCatPlatform *platform) {
    (void)platform;
}
void bongo_cat_platform_set_always_on_top(BongoCatPlatform *platform, bool enabled) {
    [native_window(platform) setLevel:enabled ? NSFloatingWindowLevel : NSNormalWindowLevel];
}
void bongo_cat_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    configure_capture_window(native);
    [NSApp activateIgnoringOtherApps:YES];
    [native makeKeyAndOrderFront:nil];
}

bool bongo_cat_platform_set_geometry(BongoCatPlatform *platform,
    int x, int y, int width, int height) {
    if (!platform || !platform->window) return false;
    int current_width, current_height;
    if ((!SDL_GetWindowSize(platform->window, &current_width, &current_height) ||
        current_width != width || current_height != height) &&
        !SDL_SetWindowSize(platform->window, width, height)) return false;
    int current_x, current_y;
    if (!SDL_GetWindowPosition(platform->window, &current_x, &current_y) ||
        current_x != x || current_y != y)
        SDL_SetWindowPosition(platform->window, x, y);
    return true;
}
void bongo_cat_platform_begin_drag(BongoCatPlatform *platform,
    BongoCatModalTick modal_tick, void *userdata) {
    NSWindow *window = native_window(platform);
    BongoCatModalTarget *target = modal_tick ? [[BongoCatModalTarget alloc]
        initWithCallback:modal_tick userdata:userdata] : nil;
    if (modal_tick) modal_tick(userdata);
    NSTimer *timer = target ? [NSTimer timerWithTimeInterval:1.0 / 60.0
        target:target selector:@selector(tick:) userInfo:nil repeats:YES] : nil;
    if (timer) [[NSRunLoop currentRunLoop] addTimer:timer
        forMode:NSRunLoopCommonModes];
    [window performWindowDragWithEvent:[NSApp currentEvent]];
    [timer invalidate];
    if (modal_tick) modal_tick(userdata);
    [target release];
}
bool bongo_cat_platform_dynamic_hit_supported(void) {
    return bongo_cat_macos_input_supported();
}

bool bongo_cat_platform_input_monitoring_authorized(void) {
    return bongo_cat_macos_input_monitoring_authorized();
}

bool bongo_cat_platform_input_monitoring_request(void) {
    return bongo_cat_macos_input_monitoring_request();
}

bool bongo_cat_platform_open_directory(const char *path) {
    if (!path || !path[0]) return false;
    @autoreleasepool {
        NSString *directory = [NSString stringWithUTF8String:path];
        return directory && [[NSWorkspace sharedWorkspace]
            openURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    }
}

bool bongo_cat_platform_single_instance_begin(void) {
    char path[96]; snprintf(path, sizeof(path),
        "/tmp/" BONGO_CAT_SLUG "-%lu.lock",
        (unsigned long)getuid());
    instance_lock = open(path, O_CREAT | O_RDWR, 0600);
    if (instance_lock < 0 || flock(instance_lock, LOCK_EX | LOCK_NB) == 0) {
        observe_instance(); return true;
    }
    [[NSDistributedNotificationCenter defaultCenter]
        postNotificationName:@"com.bongocat.desktop.show" object:nil
        userInfo:nil deliverImmediately:YES];
    close(instance_lock); instance_lock = -1; return false;
}
bool bongo_cat_platform_single_instance_take_wake(void) { return false; }
void bongo_cat_platform_single_instance_end(void) {
    if (instance_lock >= 0) close(instance_lock);
    instance_lock = -1;
    if (instance_observer) {
        [[NSDistributedNotificationCenter defaultCenter] removeObserver:instance_observer];
        [instance_observer release]; instance_observer = nil;
    }
}
BongoCatResult bongo_cat_platform_embedded_assets(const char *target, BongoCatError *error) {
    (void)target; (void)error; return BONGO_CAT_ERROR_PLATFORM;
}
#endif
