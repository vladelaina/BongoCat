#include "runtime.h"

static int round_position(double value) {
    return (int)(value + (value < 0.0 ? -0.5 : 0.5));
}

bool bongo_cat_window_set_scale(BongoCatApp *app, float scale) {
    if (!app || !app->window) return false;
    int x, y, width, height;
    if (!SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &width, &height)) return false;
    float actual;
    int next_width, next_height;
    if (!bongo_cat_window_scaled_size(width, height,
        app->session.window.scale_percent, scale,
        &actual, &next_width, &next_height)) return false;
    if (actual == app->session.window.scale_percent &&
        next_width == width && next_height == height) return false;
    return bongo_cat_window_apply_scale_centered(app, scale,
        width, height, app->session.window.scale_percent,
        x + width * 0.5f, y + height * 0.5f);
}

bool bongo_cat_window_apply_scale_centered(BongoCatApp *app, float scale,
    int base_width, int base_height, float base_scale,
    float center_x, float center_y) {
    float actual;
    int width, height;
    if (!app || !app->window || !bongo_cat_window_scaled_size(base_width, base_height,
        base_scale, scale, &actual, &width, &height)) return false;
    int x = round_position(center_x - width * 0.5);
    int y = round_position(center_y - height * 0.5);
    SDL_FRect image = {0, 0, base_width * actual / base_scale,
        base_height * actual / base_scale};
    image.x = center_x - image.w * 0.5f;
    image.y = center_y - image.h * 0.5f;
    if (x == app->session.window.x && y == app->session.window.y &&
        width == app->session.window.width && height == app->session.window.height) {
        app->session.window.scale_percent = actual;
        if (app->window_snapshot) {
            bongo_cat_window_snapshot_geometry(app, &image);
            bongo_cat_window_snapshot_present(app);
        }
        return true;
    }
    bongo_cat_window_snapshot_begin(app);
    bongo_cat_window_snapshot_geometry(app, &image);
    if (!bongo_cat_window_apply_geometry(app, x, y, actual, width, height)) {
        bongo_cat_window_snapshot_end(app);
        return false;
    }
    /* Uniform scaling preserves the normalized gaze anchor. Finish drawing
       the resized surface before returning to event/model processing, where
       the compositor could otherwise display the moved previous frame. */
    app->resize_render_target_pending = true;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bongo_cat_app_render_now(app);
    if (previous_window && previous_context &&
        (previous_window != app->window || previous_context != app->gl_context) &&
        !SDL_GL_MakeCurrent(previous_window, previous_context))
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Cannot restore OpenGL context after window scaling: %s", SDL_GetError());
    return true;
}
