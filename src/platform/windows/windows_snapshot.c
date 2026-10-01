#include "windows_snapshot_internal.h"
#include "windows_capture.h"

#include <SDL3/SDL_properties.h>

static HWND native_window(SDL_Window *window) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

void bongo_cat_windows_snapshot_destroy(BongoCatWindowsSnapshot *s) {
    if (!s) return;
    if (s->suppressed) {
        SDL_SetWindowOpacity(s->source, s->opacity);
        bongo_cat_windows_capture_restore_transparency(s->source_handle);
    }
    if (s->handle) {
        ShowWindow(s->handle, SW_HIDE);
        bongo_cat_windows_snapshot_unbind_input(s);
    }
    if (s->texture) SDL_DestroyTexture(s->texture);
    if (s->renderer) SDL_DestroyRenderer(s->renderer);
    if (s->window) SDL_DestroyWindow(s->window);
    SDL_free(s->pixels);
    SDL_free(s);
}

BongoCatWindowsSnapshot *bongo_cat_windows_snapshot_create(SDL_Window *source,
    float opacity) {
    BongoCatWindowsSnapshot *s = SDL_calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->source = source;
    s->source_handle = native_window(source);
    s->opacity = opacity;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    s->desktop = (RECT){x, y, x + width, y + height};
    /* Bound the temporary desktop surface as well as the captured texture. */
    if (width <= 0 || height <= 0 || (size_t)width * height > SNAPSHOT_MAX_PIXELS ||
        !bongo_cat_windows_snapshot_capture(s)) goto failed;
    s->window = SDL_CreateWindow("BongoCat interaction preview", width, height,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT |
        SDL_WINDOW_UTILITY | SDL_WINDOW_NOT_FOCUSABLE);
    if (!s->window) goto failed;
    s->handle = native_window(s->window);
    if (!s->handle) goto failed;
    /* A fixed desktop-sized surface means DWM only sees image changes, never
       a sequence of native window moves/resizes during the gesture. */
    LONG_PTR style = GetWindowLongPtrW(s->handle, GWL_EXSTYLE);
    SetWindowLongPtrW(s->handle, GWL_EXSTYLE, style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    if (!SetWindowPos(s->handle, s->source_handle, x, y, width, height, SWP_NOACTIVATE))
        goto failed;
    s->renderer = SDL_CreateRenderer(s->window, "direct3d11");
    if (!s->renderer) goto failed;
    SDL_SetRenderVSync(s->renderer, 1);
    s->texture = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC, s->width, s->height);
    if (!s->texture || !SDL_UpdateTexture(s->texture, NULL, s->pixels, s->width * 4))
        goto failed;
    /* The framebuffer already contains premultiplied alpha. Replace pixels
       directly, multiplying both RGB and alpha by the window opacity. */
    if (!SDL_SetTextureBlendMode(s->texture, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(s->texture, SDL_SCALEMODE_LINEAR)) goto failed;
    Uint8 alpha = (Uint8)(SDL_clamp(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    SDL_SetTextureColorMod(s->texture, alpha, alpha, alpha);
    SDL_SetTextureAlphaMod(s->texture, alpha);
    if (!bongo_cat_windows_snapshot_bind_input(s)) goto failed;
    if (!bongo_cat_windows_capture_restore_transparency(s->handle) ||
        !bongo_cat_windows_snapshot_present(s)) goto failed;
    ShowWindow(s->handle, SW_SHOWNOACTIVATE);
    SetWindowPos(s->handle, s->source_handle, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    s->has_frame = false;
    if (!bongo_cat_windows_snapshot_present(s)) goto failed;
    bongo_cat_windows_snapshot_pointer(s);
    if (!SDL_SetWindowOpacity(source, 0.0f)) goto failed;
    s->suppressed = true;
    return s;
failed:
    bongo_cat_windows_snapshot_destroy(s);
    return NULL;
}
