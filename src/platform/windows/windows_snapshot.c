#include "windows_snapshot_internal.h"
#include "windows_capture.h"
#include "windows_layered.h"
#include "bongo_cat/log.h"
#include "bongo_cat/resource_trace.h"

#include <SDL3/SDL_properties.h>

static HWND native_window(SDL_Window *window) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

bool bongo_cat_windows_snapshot_available(void) {
    /* SDL's compiled renderer list cannot change during the process lifetime.
       Cache only backend availability; transient capture failures may retry. */
    static int available = -1;
    if (available >= 0) return available != 0;
    available = 0;
    int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; ++i) {
        const char *name = SDL_GetRenderDriver(i);
        if (name && SDL_strcmp(name, "direct3d11") == 0) {
            available = 1;
            break;
        }
    }
    if (!available)
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[render] Interaction snapshot disabled: SDL Direct3D 11 renderer "
            "is unavailable; using live rendering");
    return available != 0;
}

void bongo_cat_windows_snapshot_destroy(BongoCatWindowsSnapshot *s) {
    if (!s) return;
    bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_SNAPSHOT, "before-release",
        "ready=%d pixels=%dx%d cpu_mib=%.2f texture=%d renderer=%d window=%d",
        s->ready, s->width, s->height, (double)s->pixel_bytes / (1024.0 * 1024.0),
        s->texture != NULL, s->renderer != NULL, s->window != NULL);
    uint64_t cleanup_started = SDL_GetTicksNS();
    bool ready = s->ready;
    if (s->suppressed && !bongo_cat_windows_layered_suppressed(s->source_handle)) {
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
    bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_SNAPSHOT,
        ready ? "released" : "create-failed", "cleanup_ms=%.1f",
        (double)(SDL_GetTicksNS() - cleanup_started) / 1000000.0);
}

BongoCatWindowsSnapshot *bongo_cat_windows_snapshot_create(SDL_Window *source,
    float opacity) {
    if (!bongo_cat_windows_snapshot_available()) {
        SDL_SetError("SDL Direct3D 11 renderer is unavailable");
        return NULL;
    }
    bongo_cat_resource_trace_begin(BONGO_CAT_RESOURCE_SNAPSHOT, "interaction-preview");
    BongoCatWindowsSnapshot *s = SDL_calloc(1, sizeof(*s));
    if (!s) {
        bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_SNAPSHOT, "allocation-failed", NULL);
        return NULL;
    }
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
    /* SDL_UpdateTexture has consumed RGBA. Hit testing needs only alpha, so
       compact it in place and release the other three bytes per pixel. A
       failed shrink keeps a valid packed buffer for the same hit-test result. */
    size_t pixel_count = (size_t)s->width * s->height;
    for (size_t i = 0; i < pixel_count; ++i) s->pixels[i] = s->pixels[i * 4 + 3];
    unsigned char *alpha = SDL_realloc(s->pixels, pixel_count);
    if (alpha) { s->pixels = alpha; s->pixel_bytes = pixel_count; }
    /* The framebuffer already contains premultiplied alpha. Replace pixels
       directly, multiplying both RGB and alpha by the window opacity. */
    if (!SDL_SetTextureBlendMode(s->texture, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(s->texture, SDL_SCALEMODE_LINEAR)) goto failed;
    Uint8 opacity_alpha = (Uint8)(SDL_clamp(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    SDL_SetTextureColorMod(s->texture, opacity_alpha, opacity_alpha, opacity_alpha);
    SDL_SetTextureAlphaMod(s->texture, opacity_alpha);
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
    s->ready = true;
    bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_SNAPSHOT, "ready",
        "pixels=%dx%d cpu_mib=%.2f texture_rgba_est_mib=%.2f desktop_surface=%dx%d alpha_compacted=%d",
        s->width, s->height, (double)s->pixel_bytes / (1024.0 * 1024.0),
        (double)pixel_count * 4 / (1024.0 * 1024.0), width, height, alpha != NULL);
    return s;
failed:
    bongo_cat_windows_snapshot_destroy(s);
    return NULL;
}
