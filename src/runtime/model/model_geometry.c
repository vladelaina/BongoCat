#include "model_geometry.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>

/* Share the aspect policy between texture planning and the window commit.
   The incoming width must never come from the outgoing model's window. */
static bool content_size(const BongoCatLive2DRenderOptions *options,
    int canvas_width, int canvas_height, int requested_height,
    int *width, int *height) {
    int reference_width = canvas_width > 0 ? canvas_width : 612;
    int reference_height = canvas_height > 0 ? canvas_height : 354;
    if (options && options->mver_projection) {
        reference_width = options->reference_width;
        reference_height = options->reference_height;
    }
    if (reference_width <= 0 || reference_height <= 0 || requested_height <= 0)
        return false;
    double scaled_width = (double)requested_height * reference_width / reference_height;
    *width = (int)SDL_clamp(scaled_width + 0.5, 64.0, 8192.0);
    *height = SDL_clamp(requested_height, 64, 8192);
    return true;
}

bool bongo_cat_model_texture_display_size(void *userdata,
    const BongoCatLive2DRenderOptions *options, int canvas_width,
    int canvas_height, int *display_width, int *display_height) {
    BongoCatApp *app = userdata;
    int width = 0, height = 0, pixels_w = 0, pixels_h = 0;
    if (!app || !app->window || !display_width || !display_height ||
        !SDL_GetWindowSize(app->window, &width, &height) ||
        !SDL_GetWindowSizeInPixels(app->window, &pixels_w, &pixels_h) ||
        width <= 0 || height <= 0 || pixels_w <= 0 || pixels_h <= 0) return false;
    int content_width = 0, content_height = 0;
    int requested_height = app->session.window.content_height > 0
        ? app->session.window.content_height : height;
    if (!content_size(options, canvas_width, canvas_height, requested_height,
        &content_width, &content_height)) return false;
    /* Account for high-DPI windows. Later monitor changes use the ordinary
       background resolution refresh once the new pixel size is known. */
    double target_width = ceil((double)content_width * pixels_w / width);
    double target_height = ceil((double)content_height * pixels_h / height);
    if (target_width < 1.0 || target_height < 1.0 ||
        target_width > INT_MAX || target_height > INT_MAX) return false;
    *display_width = (int)target_width;
    *display_height = (int)target_height;
    return true;
}

BongoCatModelContentAnchor bongo_cat_model_content_anchor(BongoCatApp *app) {
    BongoCatModelContentAnchor anchor = {0};
    int window_x = 0, window_y = 0;
    if (!app || !app->window ||
        !SDL_GetWindowPosition(app->window, &window_x, &window_y)) return anchor;
    int left = 0, top = 0, ignored_width = 0, ignored_height = 0;
    int content_width = app->session.window.content_width > 0
        ? app->session.window.content_width : app->session.window.width;
    int content_height = app->session.window.content_height > 0
        ? app->session.window.content_height : app->session.window.height;
    if (!bongo_cat_window_frame_size(app, content_width, content_height,
            &ignored_width, &ignored_height, &left, &top)) return anchor;
    anchor.x = window_x + left + content_width / 2;
    anchor.y = window_y + top + content_height / 2;
    anchor.valid = true;
    return anchor;
}

bool bongo_cat_model_apply_aspect(BongoCatApp *app,
    const BongoCatLive2DRenderOptions *options,
    const BongoCatModelContentAnchor *anchor, bool replacing_model) {
    if (!app || !app->window) return false;
    int canvas_width = 0, canvas_height = 0;
    bongo_cat_live2d_canvas_size(app->live2d, &canvas_width, &canvas_height);
    int x, y, width, height;
    if (!SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &width, &height)) return false;
    int requested_height = app->session.window.content_height > 0
        ? app->session.window.content_height : height;
    int content_width = 0, content_height = 0;
    if (!content_size(options, canvas_width, canvas_height, requested_height,
        &content_width, &content_height)) return false;
    int next_width = 0, next_height = 0, left = 0, top = 0;
    if (!bongo_cat_window_frame_size(app, content_width, content_height,
            &next_width, &next_height, &left, &top)) return false;
    bool restored_frame = !replacing_model &&
        SDL_abs(width - next_width) <= 1 && SDL_abs(height - next_height) <= 1;
    int next_x = x, next_y = y;
    if (anchor && anchor->valid && !restored_frame) {
        next_x = anchor->x - left - content_width / 2;
        next_y = anchor->y - top - content_height / 2;
    }
    app->session.window.content_width = content_width;
    app->session.window.content_height = content_height;
    if (next_x == x && next_y == y && next_width == width &&
        next_height == height) return false;
    bool changed = bongo_cat_window_apply_geometry(app, next_x, next_y,
        app->session.window.scale_percent, next_width, next_height);
    if (changed) {
        app->session.window.content_width = content_width;
        app->session.window.content_height = content_height;
    }
    return changed;
}
