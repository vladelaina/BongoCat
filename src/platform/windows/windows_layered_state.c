#include "windows_layered_internal.h"
#include "windows_capture.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <windows.h>

static HWND native_window(BongoCatPlatform *platform) {
    return platform && platform->window ? (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
}

bool bongo_cat_platform_set_opacity(BongoCatPlatform *platform,
    float opacity) {
    if (!platform || !platform->window) return false;
    BongoCatWindowsLayered *value = platform->presenter;
    platform->window_opacity = SDL_clamp(opacity, 0.0f, 1.0f);
    if (value && value->active)
        return !value->has_frame ||
            bongo_cat_windows_layered_update_proxy(platform, value);
    return SDL_SetWindowOpacity(platform->window, platform->window_opacity);
}

bool bongo_cat_platform_frame_alpha(const BongoCatPlatform *platform,
    int width, int height, int x, int y, uint8_t *alpha) {
    const BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    const unsigned char *pixels = value && value->readback_valid ?
        value->readback : value ? value->pixels : NULL;
    if (!value || !value->active || !value->has_frame || !pixels ||
        value->source_width != width || value->source_height != height ||
        !alpha || x < 0 || y < 0 || x >= width || y >= height)
        return false;
    *alpha = pixels[((size_t)y * (size_t)width + (size_t)x) * 4 + 3];
    return true;
}

void bongo_cat_platform_set_visible(BongoCatPlatform *platform,
    bool visible) {
    if (!platform || !platform->window) return;
    BongoCatWindowsLayered *value = platform->presenter;
    if (value) value->visible = visible;
    if (!visible && value && value->proxy) ShowWindow(value->proxy, SW_HIDE);
    if (visible) SDL_ShowWindow(platform->window);
    else SDL_HideWindow(platform->window);
    if (visible) {
        HWND source = native_window(platform);
        bongo_cat_windows_capture_configure(source);
        bongo_cat_windows_capture_repair_transparency(source);
        bongo_cat_windows_capture_log(source, "visible");
    }
}
#endif
