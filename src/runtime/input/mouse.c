#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static void audit_mouse(BongoCatApp *app, double x, double y) {
    if (!app->smoke_input_audit) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->state_root, "input-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "ab");
    if (!file) return;
    fprintf(file, "mouse x=%.2f y=%.2f\n", x, y);
    fclose(file);
}

void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y) {
    int window_x, window_y, width, height;
    SDL_GetWindowPosition(app->window, &window_x, &window_y);
    SDL_GetWindowSize(app->window, &width, &height);
    bool inside = x >= window_x && x <= window_x + width &&
        y >= window_y && y <= window_y + height;
    app->pointer_known = true;
    app->pointer_x = x;
    app->pointer_y = y;
    bongo_cat_window_schedule_pointer_hit(app);
    if (inside == app->hover_inside) return;
    app->hover_inside = inside;
    app->hover_deadline_ns = inside ? SDL_GetTicksNS() +
        (uint64_t)(app->settings.window.hide_delay_seconds * 1000000000.0) : 0;
    if (!inside && app->hover_hidden) {
        bongo_cat_platform_set_opacity(&app->platform,
            app->session.window.opacity_percent / 100.0f);
        app->hover_hidden = false;
        bongo_cat_window_sync_click_through(app);
    }
}

void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now) {
    if (!app->settings.window.hide_on_hover || !app->hover_inside || app->hover_hidden ||
        !app->hover_deadline_ns || now < app->hover_deadline_ns) return;
    bongo_cat_platform_set_opacity(&app->platform, 0.0f);
    app->hover_hidden = true;
    bongo_cat_window_sync_click_through(app);
}

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

static void log_mouse_diagnostics(BongoCatApp *app, uint64_t now,
    bool received, double target_x, double target_y,
    float global_x, float global_y, SDL_MouseButtonFlags buttons,
    bool cursor_locked, bool relative_requested) {
    bool hook_active = app->mouse_last_ns != 0;
    bool mismatch = hook_active &&
        (target_x != global_x || target_y != global_y);
    if (app->mouse_diagnostic_due_ns &&
        now < app->mouse_diagnostic_due_ns) return;
    app->mouse_diagnostic_due_ns = now +
        (mismatch || relative_requested ? 30000000000ull : 60000000000ull);
    uint64_t hook_age_ms = app->mouse_last_ns && now >= app->mouse_last_ns
        ? (now - app->mouse_last_ns) / 1000000ull : 0;
    SDL_Log("[input] Pointer pipeline: source=%s sample_now=%d "
        "samples=%llu age_ms=%llu selected=%.1f,%.1f sdl=%.1f,%.1f "
        "mismatch=%d sdl_buttons=0x%x model_buttons=%d,%d,%d "
        "pointer_known=%d cursor_locked=%d relative_requested=%d relative_device=%d "
        "dropped=%llu model=%s mode=%d ignore=%d centered=%d",
        hook_active ? "native-hook" : "sdl-fallback", received,
        (unsigned long long)app->mouse_hook_samples,
        (unsigned long long)hook_age_ms, target_x, target_y,
        global_x, global_y, mismatch, (unsigned)buttons,
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down,
        app->pointer_known, cursor_locked, relative_requested,
        app->platform.relative_pointer != NULL,
        (unsigned long long)atomic_load_explicit(&app->input.dropped,
            memory_order_relaxed),
        app->loaded_model[0] ? app->loaded_model : "none",
        (int)app->loaded_mode, app->settings.model.ignore_mouse,
        app->settings.model.mouse_centered);
}

static void log_pointer_mode_change(const BongoCatApp *app,
    bool cursor_locked, bool profile_relative, bool relative_requested,
    bool map_requested, bool map_ok, double target_x, double target_y,
    double model_x, double model_y, bool model_moved) {
    const char *reason = relative_requested
        ? (cursor_locked ? "cursor-locked" : "model-profile")
        : (cursor_locked ? "mouse-disabled" : "cursor-free");
    SDL_Log("[input] Pointer mode changed: mode=%s reason=%s lock=%d "
        "profile=%d map_requested=%d map_ok=%d relative_device=%d "
        "target=%.1f,%.1f virtual=%.1f,%.1f "
        "moved=%d model=%s",
        relative_requested ? "relative" : "absolute", reason,
        cursor_locked, profile_relative, map_requested, map_ok,
        app->platform.relative_pointer != NULL, target_x, target_y,
        model_x, model_y, model_moved,
        app->loaded_model[0] ? app->loaded_model : "none");
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
    bool moved = !app->pointer_known || app->pointer_x != target_x ||
        app->pointer_y != target_y;
    if (moved) {
        audit_mouse(app, target_x, target_y);
        bongo_cat_app_track_hover(app, target_x, target_y);
        // Refresh the hit pixel before the next button press. Waiting for the
        // scheduled frame can leave a stale transparent state for fast clicks.
        if (app->click_through_applied && !app->left_mouse_down &&
            !app->right_mouse_down)
            bongo_cat_window_capture_pointer_hit(app);
    }
    bongo_cat_window_sync_click_through(app);
    double model_x = target_x, model_y = target_y;
    bool model_moved = moved;
    bool profile_relative = app->model_render_options.mver_projection &&
        app->model_render_options.mouse_force_move;
    bool relative_requested = !app->settings.model.ignore_mouse &&
        (profile_relative || cursor_locked);
    bool cursor_lock_changed = cursor_locked != app->pointer_cursor_locked;
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
        log_pointer_mode_change(app, cursor_locked, profile_relative,
            relative_requested, map_pointer, map_ok, target_x, target_y,
            model_x, model_y, model_moved);
    log_mouse_diagnostics(app, now, received, target_x, target_y,
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
