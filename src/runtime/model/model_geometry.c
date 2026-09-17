#include "model_geometry.h"

#include <SDL3/SDL.h>

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
    int reference_width = 612;
    int reference_height = 354;
    if (options && options->mver_projection) {
        reference_width = options->reference_width;
        reference_height = options->reference_height;
    } else {
        int canvas_width = 0, canvas_height = 0;
        if (bongo_cat_live2d_canvas_size(app->live2d,
            &canvas_width, &canvas_height)) {
            reference_width = canvas_width;
            reference_height = canvas_height;
        }
    }
    int x, y, width, height;
    if (reference_width <= 0 || reference_height <= 0 ||
        !SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &width, &height)) return false;
    int content_height = app->session.window.content_height > 0
        ? app->session.window.content_height : height;
    int content_width = (int)((double)content_height * reference_width /
        reference_height + 0.5);
    if (content_width < 64) content_width = 64;
    if (content_width > 8192) content_width = 8192;
    if (content_height < 64) content_height = 64;
    if (content_height > 8192) content_height = 8192;
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
    /* scale_percent is anchored to the default content height (354px), so
       recompute it from the content height just derived. This keeps the scale
       factor in sync with the geometry whenever a model load changes the size;
       otherwise a stale factor keeps the old, possibly drifted value. */
    float scale = 100.0f * (float)content_height /
        BONGO_CAT_DEFAULT_WINDOW_HEIGHT;
    bool scale_drifted =
        SDL_fabsf(scale - app->session.window.scale_percent) > 0.01f;
    if (next_x == x && next_y == y && next_width == width &&
        next_height == height && !scale_drifted) return false;
    bool changed = bongo_cat_window_apply_geometry(app, next_x, next_y,
        scale, next_width, next_height);
    if (changed) {
        app->session.window.content_width = content_width;
        app->session.window.content_height = content_height;
    }
    return changed;
}
