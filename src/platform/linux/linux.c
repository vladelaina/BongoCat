#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bongo_cat/platform.h"
#include "bongo_cat/common.h"
#include "bongo_cat/path.h"
#include "linux_internal.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <X11/Xlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

static int instance_lock = -1;
static BongoCatPlatform *active_platform;

void bongo_cat_platform_configure_preferences_window(SDL_Window *window) {
    (void)window;
}

static void publish_instance_window(SDL_Window *window) {
    if (instance_lock < 0 || !window) return;
    Window id = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!id) return;
    char text[32]; int length = snprintf(text, sizeof(text), "%lu\n", (unsigned long)id);
    if (length <= 0 || (size_t)length >= sizeof(text)) return;
    if (ftruncate(instance_lock, 0) == 0 && lseek(instance_lock, 0, SEEK_SET) == 0) {
        if (write(instance_lock, text, (size_t)length) == (ssize_t)length) fsync(instance_lock);
    }
}

static void restore_instance_window(void) {
    if (instance_lock < 0) return;
    char text[32] = {0}; lseek(instance_lock, 0, SEEK_SET);
    ssize_t length = read(instance_lock, text, sizeof(text) - 1);
    Window window = length > 0 ? (Window)strtoul(text, NULL, 10) : 0;
    Display *display = window ? XOpenDisplay(NULL) : NULL;
    if (!display) return;
    XMapRaised(display, window);
    Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XEvent event = {0}; event.xclient.type = ClientMessage;
    event.xclient.window = window; event.xclient.message_type = active;
    event.xclient.format = 32; event.xclient.data.l[0] = 2;
    XSendEvent(display, DefaultRootWindow(display), False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display); XCloseDisplay(display);
}

static bool executable_path(char output[BONGO_CAT_PATH_CAP]) {
    const char *appimage = getenv("APPIMAGE");
    if (appimage && appimage[0]) {
        int length = snprintf(output, BONGO_CAT_PATH_CAP, "%s", appimage);
        return length >= 0 && length < BONGO_CAT_PATH_CAP;
    }
    ssize_t length = readlink("/proc/self/exe", output, BONGO_CAT_PATH_CAP - 1);
    if (length <= 0 || length >= BONGO_CAT_PATH_CAP) return false;
    output[length] = '\0'; return true;
}

BongoCatResult bongo_cat_platform_init(BongoCatPlatform *platform, SDL_Window *window,
    BongoCatInputState *input, BongoCatError *error) {
    memset(platform, 0, sizeof(*platform));
    platform->window = window;
    platform->input = input;
    platform->window_opacity = 1.0f;
    const char *driver = SDL_GetCurrentVideoDriver();
    platform->hover_hide_unavailable = !driver || strcmp(driver, "x11") != 0;
    if (platform->hover_hide_unavailable)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Hover hiding requires the X11 backend on Linux");
    platform->wake_event_type = SDL_RegisterEvents(1);
    if (platform->wake_event_type == (Uint32)-1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot reserve the Linux input wake event");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    LinuxPlatformState *native = calloc(1, sizeof(*native));
    if (!native) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot allocate Linux platform state");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    platform->native = native;
    active_platform = platform;
    publish_instance_window(window);
    const char *wayland = getenv("WAYLAND_DISPLAY");
    const char *session = getenv("XDG_SESSION_TYPE");
    native->wayland = (wayland && wayland[0]) ||
        (driver && strcmp(driver, "wayland") == 0) ||
        (session && strcmp(session, "wayland") == 0);
    native->evdev_selected = bongo_cat_linux_evdev_requested(
        getenv("BONGOCAT_ENABLE_EVDEV"), native->wayland);
    BongoCatError input_error = {0};
    if (!bongo_cat_linux_x11_start(platform, &input_error) && input_error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", input_error.message);
    if (native->evdev_selected) {
        input_error = (BongoCatError){0};
        if (!bongo_cat_linux_evdev_start(platform, &input_error) && input_error.message[0])
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", input_error.message);
    }
    return BONGO_CAT_OK;
}
void bongo_cat_platform_shutdown(BongoCatPlatform *platform) {
    if (!platform) return;
    bongo_cat_linux_x11_stop(platform);
    bongo_cat_linux_evdev_stop(platform);
    free(platform->native);
    platform->native = NULL;
    if (active_platform == platform) active_platform = NULL;
}
void bongo_cat_platform_set_click_through(BongoCatPlatform *platform,
    bool forced, bool pointer_transparent) {
    bongo_cat_linux_x11_click_through(platform, forced || pointer_transparent);
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
    visible ? SDL_ShowWindow(platform->window) : SDL_HideWindow(platform->window);
    if (visible) {
        bongo_cat_linux_x11_configure_capture_window(platform);
        const LinuxPlatformState *native = platform->native;
        if (native) bongo_cat_linux_x11_set_above(platform, native->always_on_top);
    }
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
    return bongo_cat_linux_evdev_pointer_active(platform);
}
bool bongo_cat_platform_relative_pointer(BongoCatPlatform *platform,
    double *x, double *y) {
    return bongo_cat_linux_evdev_relative_pointer(platform, x, y);
}
void bongo_cat_platform_relative_pointer_reset(BongoCatPlatform *platform) {
    bongo_cat_linux_evdev_relative_pointer_reset(platform);
}
void bongo_cat_platform_relative_pointer_release(BongoCatPlatform *platform) {
    bongo_cat_linux_evdev_relative_pointer_reset(platform);
}
void bongo_cat_platform_set_always_on_top(BongoCatPlatform *platform, bool enabled) {
    if (!platform || !platform->window) return;
    LinuxPlatformState *native = platform->native;
    if (native) native->always_on_top = enabled;
    SDL_SetWindowAlwaysOnTop(platform->window, enabled);
    bongo_cat_linux_x11_set_above(platform, enabled);
    bongo_cat_linux_x11_configure_capture_window(platform);
}
void bongo_cat_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    if (active_platform && active_platform->window == window)
        bongo_cat_platform_set_visible(active_platform, true);
    else SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
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
    (void)modal_tick; (void)userdata;
    bongo_cat_linux_x11_begin_drag(platform);
}
bool bongo_cat_platform_dynamic_hit_supported(void) {
    /* XWayland only forwards pointer events while one of its surfaces has
       pointer focus, so an empty input region would hide the pointer
       permanently and the pet could never become clickable again. */
    return bongo_cat_linux_x11_supported(active_platform) &&
        !bongo_cat_linux_x11_xwayland(active_platform);
}

bool bongo_cat_platform_open_directory(const char *path) {
    if (!path || !path[0]) return false;
    pid_t launcher = fork();
    if (launcher < 0) return false;
    if (launcher == 0) {
        pid_t opener = fork();
        if (opener < 0) _exit(127);
        if (opener > 0) _exit(0);
        setsid();
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(launcher, &status, 0) == launcher &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void bongo_cat_platform_set_tray_callbacks(void *tray,
    BongoCatTrayClick left_click, BongoCatModalTick modal_tick,
    BongoCatTrayRestore restore, void *userdata) {
    (void)tray; (void)left_click; (void)modal_tick;
    (void)restore; (void)userdata;
}
bool bongo_cat_platform_single_instance_begin(void) {
    char path[96]; snprintf(path, sizeof(path), "/tmp/%s-%lu.lock",
        BONGO_CAT_SLUG,
        (unsigned long)getuid());
    instance_lock = open(path, O_CREAT | O_RDWR, 0600);
    if (instance_lock < 0 || flock(instance_lock, LOCK_EX | LOCK_NB) == 0) return true;
    restore_instance_window(); close(instance_lock); instance_lock = -1; return false;
}
bool bongo_cat_platform_single_instance_take_wake(void) { return false; }
void bongo_cat_platform_single_instance_end(void) {
    if (instance_lock >= 0) close(instance_lock);
    instance_lock = -1;
}
BongoCatResult bongo_cat_platform_set_autostart(bool enabled, BongoCatError *error) {
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char config[BONGO_CAT_PATH_CAP], directory[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    if (base && base[0]) snprintf(config, sizeof(config), "%s", base);
    else if (home && bongo_cat_path_join(config, sizeof(config), home, ".config")) {}
    else return BONGO_CAT_ERROR_PLATFORM;
    if (!bongo_cat_path_join(directory, sizeof(directory), config, "autostart") ||
        !bongo_cat_path_join(path, sizeof(path), directory, BONGO_CAT_SLUG ".desktop"))
        return BONGO_CAT_ERROR_IO;
    if (!enabled) {
        if (remove(path) == 0 || errno == ENOENT) return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot remove Linux autostart entry");
        return BONGO_CAT_ERROR_IO;
    }
    char executable[BONGO_CAT_PATH_CAP];
    if (!executable_path(executable) || strchr(executable, '"') ||
        !bongo_cat_path_create_directory(directory)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot prepare Linux autostart entry");
        return BONGO_CAT_ERROR_IO;
    }
    FILE *file = fopen(path, "wb");
    if (!file) return BONGO_CAT_ERROR_IO;
    bool written = fprintf(file, "[Desktop Entry]\nType=Application\nName=BongoCat\n"
        "Exec=\"%s\" --autostart\nTerminal=false\nX-GNOME-Autostart-enabled=true\n", executable) > 0;
    if (fclose(file) != 0) written = false;
    if (written) return BONGO_CAT_OK;
    remove(path); bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot write Linux autostart entry");
    return BONGO_CAT_ERROR_IO;
}
BongoCatResult bongo_cat_platform_embedded_assets(const char *target, BongoCatError *error) {
    (void)target; (void)error; return BONGO_CAT_ERROR_PLATFORM;
}
#endif
