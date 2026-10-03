#include "runtime.h"
#include "mouse_internal.h"

#include <SDL3/SDL.h>

/* Select screen input, then map it independently for the model. */
#ifndef _WIN32
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
#endif

static bool update_pointer_mode(BongoCatApp *app, bool cursor_locked) {
    bool relative = !app->settings.model.ignore_mouse && cursor_locked;
    bool changed = relative != app->pointer_relative_active ||
        (relative && cursor_locked != app->pointer_cursor_locked);
    if (changed) {
        if (relative != app->pointer_relative_active)
            app->mver_pointer = (BongoCatMverPointerState){0};
        if (relative) bongo_cat_platform_relative_pointer_reset(&app->platform);
        else bongo_cat_platform_relative_pointer_release(&app->platform);
        app->pointer_relative_active = relative;
    }
    app->pointer_cursor_locked = cursor_locked;
    return changed;
}

void bongo_cat_app_apply_mouse(BongoCatApp *app) {
    if (!app) return;
    double target_x, target_y;
    bool received = bongo_cat_input_take_mouse(&app->input, &target_x, &target_y);
    uint64_t now = SDL_GetTicksNS();
    if (received) {
        app->mouse_last_ns = now ? now : 1;
    }
    float global_x = 0.0f, global_y = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&global_x, &global_y);
    bool button_event_pending = app->mouse_button_event_pending;
    bool cursor_locked = bongo_cat_platform_pointer_locked(&app->platform);
    bool cursor_lock_changed = cursor_locked != app->pointer_cursor_locked;
    bool button_changed = false;
#ifdef _WIN32
    /* Model buttons belong to Raw Input; cursor polling is position-only. */
    (void)buttons;
#else
    if (!button_event_pending && !cursor_locked) {
        button_changed = reconcile_button(app, &app->left_mouse_down,
            (buttons & SDL_BUTTON_LMASK) != 0, "ParamMouseLeftDown");
        button_changed = reconcile_button(app, &app->right_mouse_down,
            (buttons & SDL_BUTTON_RMASK) != 0, "ParamMouseRightDown") ||
            button_changed;
        app->back_mouse_down = (buttons & SDL_BUTTON_X1MASK) != 0;
        app->forward_mouse_down = (buttons & SDL_BUTTON_X2MASK) != 0;
        bool side_down = app->back_mouse_down || app->forward_mouse_down;
        if (app->side_mouse_down != side_down) {
            app->side_mouse_down = side_down;
            button_changed = true;
            app->dirty = true;
        }
    }
#endif
    bool native_selected = received || (app->pointer_known &&
        bongo_cat_mouse_sample_fresh(now, app->mouse_last_ns));
    /* Native screen updates are optional; SDL supplies the current position. */
    if (!native_selected) {
        target_x = global_x;
        target_y = global_y;
    } else if (!received) {
        target_x = app->pointer_x;
        target_y = app->pointer_y;
    }
    /* Seed relative motion from the observed lock point. */
    if (cursor_locked && cursor_lock_changed) {
        target_x = global_x;
        target_y = global_y;
    }
    bool moved = !app->pointer_known || app->pointer_x != target_x ||
        app->pointer_y != target_y;
    if (moved) {
        bongo_cat_app_track_hover(app, target_x, target_y);
        /* Refresh the hit pixel before the next button press. Waiting for the
           scheduled frame can leave a stale transparent state for fast clicks. */
        if (app->click_through_applied && !app->left_mouse_down &&
            !app->right_mouse_down)
            bongo_cat_window_capture_pointer_hit(app);
    }
    bongo_cat_window_sync_click_through(app);
    bool pointer_mode_changed = update_pointer_mode(app, cursor_locked);
    app->mouse_button_event_pending = false;
    if (app->settings.model.ignore_mouse) return;
    bool relative_requested = app->pointer_relative_active;
    if (!relative_requested && !moved && !button_changed &&
        !pointer_mode_changed && !button_event_pending) return;

    /* Resolve once: tracking limits and model output share the same display,
       custom workarea and centered anchor for this update. */
    BongoCatMouseProjection projection;
    if (!bongo_cat_app_mouse_projection(app, target_x, target_y, &projection))
        return;
    double model_x = target_x, model_y = target_y;
    bool model_moved = moved;
    bool profile_relative = app->model_render_options.mver_projection &&
        app->model_render_options.mouse_force_move;
    bool map_pointer = cursor_locked || (app->model_render_options.mver_projection &&
        (!app->settings.model.mouse_centered || profile_relative));
    if (map_pointer && !bongo_cat_app_map_pointer(app, &projection,
            relative_requested, target_x, target_y, &model_x, &model_y,
            &model_moved)) {
        model_x = target_x; model_y = target_y; model_moved = moved;
    }
    if (model_moved || button_changed || pointer_mode_changed || button_event_pending)
        bongo_cat_app_apply_mouse_coordinates(app, &projection, model_x, model_y,
            cursor_locked ? model_x : target_x, cursor_locked ? model_y : target_y);
}
