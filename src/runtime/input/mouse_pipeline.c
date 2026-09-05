#include "runtime.h"
#include "mouse_diagnostics.h"

#include <SDL3/SDL.h>

/* One frame of application mouse input. This module chooses the authoritative
   pointer source, reconciles buttons, switches absolute/relative mode, and
   hands normalized coordinates to the mapping module. */
static bool reconcile_button(BongoCatApp *app, bool *current, bool pressed,
    const char *parameter) {
    if (*current == pressed) return false;
    *current = pressed;
    bongo_cat_live2d_set_parameter(app->live2d, parameter,
        pressed ? 1.0f : 0.0f);
    if (!pressed) bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
    return true;
}

void bongo_cat_app_apply_mouse(BongoCatApp *app) {
    if (!app) return;
    double target_x, target_y;
    bool received = bongo_cat_input_take_mouse(&app->input, &target_x, &target_y);
    uint64_t now = SDL_GetTicksNS();
    if (received) {
        app->mouse_last_ns = now ? now : 1;
        app->mouse_hook_samples++;
    }
    float global_x = 0.0f, global_y = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&global_x, &global_y);
    bool button_event_pending = app->mouse_button_event_pending;
    bool cursor_locked = bongo_cat_platform_pointer_locked(&app->platform);
    bool cursor_lock_changed = cursor_locked != app->pointer_cursor_locked;
    bool button_changed = false;
    if (!button_event_pending && !cursor_locked) {
        button_changed = reconcile_button(app, &app->left_mouse_down,
            (buttons & SDL_BUTTON_LMASK) != 0, "ParamMouseLeftDown");
        button_changed = reconcile_button(app, &app->right_mouse_down,
            (buttons & SDL_BUTTON_RMASK) != 0, "ParamMouseRightDown") ||
            button_changed;
        bool side_down = (buttons & (SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK)) != 0;
        if (app->side_mouse_down != side_down) {
            app->side_mouse_down = side_down;
            button_changed = true;
            app->dirty = true;
        }
    }
    if (!received && (!app->mouse_last_ns || !app->pointer_known)) {
        target_x = global_x;
        target_y = global_y;
    } else if (!received) {
        target_x = app->pointer_x;
        target_y = app->pointer_y;
    }
    /* Most games recenter the cursor as they enter relative-look mode. Use
       that observed center as the new virtual origin instead of carrying a
       pre-game edge position into the first relative sample. */
    if (cursor_locked && cursor_lock_changed) {
        target_x = global_x;
        target_y = global_y;
    }
    bool moved = !app->pointer_known || app->pointer_x != target_x ||
        app->pointer_y != target_y;
    if (moved) {
        bongo_cat_mouse_audit(app, target_x, target_y);
        bongo_cat_app_track_hover(app, target_x, target_y);
        /* Refresh the hit pixel before the next button press. Waiting for the
           scheduled frame can leave a stale transparent state for fast clicks. */
        if (app->click_through_applied && !app->left_mouse_down &&
            !app->right_mouse_down)
            bongo_cat_window_capture_pointer_hit(app);
    }
    bongo_cat_window_sync_click_through(app);
    double model_x = target_x, model_y = target_y;
    bool model_moved = moved;
    bool profile_relative = app->model_render_options.mver_projection &&
        app->model_render_options.mouse_force_move;
    /* A model may request forced mouse movement, but while the cursor is
       visible and free its hook coordinates are the authoritative screen
       position. Enter relative mode only after another foreground window
       actually confines the cursor; otherwise a missing raw sample would
       leave the virtual point stuck at an old position. */
    bool relative_requested = !app->settings.model.ignore_mouse &&
        cursor_locked;
    bool pointer_mode_changed = false;
    if (relative_requested != app->pointer_relative_active ||
        (relative_requested && cursor_lock_changed)) {
        pointer_mode_changed = true;
        if (relative_requested != app->pointer_relative_active)
            app->mver_pointer = (BongoCatMverPointerState){0};
        if (relative_requested)
            bongo_cat_platform_relative_pointer_reset(&app->platform);
        else bongo_cat_platform_relative_pointer_release(&app->platform);
        app->pointer_relative_active = relative_requested;
    }
    app->pointer_cursor_locked = cursor_locked;
    bool map_pointer = !app->settings.model.ignore_mouse && (cursor_locked ||
        (app->model_render_options.mver_projection &&
            (!app->settings.model.mouse_centered || profile_relative)));
    bool map_ok = !map_pointer;
    if (map_pointer) {
        map_ok = bongo_cat_app_map_pointer(app, relative_requested, target_x,
            target_y, &model_x, &model_y, &model_moved);
        if (!map_ok) {
            model_x = target_x; model_y = target_y; model_moved = moved;
        }
    }
    if (pointer_mode_changed)
        bongo_cat_mouse_log_mode_change(app, cursor_locked, profile_relative,
            relative_requested, map_pointer, map_ok, target_x, target_y,
            model_x, model_y, model_moved);
    bongo_cat_mouse_log_diagnostics(app, now, received, target_x, target_y,
        global_x, global_y, buttons, cursor_locked, relative_requested);
    if (app->settings.model.ignore_mouse ||
        (!model_moved && !button_changed && !pointer_mode_changed &&
            !button_event_pending)) {
        app->mouse_button_event_pending = false;
        return;
    }
    app->input_mouse_updates++;
    bongo_cat_app_apply_mouse_coordinates(app, model_x, model_y,
        cursor_locked ? model_x : target_x, cursor_locked ? model_y : target_y);
    app->mouse_button_event_pending = false;
}
