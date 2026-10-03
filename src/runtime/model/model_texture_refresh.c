#include "runtime.h"
#include "bongo_cat/resource_trace.h"

void bongo_cat_app_refresh_texture_resolution(BongoCatApp *app, bool allow_start) {
    if (!app || !app->running || !app->window || !app->gl_context ||
        app->loading_model[0]) return;
    bongo_cat_resource_trace_resize_observe(app->session.window.width,
        app->session.window.height,
        app->resize_gesture || app->resize_candidate || app->wheel_animation_active,
        bongo_cat_live2d_texture_refresh_pending(app->live2d, true),
        app->window_snapshot != NULL);
    /* A deliberate hide/minimize can last indefinitely. Release unfinished
       replacements now; the short pause policy is only for transient gestures
       and hover hiding. Cleanup is still serviced while the pet is invisible. */
    if (!app->session.window.visible || app->window_minimized)
        bongo_cat_live2d_cancel_texture_refresh(app->live2d);
    bool active = app->session.window.visible && !app->window_minimized &&
        !app->hover_hidden && !app->startup_visibility_pending &&
        !app->resize_gesture && !app->resize_candidate &&
        !app->wheel_animation_active && !app->window_drag_active &&
        !app->window_snapshot;
    if (!bongo_cat_live2d_texture_refresh_due(app->live2d, active, allow_start)) return;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool switch_context = previous_window != app->window ||
        previous_context != app->gl_context;
    if (switch_context && !SDL_GL_MakeCurrent(app->window, app->gl_context)) return;
    const char *previous_phase = bongo_cat_diagnostics_phase("texture-refresh");
    if (bongo_cat_live2d_refresh_textures(app->live2d, active, allow_start)) {
        app->dirty = true;
        bongo_cat_window_mark_hit_dirty(app);
    }
    bongo_cat_diagnostics_phase(previous_phase);
    if (switch_context && previous_window && previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
}
