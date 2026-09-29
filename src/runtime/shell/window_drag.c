#include "runtime.h"
#include "modal_frame.h"

static int rounded_delta(float value) {
    return value < 0.0f ? (int)(value - 0.5f) : (int)(value + 0.5f);
}

static bool use_pointer_drag(const BongoCatApp *app) {
    (void)app;
    return true;
}

static void move_with_pointer(BongoCatApp *app, float x, float y) {
    int next_x = app->drag_window_x + rounded_delta(x - app->drag_start_x);
    int next_y = app->drag_window_y + rounded_delta(y - app->drag_start_y);
    bongo_cat_window_drag_to(app, next_x, next_y);
}

void bongo_cat_window_drag_begin(BongoCatApp *app,
    const SDL_MouseButtonEvent *event) {
    if (!app || !event) return;
    bongo_cat_window_cancel_wheel_animation(app);
    app->drag_candidate = bongo_cat_window_visible_at_pointer(
        app, event->x, event->y);
    if (!app->drag_candidate || !SDL_GetWindowPosition(app->window,
        &app->drag_window_x, &app->drag_window_y)) {
        app->drag_candidate = false; return;
    }
    SDL_GetGlobalMouseState(&app->drag_start_x, &app->drag_start_y);
}

void bongo_cat_window_drag_motion(BongoCatApp *app,
    const SDL_MouseMotionEvent *event) {
    if (!app || !event || (!app->drag_candidate && !app->window_drag_active)) return;
    float pointer_x = 0.0f, pointer_y = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
    if (!(buttons & SDL_BUTTON_LMASK)) {
        bongo_cat_window_drag_end(app); return;
    }
    if (app->window_drag_active) {
        if (use_pointer_drag(app))
            move_with_pointer(app, pointer_x, pointer_y);
        return;
    }
    float x = pointer_x - app->drag_start_x;
    float y = pointer_y - app->drag_start_y;
    if (x * x + y * y < 9.0f) return;
    app->drag_candidate = false;
    app->window_drag_active = true;
    bongo_cat_window_snapshot_begin(app);
    if (use_pointer_drag(app)) {
        if (!SDL_CaptureMouse(true)) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Mouse capture is unavailable during window drag: %s",
            SDL_GetError());
        move_with_pointer(app, pointer_x, pointer_y);
    } else {
        BongoCatModalFrame modal_frame;
        bongo_cat_modal_frame_init(&modal_frame, app);
        bongo_cat_platform_begin_drag(&app->platform,
            bongo_cat_modal_frame_tick, &modal_frame);
    }
}

void bongo_cat_window_drag_end(BongoCatApp *app) {
    if (!app) return;
    bool was_active = app->window_drag_active;
    if (was_active) SDL_CaptureMouse(false);
    app->drag_candidate = false;
    app->window_drag_active = false;
    bongo_cat_window_snapshot_end(app);
    bongo_cat_window_drag_bounds_clear(app);
    if (was_active && app->settings.model.mouse_centered) {
        float pointer_x = 0.0f, pointer_y = 0.0f;
        SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
        bongo_cat_app_apply_mouse_position(app, pointer_x, pointer_y, 0.0f);
    }
}
