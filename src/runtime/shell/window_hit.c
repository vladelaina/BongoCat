#include "runtime.h"
#include "bongo_cat/preferences.h"
#include "../../platform/common/gl_readback.h"

#include <SDL3/SDL_opengl.h>

static bool needs_pointer_hit_sample(const BongoCatApp *app) {
    return !bongo_cat_platform_native_hit_test(&app->platform) &&
        bongo_cat_platform_dynamic_hit_supported();
}

static bool visible_at_pointer(BongoCatApp *app, float x, float y, bool pending_frame) {
    if (app->window_snapshot) return bongo_cat_window_snapshot_hit(app, x, y);
    int width, height, pixel_width, pixel_height;
    if (!SDL_GetWindowSize(app->window, &width, &height) ||
        !SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height) ||
        width <= 0 || height <= 0 || pixel_width <= 0 || pixel_height <= 0) return false;
    /* The presented/back-buffer alpha already includes the corner mask.
       Use it as the authority, just like native hit testing and snapshots;
       a second analytic mask would disagree on content margins and AA edges. */
    int pixel_x = SDL_clamp((int)(x * pixel_width / width), 0, pixel_width - 1);
    int pixel_y = pixel_height - 1 -
        SDL_clamp((int)(y * pixel_height / height), 0, pixel_height - 1);
    uint8_t presented_alpha = 0;
    if (!pending_frame && bongo_cat_platform_frame_alpha(&app->platform, pixel_width, pixel_height,
        pixel_x, pixel_y, &presented_alpha)) return presented_alpha > 8;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool switch_context = previous_window != app->window ||
        previous_context != app->gl_context;
    if (switch_context && !SDL_GL_MakeCurrent(app->window, app->gl_context)) return true;
    GLubyte pixel[4] = {0};
    /* Before swap, only GL_BACK describes the frame about to be displayed.
       Falling back just because alpha is zero resurrects pixels from an old
       frame and leaves moving models' empty areas blocking the mouse. */
    bool read = bongo_cat_gl_read_window(pixel_x, pixel_y, 1, 1, pending_frame, pixel);
    if (switch_context && previous_window && previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
    return !read || pixel[3] > 8;
}

bool bongo_cat_window_visible_at_pointer(BongoCatApp *app, float x, float y) {
    return visible_at_pointer(app, x, y, false);
}

void bongo_cat_window_capture_pointer_hit(BongoCatApp *app, bool pending_frame) {
    /* Backends without dynamic hit testing would discard the readback, so the
       alpha sample would only stall the pipeline for every presented frame. */
    if (!app || !app->window || !app->pointer_known ||
        !needs_pointer_hit_sample(app) ||
        app->settings.window.pass_through || app->hover_hidden ||
        app->left_mouse_down || app->right_mouse_down) return;
    float local_x, local_y;
    bool inside = bongo_cat_platform_pointer_local(&app->platform,
        app->pointer_x, app->pointer_y, &local_x, &local_y);
    app->pointer_transparent = inside &&
        !visible_at_pointer(app, local_x, local_y, pending_frame);
    app->pointer_hit_dirty = false;
    app->pointer_hit_deadline_ns = 0;
}

void bongo_cat_window_mark_hit_dirty(BongoCatApp *app) {
    if (!app) return;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = 0;
}

void bongo_cat_window_set_visible(BongoCatApp *app, bool visible) {
    if (!app || !app->window) return;
    if (!visible) bongo_cat_window_resize_end(app);
    app->session.window.visible = visible;
    if (!visible) bongo_cat_window_snapshot_discard(app);
    if (!visible) {
        app->startup_visibility_pending = false;
#if defined(__linux__)
        /* Remember XWayland placement before unmapping the surface. */
        int x = 0, y = 0;
        if (SDL_GetWindowPosition(app->window, &x, &y)) {
            app->session.window.x = x;
            app->session.window.y = y;
            app->session.window.position_known = true;
        }
#endif
        bongo_cat_platform_set_visible(&app->platform, false);
        return;
    }
    if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_MINIMIZED)
        SDL_RestoreWindow(app->window);
    app->window_minimized = false;
    app->hover_hidden = false;
    bongo_cat_app_cancel_hover_fade(app);
    bongo_cat_app_reset_pointer_tracking(app);
    bongo_cat_platform_set_opacity(&app->platform,
        app->session.window.opacity_percent / 100.0f);
#if defined(__linux__)
    /* Restore before clamping so stale off-screen coordinates cannot undo it. */
    if (app->session.window.position_known)
        SDL_SetWindowPosition(app->window, app->session.window.x,
            app->session.window.y);
#endif
    /* Keep the native surface hidden until the first complete frame has been
       submitted. The render loop will reveal it next to that presentation. */
    bongo_cat_platform_set_visible(&app->platform,
        !app->startup_visibility_pending);
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

void bongo_cat_window_raise_when_due(BongoCatApp *app, uint64_t now) {
    if (!app || !app->startup_raise_due_ns ||
        app->startup_visibility_pending || now < app->startup_raise_due_ns) return;
    app->startup_raise_due_ns = 0;
    bongo_cat_window_set_visible(app, true);
    bongo_cat_platform_raise_window(app->window);
}

void bongo_cat_window_schedule_pointer_hit(BongoCatApp *app) {
    if (!app || !needs_pointer_hit_sample(app)) return;
    uint64_t deadline = SDL_GetTicksNS() + 8000000ull;
    if (!app->pointer_hit_dirty) {
        app->pointer_hit_dirty = true;
        app->pointer_hit_deadline_ns = deadline;
    } else if (app->pointer_hit_deadline_ns &&
        app->pointer_hit_deadline_ns > deadline) {
        app->pointer_hit_deadline_ns = deadline;
    }
}

void bongo_cat_window_schedule_hit_check(BongoCatApp *app) {
    if (!app || app->pointer_hit_dirty || !app->pointer_known ||
        !needs_pointer_hit_sample(app)) return;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = SDL_GetTicksNS() + 100000000ull;
}

void bongo_cat_window_sync_click_through(BongoCatApp *app) {
    if (!app || !app->window) return;
    bool forced = app->settings.window.pass_through || app->hover_hidden;
    if (forced) bongo_cat_window_resize_end(app);
    if (forced && app->window_snapshot) bongo_cat_window_snapshot_end(app);
    if (!forced && !needs_pointer_hit_sample(app)) {
        app->pointer_transparent = false;
        app->pointer_hit_dirty = false;
        app->pointer_hit_deadline_ns = 0;
    }
    if (!forced && (app->left_mouse_down || app->right_mouse_down)) return;
    if (!forced && app->session.window.visible && app->pointer_known &&
        app->pointer_hit_dirty &&
        (!app->pointer_hit_deadline_ns || SDL_GetTicksNS() >= app->pointer_hit_deadline_ns)) {
        float local_x, local_y;
        bool inside = bongo_cat_platform_pointer_local(&app->platform,
            app->pointer_x, app->pointer_y, &local_x, &local_y);
        app->pointer_transparent = inside && !bongo_cat_window_visible_at_pointer(app,
            local_x, local_y);
        app->pointer_hit_dirty = false;
        app->pointer_hit_deadline_ns = 0;
    }
    bool pointer_transparent = !forced && app->pointer_known && app->pointer_transparent;
    bool forced_changed = !app->click_through_valid ||
        app->click_through_forced_applied != forced;
    if (app->click_through_valid && !forced_changed &&
        app->click_through_applied == pointer_transparent) return;
    bongo_cat_platform_set_click_through(&app->platform, forced, pointer_transparent);
    app->click_through_applied = pointer_transparent;
    app->click_through_forced_applied = forced;
    app->click_through_valid = true;
    if (forced_changed) app->dirty = true;
}

void bongo_cat_window_apply_pending_resize(BongoCatApp *app) {
    if (!app) return;
    if (app->window_snapshot) return;
    if (app->wheel_animation_active || app->resize_gesture) {
        if (!app->resize_pending) return;
        app->resize_pending = false;
        app->resize_render_target_pending = true;
        /* Keep the normalized gaze anchor stable throughout the gesture. */
        bongo_cat_live2d_reshape(app->live2d,
            app->resize_pixel_width, app->resize_pixel_height);
        return;
    }
    if (!app->resize_pending && !app->resize_render_target_pending) return;
    app->resize_pending = false;
    if (!app->resize_render_target_pending)
        app->model_pointer_anchor_ready = false;
    app->resize_render_target_pending = false;
    bongo_cat_live2d_resize(app->live2d,
        app->resize_pixel_width, app->resize_pixel_height);
    bongo_cat_window_mark_hit_dirty(app);
}
