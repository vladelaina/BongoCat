#include "bongo_cat/platform.h"
#include "windows_borderless.h"
#include "windows_capture.h"
#include "windows_layered.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <windows.h>

static HWND native_window(BongoCatPlatform *platform) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

bool bongo_cat_platform_pointer_local(BongoCatPlatform *platform, double screen_x,
    double screen_y, float *local_x, float *local_y) {
    if (!platform || !local_x || !local_y) return false;
    HWND window = native_window(platform);
    POINT point = {(LONG)screen_x, (LONG)screen_y};
    RECT client;
    if (!window || !ScreenToClient(window, &point) || !GetClientRect(window, &client))
        return false;
    *local_x = (float)point.x; *local_y = (float)point.y;
    return point.x >= client.left && point.x < client.right &&
        point.y >= client.top && point.y < client.bottom;
}

void bongo_cat_platform_set_click_through(BongoCatPlatform *platform,
    bool forced, bool pointer_transparent) {
    HWND window = native_window(platform);
    bongo_cat_windows_layered_set_click_through(platform, forced);
    bongo_cat_windows_borderless_set_click_through(window, forced,
        pointer_transparent);
}

void bongo_cat_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    SDL_ShowWindow(window);
    HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!handle) return;
    HWND proxy = bongo_cat_windows_layered_proxy(handle);
    if (IsIconic(handle)) ShowWindow(handle, SW_RESTORE);
    BringWindowToTop(handle);
    SetForegroundWindow(handle);
    if (proxy) BringWindowToTop(proxy);
    /* Preferences windows are transparent too, but are not OBS capture
       sources. Running the capture style transaction on them can recreate the
       DWM surface and reintroduce an opaque edge. Only configure windows that
       were explicitly registered as capture sources. */
    if (bongo_cat_windows_capture_is_configured(handle))
        bongo_cat_windows_capture_configure(handle);
    else
        bongo_cat_windows_capture_repair_transparency(handle);
}

bool bongo_cat_platform_set_geometry(BongoCatPlatform *platform,
    int x, int y, int width, int height) {
    if (!platform || !platform->window) return false;
    int current_width, current_height;
    bool size_known = SDL_GetWindowSize(platform->window,
        &current_width, &current_height);
    int current_x, current_y;
    bool position_known = SDL_GetWindowPosition(platform->window,
        &current_x, &current_y);
    bool size_changed = !size_known || current_width != width || current_height != height;
    bool position_changed = !position_known || current_x != x || current_y != y;
    if (!size_changed && !position_changed) return true;
    HWND window = native_window(platform);
    if (!window) return false;
    if (size_changed &&
        !(SDL_GetWindowFlags(platform->window) & SDL_WINDOW_RESIZABLE) &&
        !SDL_SetWindowResizable(platform->window, true))
        return false;
    bool changed = SetWindowPos(window, NULL, position_changed ? x : current_x,
        position_changed ? y : current_y, size_changed ? width : current_width,
        size_changed ? height : current_height,
        SWP_NOZORDER | SWP_NOACTIVATE) != 0;
    if (!changed) return false;
    bool synced = SDL_SyncWindow(platform->window);
    if (synced) bongo_cat_windows_capture_repair_transparency(window);
    return synced;
}
#endif
