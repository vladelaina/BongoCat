#include "runtime.h"

#include <math.h>
#include <stdio.h>

#define WINDOW_MIN_DIMENSION 64
#define WINDOW_MAX_DIMENSION 8192
#define WINDOW_MIN_SCALE 10.0f
#define WINDOW_MAX_SCALE 500.0f

static int round_dimension(double value) {
    return (int)(value + 0.5);
}

static BongoCatLive2DFrame model_frame(BongoCatApp *app) {
    BongoCatLive2DFrame frame = {0};
    if (app && app->live2d) bongo_cat_live2d_frame(app->live2d, &frame);
    if (!isfinite(frame.left) || frame.left < 0.0f) frame.left = 0.0f;
    if (!isfinite(frame.top) || frame.top < 0.0f) frame.top = 0.0f;
    if (!isfinite(frame.right) || frame.right < 0.0f) frame.right = 0.0f;
    if (!isfinite(frame.bottom) || frame.bottom < 0.0f) frame.bottom = 0.0f;
    return frame;
}

bool bongo_cat_window_frame_size(BongoCatApp *app,
    int content_width, int content_height, int *width, int *height,
    int *left, int *top) {
    if (content_width <= 0 || content_height <= 0 || !width || !height)
        return false;
    BongoCatLive2DFrame frame = model_frame(app);
    double frame_width = content_width *
        (1.0 + frame.left + frame.right);
    double frame_height = content_height *
        (1.0 + frame.top + frame.bottom);
    if (!isfinite(frame_width) || !isfinite(frame_height) ||
        frame_width < 1.0 || frame_height < 1.0 ||
        frame_width > WINDOW_MAX_DIMENSION ||
        frame_height > WINDOW_MAX_DIMENSION) return false;
    *width = round_dimension(frame_width);
    *height = round_dimension(frame_height);
    if (left) *left = round_dimension(content_width * frame.left);
    if (top) *top = round_dimension(content_height * frame.top);
    return true;
}

bool bongo_cat_window_content_size(BongoCatApp *app,
    int width, int height, int *content_width, int *content_height) {
    if (width <= 0 || height <= 0 || !content_width || !content_height)
        return false;
    BongoCatLive2DFrame frame = model_frame(app);
    double horizontal = 1.0 + frame.left + frame.right;
    double vertical = 1.0 + frame.top + frame.bottom;
    if (!isfinite(horizontal) || !isfinite(vertical) ||
        horizontal <= 0.0 || vertical <= 0.0) return false;
    *content_width = SDL_max(1, round_dimension(width / horizontal));
    *content_height = SDL_max(1, round_dimension(height / vertical));
    return true;
}

bool bongo_cat_window_scaled_size(int base_width, int base_height, float base_scale,
    float requested_scale, float *actual_scale, int *width, int *height) {
    if (base_width <= 0 || base_height <= 0 || base_scale <= 0.0f ||
        !actual_scale || !width || !height) return false;
    float minimum = SDL_max(WINDOW_MIN_SCALE, SDL_max(
        base_scale * WINDOW_MIN_DIMENSION / base_width,
        base_scale * WINDOW_MIN_DIMENSION / base_height));
    float maximum = SDL_min(WINDOW_MAX_SCALE, SDL_min(
        base_scale * WINDOW_MAX_DIMENSION / base_width,
        base_scale * WINDOW_MAX_DIMENSION / base_height));
    if (minimum > maximum) return false;
    float scale = SDL_clamp(requested_scale, minimum, maximum);
    int next_width = round_dimension((double)base_width * scale / base_scale);
    int next_height = round_dimension((double)base_height * scale / base_scale);
    *actual_scale = scale;
    *width = SDL_clamp(next_width, WINDOW_MIN_DIMENSION, WINDOW_MAX_DIMENSION);
    *height = SDL_clamp(next_height, WINDOW_MIN_DIMENSION, WINDOW_MAX_DIMENSION);
    return true;
}

bool bongo_cat_window_apply_geometry(BongoCatApp *app, int x, int y,
    float scale, int width, int height) {
    if (!app || !app->window || width < WINDOW_MIN_DIMENSION ||
        height < WINDOW_MIN_DIMENSION || width > WINDOW_MAX_DIMENSION ||
        height > WINDOW_MAX_DIMENSION) return false;
    if (!bongo_cat_platform_set_geometry(&app->platform, x, y, width, height))
        return false;
    app->session.window.scale_percent = scale;
    app->session.window.x = x;
    app->session.window.y = y;
    app->session.window.position_known = true;
    app->session.window.width = width;
    app->session.window.height = height;
    bongo_cat_window_content_size(app, width, height,
        &app->session.window.content_width,
        &app->session.window.content_height);
    if (SDL_GetWindowSizeInPixels(app->window,
        &app->resize_pixel_width, &app->resize_pixel_height))
        app->resize_pending = true;
    app->dirty = true;
    bongo_cat_window_mark_hit_dirty(app);
    return true;
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
    return bongo_cat_window_apply_geometry(app, x, y,
        actual, next_width, next_height);
}

void bongo_cat_window_resize_by_pointer(BongoCatApp *app, const SDL_Event *event) {
    bool shift = bongo_cat_input_shift_down(&app->input);
#ifndef _WIN32
    shift = shift || (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
#endif
    /* Match wheel handling: SDL may retain modifiers without keyboard focus. */
    if (!(event->motion.state & SDL_BUTTON_RMASK) || !shift) return;
    bongo_cat_window_cancel_wheel_animation(app);
    if (!app->resize_gesture) {
        if (!SDL_GetWindowSize(app->window,
            &app->resize_base_width, &app->resize_base_height)) return;
        app->resize_scale_start = app->session.window.scale_percent;
        app->resize_scale_target = app->resize_scale_start;
        app->resize_gesture = true;
    }
    float delta = (event->motion.xrel + event->motion.yrel) * 0.5f;
    app->resize_scale_target = SDL_clamp(app->resize_scale_target + delta,
        WINDOW_MIN_SCALE, WINDOW_MAX_SCALE);
    float actual;
    int width, height, x, y;
    if (!bongo_cat_window_scaled_size(app->resize_base_width,
        app->resize_base_height, app->resize_scale_start,
        app->resize_scale_target, &actual, &width, &height) ||
        !SDL_GetWindowPosition(app->window, &x, &y)) return;
    app->resize_scale_target = actual;
    if (!bongo_cat_window_apply_geometry(app, x, y, actual, width, height))
        app->resize_scale_target = app->session.window.scale_percent;
}

bool bongo_cat_window_geometry_self_test(BongoCatApp *app) {
    if (!app || !app->window) return false;
    SDL_SyncWindow(app->window);
    BongoCatWindowPreferences preferences_backup = app->settings.window;
    BongoCatWindowState state_backup = app->session.window;
    int original_x, original_y, original_width, original_height;
    SDL_GetWindowPosition(app->window, &original_x, &original_y);
    SDL_GetWindowSize(app->window, &original_width, &original_height);
    SDL_DisplayID display = SDL_GetDisplayForWindow(app->window);
    SDL_Rect bounds;
    if (!display || !SDL_GetDisplayUsableBounds(display, &bounds)) return false;
    app->settings.window.keep_in_screen = true;
    app->model_pointer_anchor_ready = true;
    bongo_cat_window_apply_geometry(app, bounds.x - 2000, bounds.y - 2000,
        100.0f, 320, 240);
    SDL_SyncWindow(app->window);
    bongo_cat_window_apply_pending_resize(app);
    bongo_cat_window_clamp_to_display(app);
    SDL_SyncWindow(app->window);
    int x, y, width, height;
    SDL_GetWindowPosition(app->window, &x, &y);
    bool clamped = x >= bounds.x && y >= bounds.y;
    bool anchor_reset = !app->model_pointer_anchor_ready;
    bool scaled = bongo_cat_window_set_scale(app, 125.0f);
    SDL_SyncWindow(app->window);
    SDL_GetWindowSize(app->window, &width, &height);
    int scaled_width = width, scaled_height = height;
    scaled = scaled && width == 400 && height == 300;
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_OPACITY_50);
    bool opacity = SDL_fabsf(bongo_cat_platform_get_opacity(
        &app->platform) - 0.5f) < 0.02f;
    app->settings.window.hide_on_hover = true;
    app->settings.window.hide_delay_seconds = 0.0f;
    app->settings.window.hide_fade_seconds = 0.0f;
    app->settings.window.pass_through = true;
    app->settings.window.always_on_top = true;
    app->settings.window.obs_background = true;
    app->settings.window.rounded_corners = false;
    app->session.window.opacity_percent = 100.0f;
    bongo_cat_window_sync_click_through(app);
    bongo_cat_app_render_now(app);
    bongo_cat_app_track_hover(app, x + 10, y + 10);
    bongo_cat_app_update_hover(app, SDL_GetTicksNS() + 1);
    bool hidden = app->hover_hidden &&
        bongo_cat_platform_get_opacity(&app->platform) < 0.02f;
    bongo_cat_app_track_hover(app, bounds.x - 10, bounds.y - 10);
    bool restored = !app->hover_hidden &&
        SDL_fabsf(bongo_cat_platform_get_opacity(&app->platform) - 1.0f) < 0.02f;
    bongo_cat_app_track_hover(app, x + 10, y + 10);
    app->settings.window.always_on_top = false;
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
    restored = restored && !app->hover_hidden;
    app->settings.window.always_on_top = true;
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
    hidden = hidden && app->hover_hidden;
    app->settings.window.pass_through = false;
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
    restored = restored && !app->hover_hidden;
    app->settings.window.pass_through = true;
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
    hidden = hidden && app->hover_hidden;
    app->settings.window.hide_on_hover = false;
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
    restored = restored && !app->hover_hidden;
    /* With a fade duration the hide no longer snaps: progress animates from
       the on-screen opacity toward the target and completes there. */
    app->settings.window.hide_on_hover = true;
    app->settings.window.hide_fade_seconds = 0.5f;
    bongo_cat_app_track_hover(app, x + 10, y + 10);
    uint64_t fade_started = SDL_GetTicksNS();
    bongo_cat_app_update_hover_fade(app, fade_started + 125000000ull);
    float faded = bongo_cat_platform_get_opacity(&app->platform);
    bongo_cat_app_update_hover_fade(app, fade_started + 600000000ull);
    bool fade = app->hover_hidden && faded > 0.02f && faded < 0.98f &&
        bongo_cat_platform_get_opacity(&app->platform) < 0.02f;
    bongo_cat_app_track_hover(app, bounds.x - 10, bounds.y - 10);
    bongo_cat_app_update_hover_fade(app, SDL_GetTicksNS() + 700000000ull);
    fade = fade && !app->hover_hidden &&
        SDL_fabsf(bongo_cat_platform_get_opacity(&app->platform) - 1.0f) < 0.02f;
    app->settings.window.hide_fade_seconds = 0.0f;
    app->settings.window.hide_on_hover = false;
    float safe_scale;
    int safe_width, safe_height;
    bool bounded = bongo_cat_window_scaled_size(8000, 4000, 100.0f, 500.0f,
        &safe_scale, &safe_width, &safe_height) && safe_width == 8192 &&
        safe_height == 4096 && safe_scale < 103.0f;
    BongoCatInputEvent shift = {.kind = BONGO_CAT_INPUT_KEY_DOWN};
    snprintf(shift.name, sizeof(shift.name), "ShiftLeft");
    bongo_cat_input_push(&app->input, &shift);
    BongoCatInputEvent discarded;
    bongo_cat_input_pop(&app->input, &discarded);
    SDL_Keymod old_modifiers = SDL_GetModState();
    SDL_SetModState(old_modifiers & ~SDL_KMOD_SHIFT);
    app->resize_gesture = false;
    bongo_cat_window_apply_geometry(app, x, y, 100.0f, 320, 240);
    SDL_Event motion = {.type = SDL_EVENT_MOUSE_MOTION};
    motion.motion.state = SDL_BUTTON_RMASK;
    motion.motion.xrel = 20.0f;
    motion.motion.yrel = 20.0f;
    bongo_cat_window_resize_by_pointer(app, &motion);
    SDL_SyncWindow(app->window);
    SDL_GetWindowSize(app->window, &width, &height);
    int gesture_width = width, gesture_height = height;
    bool gesture = app->resize_gesture &&
        app->session.window.scale_percent == 120.0f &&
        width == 384 && height == 288;
    SDL_Event released = {.type = SDL_EVENT_MOUSE_BUTTON_UP};
    released.button.windowID = SDL_GetWindowID(app->window);
    released.button.button = SDL_BUTTON_RIGHT;
    bongo_cat_window_event(app, &released);
    gesture = gesture && !app->resize_gesture;
    app->pointer_known = true; app->model_pointer_anchor_ready = true;
    app->mver_pointer.initialized = true;
    SDL_Event display_scale = {.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED};
    display_scale.window.windowID = SDL_GetWindowID(app->window);
    bongo_cat_window_event(app, &display_scale);
    bool display_reset = !app->pointer_known &&
        !app->model_pointer_anchor_ready && !app->mver_pointer.initialized;
    shift.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_input_push(&app->input, &shift);
    bongo_cat_input_pop(&app->input, &discarded);
    app->resize_gesture = false;
#ifdef _WIN32
    SDL_SetModState(old_modifiers | SDL_KMOD_SHIFT);
    float released_scale = app->session.window.scale_percent;
    bongo_cat_window_resize_by_pointer(app, &motion);
    gesture = gesture && !app->resize_gesture &&
        app->session.window.scale_percent == released_scale;
#endif
    SDL_SetModState(old_modifiers);
    bongo_cat_window_apply_geometry(app, original_x, original_y,
        state_backup.scale_percent, original_width, original_height);
    app->settings.window = preferences_backup;
    app->session.window = state_backup;
    bongo_cat_platform_set_opacity(&app->platform,
        state_backup.opacity_percent / 100.0f);
    bongo_cat_window_sync_click_through(app);
    SDL_SyncWindow(app->window);
    bool passed = clamped && anchor_reset && scaled && opacity && hidden && restored &&
        fade && bounded && gesture && display_reset;
    if (!passed) fprintf(stderr, "geometry self-test: clamped=%d scaled=%d(%dx%d) "
        "anchor=%d opacity=%d hidden=%d restored=%d fade=%d bounded=%d gesture=%d(%dx%d) display=%d\n",
        clamped, scaled, scaled_width, scaled_height, anchor_reset, opacity, hidden, restored,
        fade, bounded, gesture, gesture_width, gesture_height, display_reset);
    return passed;
}
