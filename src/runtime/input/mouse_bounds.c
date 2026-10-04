#include "runtime.h"
#include "mouse_internal.h"

#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static bool mver_pointer_bounds(const BongoCatApp *app, SDL_Rect *bounds) {
    const BongoCatLive2DRenderOptions *options = &app->model_render_options;
    if (!options->mver_projection || !bounds) return false;
    if (options->custom_pointer_bounds) {
        *bounds = (SDL_Rect){options->pointer_left, options->pointer_top,
            options->pointer_right - options->pointer_left,
            options->pointer_bottom - options->pointer_top};
        return bounds->w > 0 && bounds->h > 0;
    }
#ifdef _WIN32
    typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextFn)(
        DPI_AWARENESS_CONTEXT);
    FARPROC set_thread_dpi_proc = GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    SetThreadDpiAwarenessContextFn set_thread_dpi = NULL;
    memcpy(&set_thread_dpi, &set_thread_dpi_proc, sizeof(set_thread_dpi));
    DPI_AWARENESS_CONTEXT previous = set_thread_dpi
        ? set_thread_dpi(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) : NULL;
    RECT desktop = {0};
    bool available = GetWindowRect(GetDesktopWindow(), &desktop) != FALSE;
    if (previous && set_thread_dpi) set_thread_dpi(previous);
    if (available && desktop.right > 0 && desktop.bottom > 0) {
        /* Mver 0.1.6 uses a zero origin and stores right/bottom as dimensions. */
        *bounds = (SDL_Rect){0, 0, desktop.right, desktop.bottom};
        return true;
    }
#endif
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    SDL_Rect display;
    if (!primary || !SDL_GetDisplayBounds(primary, &display)) return false;
    *bounds = (SDL_Rect){0, 0, display.w, display.h};
    return bounds->w > 0 && bounds->h > 0;
}

static float clamp_ratio(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static bool model_pointer_center(BongoCatApp *app, double *x, double *y) {
    int window_x, window_y, width, height;
    if (!app || !app->window || !x || !y ||
        !SDL_GetWindowPosition(app->window, &window_x, &window_y) ||
        !SDL_GetWindowSize(app->window, &width, &height) ||
        width <= 0 || height <= 0) return false;
    if (!app->model_pointer_anchor_ready) {
        BongoCatLive2DVisualState state = {0};
        if (bongo_cat_live2d_visual_state(app->live2d, &state) && state.visible) {
            float center_x = (state.visible_min_x + state.visible_max_x) * 0.5f;
            float center_y = (state.visible_min_y + state.visible_max_y) * 0.5f;
            if (isfinite(center_x) && isfinite(center_y)) {
                app->model_pointer_anchor_x = clamp_ratio(center_x * 0.5f + 0.5f);
                app->model_pointer_anchor_y = clamp_ratio(0.5f - center_y * 0.5f);
                app->model_pointer_anchor_ready = true;
            }
        }
    }
    float anchor_x = app->model_pointer_anchor_ready
        ? app->model_pointer_anchor_x : 0.5f;
    float anchor_y = app->model_pointer_anchor_ready
        ? app->model_pointer_anchor_y : 0.5f;
    *x = window_x + (double)width * anchor_x;
    *y = window_y + (double)height * anchor_y;
    return true;
}

static bool model_pointer_bounds(BongoCatApp *app, SDL_Rect *bounds,
    double *center_x, double *center_y) {
    double visible_center_x, visible_center_y;
    if (!bounds || !model_pointer_center(app, &visible_center_x,
        &visible_center_y)) return false;
    if (center_x) *center_x = visible_center_x;
    if (center_y) *center_y = visible_center_y;
    SDL_Point point = {(int)visible_center_x, (int)visible_center_y};
    SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
    return display && SDL_GetDisplayBounds(display, bounds) &&
        bounds->w > 0 && bounds->h > 0;
}

bool bongo_cat_app_mouse_projection(BongoCatApp *app, double screen_x,
    double screen_y, BongoCatMouseProjection *projection) {
    if (!app || !projection || !isfinite(screen_x) || !isfinite(screen_y))
        return false;
    *projection = (BongoCatMouseProjection){0};
    SDL_Rect bounds = {0};
    bool centered = app->settings.model.mouse_centered;
    projection->centered = centered && model_pointer_bounds(app, &bounds,
        &projection->center_x, &projection->center_y);
    bool mapped = projection->centered || (!centered &&
        mver_pointer_bounds(app, &bounds));
    if (!mapped) {
        SDL_Point point = {(int)screen_x, (int)screen_y};
        SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
        if (!display || !SDL_GetDisplayBounds(display, &bounds)) return false;
    }
    if (bounds.w <= 0 || bounds.h <= 0) return false;
    projection->bounds = (BongoCatMverPointerBounds){
        bounds.x, bounds.y, bounds.w, bounds.h
    };
    return true;
}
