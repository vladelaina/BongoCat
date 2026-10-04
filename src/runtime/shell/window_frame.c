#include "runtime.h"
#include "../../live2d/model_frame_policy.h"

#include <limits.h>
#include <math.h>

void bongo_cat_window_store_content_origin(BongoCatApp *app) {
    if (!app || !app->loaded_model[0] || app->loading_model[0]) return;
    int width = 0, height = 0, left = 0, top = 0;
    if (bongo_cat_window_frame_size(app, app->session.window.content_width,
        app->session.window.content_height, &width, &height, &left, &top)) {
        app->session.window.content_left = left;
        app->session.window.content_top = top;
    }
}

/* Transparent pixels still cost compositor/readback bandwidth. At most double
   the content area, with a 4 MP target and an 8192-pixel dimension ceiling.
   These constrain automatic growth, not a user's explicitly chosen size. */
static BongoCatLive2DFrame limit_frame(BongoCatApp *app,
    BongoCatLive2DFrame current, BongoCatLive2DFrame required,
    int content_width, int content_height) {
    int w = 0, h = 0, pw = 0, ph = 0;
    if (content_width <= 0 || content_height <= 0 ||
        !SDL_GetWindowSize(app->window, &w, &h) ||
        !SDL_GetWindowSizeInPixels(app->window, &pw, &ph) ||
        w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return current;
    double cw = (double)content_width * pw / w;
    double ch = (double)content_height * ph / h;
    return bongo_cat_frame_limit(current, required,
        fmin(2.0, 4194304.0 / (cw * ch)),
        fmin(8192.0 / cw, 8192.0 / content_width),
        fmin(8192.0 / ch, 8192.0 / content_height));
}

void bongo_cat_window_limit_initial_frame(BongoCatApp *app,
    int content_width, int content_height) {
    BongoCatLive2DFrame requested = {0}, empty = {0};
    if (!app || !app->window || !bongo_cat_live2d_frame(app->live2d, &requested))
        return;
    BongoCatLive2DFrame limited = limit_frame(app, empty, requested,
        content_width, content_height);
    bongo_cat_live2d_set_frame(app->live2d, &limited);
}

/* Preserve the content origin near a screen edge. If there is insufficient
   space on that side, the shared viewport fits the complete retained envelope
   instead of moving the pet on every new extreme. */
static BongoCatLive2DFrame limit_display(BongoCatApp *app,
    BongoCatLive2DFrame current, BongoCatLive2DFrame required,
    int x, int y, int cw, int ch) {
    (void)app; (void)current; (void)x; (void)y; (void)cw; (void)ch;
    return required;
}

void bongo_cat_window_update_model_frame(BongoCatApp *app) {
    BongoCatLive2DFrame previous, required;
    if (!app || !app->window || !app->loaded_model[0] ||
        !bongo_cat_live2d_frame(app->live2d, &previous)) return;
    /* Capture the old viewport before measurement activates fallback fitting. */
    int vx = 0, vy = 0, vw = 0, vh = 0;
    bongo_cat_live2d_viewport(app->live2d, &vx, &vy, &vw, &vh);
    if (!bongo_cat_live2d_measure_frame(app->live2d, &required)) return;
    /* Ordinary frames require no native geometry/display queries. */
    if (bongo_cat_frame_equal(previous, required)) return;
    uint64_t now = SDL_GetTicksNS();
    bool may_resize = now >= app->frame_geometry_retry_ns &&
        !app->window_snapshot && !app->window_drag_active &&
        !app->drag_candidate && !app->resize_gesture && !app->resize_candidate &&
        !app->wheel_animation_active && !app->wheel_gesture_active;
    int x = 0, y = 0, w = 0, h = 0, pw = 0, ph = 0;
    if (!SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &w, &h) ||
        !SDL_GetWindowSizeInPixels(app->window, &pw, &ph) ||
        w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return;
    /* Convert a cached gaze anchor back into content coordinates. Its screen
       position must not jump just because the transparent frame changed. */
    bool anchor_ready = app->model_pointer_anchor_ready && vw > 0 && vh > 0;
    double anchor_x = anchor_ready ? (app->model_pointer_anchor_x * pw - vx) / vw : 0;
    double anchor_y = anchor_ready ?
        ((1.0 - app->model_pointer_anchor_y) * ph - vy) / vh : 0;
    int cw = 0, ch = 0;
    bongo_cat_window_content_size(app, w, h, &cw, &ch);
    if (may_resize && cw > 0 && ch > 0 &&
        !bongo_cat_frame_equal(previous, required)) {
        BongoCatLive2DFrame next = limit_display(app, previous, required, x, y, cw, ch);
        next = limit_frame(app, previous, next, cw, ch);
        if (!bongo_cat_frame_equal(previous, next)) {
            bool flip = app->settings.model.vertical_flip;
            double next_x = (double)x + round(cw * (double)previous.left) -
                round(cw * (double)next.left);
            double next_y = (double)y + round(ch * (double)(flip ? previous.bottom : previous.top)) -
                round(ch * (double)(flip ? next.bottom : next.top));
            int next_w = (int)lround(cw * (1.0 + next.left + next.right));
            int next_h = (int)lround(ch * (1.0 + next.top + next.bottom));
            if (next_x >= INT_MIN && next_x <= INT_MAX &&
                next_y >= INT_MIN && next_y <= INT_MAX) {
                bongo_cat_live2d_set_frame(app->live2d, &next);
                bool applied = bongo_cat_window_apply_geometry(app, (int)next_x, (int)next_y,
                    app->session.window.scale_percent, next_w, next_h);
                /* SDL size requests can be asynchronous on macOS/X11. Resolve
                   the actual surface before committing its content transform. */
                if (applied) applied = SDL_SyncWindow(app->window);
                int actual_w = w, actual_h = h;
                SDL_GetWindowSize(app->window, &actual_w, &actual_h);
                if (applied && actual_w == next_w && actual_h == next_h) {
                    app->session.window.content_width = cw;
                    app->session.window.content_height = ch;
                    if (SDL_GetWindowSizeInPixels(app->window,
                        &app->resize_pixel_width, &app->resize_pixel_height))
                        app->resize_pending = true;
                    /* Apply once with the GL context current before drawing. */
                    bongo_cat_window_apply_pending_resize(app);
                    SDL_GetWindowSizeInPixels(app->window, &pw, &ph);
                } else {
                    /* Keep fallback fitting if native allocation fails. */
                    bongo_cat_live2d_set_frame(app->live2d, &previous);
                    int actual_x = x, actual_y = y;
                    SDL_GetWindowPosition(app->window, &actual_x, &actual_y);
                    if (actual_w != w || actual_h != h || actual_x != x || actual_y != y) {
                        bongo_cat_window_apply_geometry(app, x, y,
                            app->session.window.scale_percent, w, h);
                        SDL_SyncWindow(app->window);
                        bongo_cat_window_apply_pending_resize(app);
                        SDL_GetWindowSizeInPixels(app->window, &pw, &ph);
                    }
                    app->frame_geometry_retry_ns = now + 1000000000ull;
                }
            }
        }
    }
    int nx = 0, ny = 0, nw = 0, nh = 0;
    if (anchor_ready && pw > 0 && ph > 0 &&
        bongo_cat_live2d_viewport(app->live2d, &nx, &ny, &nw, &nh)) {
        app->model_pointer_anchor_x = (float)((nx + anchor_x * nw) / pw);
        app->model_pointer_anchor_y = (float)(1.0 - (ny + anchor_y * nh) / ph);
        app->model_pointer_anchor_ready = true;
    }
}
