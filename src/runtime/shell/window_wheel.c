#include "runtime.h"
#include "window_wheel_internal.h"
#include "bongo_cat/preferences.h"

#define WHEEL_OPACITY_STEP 5.0f
#define WHEEL_SCALE_STEP 5.0f
#define WHEEL_SCALE_TARGET_LEAD 10.0f
#define WHEEL_OPACITY_RESPONSE_SECONDS 0.05f
#define WHEEL_SCALE_RESPONSE_SECONDS 0.01f
#define WHEEL_OPACITY_SPEED_PER_SECOND 100.0f
#define WHEEL_SCALE_SPEED_PER_SECOND 150.0f
#define WHEEL_MAX_FRAME_SECONDS (1.0f / 60.0f)

static float wheel_delta(const SDL_MouseWheelEvent *event) {
    float value = event ? event->y : 0.0f;
    return event && event->direction == SDL_MOUSEWHEEL_FLIPPED ? -value : value;
}

static bool wheel_targets_window(BongoCatApp *app,
    const SDL_MouseWheelEvent *event) {
    int width = 0, height = 0;
    if (!app->session.window.visible ||
        event->windowID != SDL_GetWindowID(app->window) ||
        !SDL_GetWindowSize(app->window, &width, &height)) return false;
    if (event->mouse_x < 0.0f || event->mouse_x >= width ||
        event->mouse_y < 0.0f || event->mouse_y >= height) return false;
    /* A transparent/click-through window can occasionally retain mouse focus
       while a wheel message is generated elsewhere. SDL then reuses the last
       local wheel coordinates, so also verify the current desktop cursor for
       real events. Timestamp-zero events are synthetic self-test fixtures. */
    if (event->timestamp) {
        float screen_x = 0.0f, screen_y = 0.0f;
        float local_x = 0.0f, local_y = 0.0f;
        SDL_GetGlobalMouseState(&screen_x, &screen_y);
        if (!bongo_cat_platform_pointer_local(&app->platform, screen_x,
            screen_y, &local_x, &local_y) || local_x < 0.0f ||
            local_x >= width || local_y < 0.0f || local_y >= height)
            return false;
        if (!bongo_cat_window_visible_at_pointer(app, local_x, local_y))
            return false;
    }
    return true;
}

static bool initialize_targets(BongoCatApp *app, bool reset) {
    if (!reset) return true;
    int x = 0, y = 0, width = 0, height = 0;
    if (!SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &width, &height) ||
        width <= 0 || height <= 0 || app->session.window.scale_percent <= 0.0f)
        return false;
    app->wheel_opacity_target = app->session.window.opacity_percent;
    app->wheel_scale_target = app->session.window.scale_percent;
    app->wheel_center_x = x + width * 0.5f;
    app->wheel_center_y = y + height * 0.5f;
    app->wheel_geometry_scale = app->session.window.scale_percent;
    app->wheel_base_width = width;
    app->wheel_base_height = height;
    app->wheel_animation_ns = SDL_GetTicksNS();
    app->wheel_input_ns = app->wheel_animation_ns;
    return true;
}

int bongo_cat_window_wheel_round_position(float value) {
    return (int)(value + (value < 0.0f ? -0.5f : 0.5f));
}

static void clamp_position(BongoCatApp *app, int width, int height, int *x, int *y) {
    if (!app->settings.window.keep_in_screen) return;
    SDL_DisplayID display = SDL_GetDisplayForWindow(app->window);
    SDL_Rect bounds;
    if (!display || !SDL_GetDisplayUsableBounds(display, &bounds)) return;
    int max_x = SDL_max(bounds.x, bounds.x + bounds.w - width);
    int max_y = SDL_max(bounds.y, bounds.y + bounds.h - height);
    *x = SDL_clamp(*x, bounds.x, max_x);
    *y = SDL_clamp(*y, bounds.y, max_y);
}

void bongo_cat_window_wheel(BongoCatApp *app, const SDL_MouseWheelEvent *event) {
    if (!app || !app->window || !event ||
        !wheel_targets_window(app, event)) return;
    float delta = wheel_delta(event);
    if (SDL_fabsf(delta) < 0.001f) return;
    delta = SDL_clamp(delta, -1.0f, 1.0f);
    uint64_t event_ns = event->timestamp ? event->timestamp : SDL_GetTicksNS();
    bool continuing = app->wheel_gesture_active && event_ns >= app->wheel_event_ns &&
        event_ns - app->wheel_event_ns < BONGO_CAT_WHEEL_GESTURE_IDLE_NS;
    if (!initialize_targets(app, !continuing)) return;
    app->wheel_event_ns = event_ns;
    app->wheel_gesture_active = true;
    float old_opacity_target = app->wheel_opacity_target;
    float old_scale_target = app->wheel_scale_target;
    bool control = bongo_cat_input_control_down(&app->input);
#ifndef _WIN32
    control = control || (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
#endif
    /* Windows Raw Input tracks releases without keyboard focus; SDL's
       cached modifiers can retain Ctrl after it has been released. */
    if (!control) {
        float minimum = SDL_max(10.0f,
            app->session.window.scale_percent - WHEEL_SCALE_TARGET_LEAD);
        float maximum = SDL_min(500.0f,
            app->session.window.scale_percent + WHEEL_SCALE_TARGET_LEAD);
        app->wheel_scale_target = SDL_clamp(app->wheel_scale_target +
            delta * WHEEL_SCALE_STEP, minimum, maximum);
    } else {
        app->wheel_opacity_target = SDL_clamp(app->wheel_opacity_target +
            delta * WHEEL_OPACITY_STEP, 10.0f, 100.0f);
    }
    uint64_t input_ns = SDL_GetTicksNS();
    if (old_opacity_target == app->wheel_opacity_target &&
        old_scale_target == app->wheel_scale_target) {
        if (app->wheel_animation_active) app->wheel_input_ns = input_ns;
        return;
    }
    if (!app->wheel_animation_active) app->wheel_animation_ns = input_ns;
    app->wheel_input_ns = input_ns;
    app->wheel_animation_active = true;
    app->dirty = true;
}

static float approach(float current, float target, float elapsed_seconds,
    float response_seconds, float speed_per_second, float snap_distance) {
    float difference = target - current;
    if (SDL_fabsf(difference) < snap_distance) return target;
    float alpha = elapsed_seconds / (response_seconds + elapsed_seconds);
    float step = difference * alpha;
    float maximum = speed_per_second * elapsed_seconds;
    step = SDL_clamp(step, -maximum, maximum);
    return SDL_fabsf(step) >= SDL_fabsf(difference) ? target : current + step;
}

static void apply_scale(BongoCatApp *app, float scale) {
    float old = app->session.window.scale_percent;
    if (old <= 0.0f || SDL_fabsf(scale - old) < 0.001f) return;
    float actual;
    int next_width, next_height;
    if (!bongo_cat_window_scaled_size(app->wheel_base_width,
        app->wheel_base_height, app->wheel_geometry_scale,
        scale, &actual, &next_width, &next_height)) return;
    if (actual != scale) app->wheel_scale_target = actual;
    if (next_width == app->session.window.width &&
        next_height == app->session.window.height) {
        app->session.window.scale_percent = actual;
        return;
    }
    int next_x = bongo_cat_window_wheel_round_position(
        app->wheel_center_x - next_width * 0.5f);
    int next_y = bongo_cat_window_wheel_round_position(
        app->wheel_center_y - next_height * 0.5f);
    clamp_position(app, next_width, next_height, &next_x, &next_y);
    if (!bongo_cat_window_apply_geometry(app, next_x, next_y,
        actual, next_width, next_height)) {
        app->wheel_scale_target = old;
        app->wheel_animation_active = false;
        app->wheel_gesture_active = false;
    }
}

void bongo_cat_window_update_wheel_animation(BongoCatApp *app, uint64_t now) {
    if (!app || !app->wheel_animation_active) return;
    uint64_t elapsed_ns = now >= app->wheel_animation_ns
        ? now - app->wheel_animation_ns : 0;
    app->wheel_animation_ns = now;
    float elapsed_seconds = SDL_min((float)elapsed_ns / 1000000000.0f,
        WHEEL_MAX_FRAME_SECONDS);
    float opacity = approach(app->session.window.opacity_percent,
        app->wheel_opacity_target, elapsed_seconds,
        WHEEL_OPACITY_RESPONSE_SECONDS, WHEEL_OPACITY_SPEED_PER_SECOND, 0.01f);
    float scale = approach(app->session.window.scale_percent,
        app->wheel_scale_target, elapsed_seconds,
        WHEEL_SCALE_RESPONSE_SECONDS, WHEEL_SCALE_SPEED_PER_SECOND, 0.5f);
    bool changed = SDL_fabsf(opacity - app->session.window.opacity_percent) > 0.001f ||
        SDL_fabsf(scale - app->session.window.scale_percent) > 0.001f;
    if (SDL_fabsf(opacity - app->session.window.opacity_percent) > 0.001f) {
        app->session.window.opacity_percent = opacity;
        if (!app->hover_hidden) {
            bongo_cat_app_cancel_hover_fade(app);
            bongo_cat_platform_set_opacity(&app->platform, opacity / 100.0f);
        }
    }
    apply_scale(app, scale);
    bool reached =
        SDL_fabsf(app->session.window.opacity_percent - app->wheel_opacity_target) < 0.01f &&
        SDL_fabsf(app->session.window.scale_percent - app->wheel_scale_target) < 0.01f;
    bool recent_input = now >= app->wheel_input_ns &&
        now - app->wheel_input_ns < BONGO_CAT_WHEEL_GESTURE_IDLE_NS;
    app->wheel_animation_active = !reached || recent_input;
    if (changed || !app->wheel_animation_active) app->dirty = true;
    if (changed) bongo_cat_preferences_invalidate(app->preferences);
}

void bongo_cat_window_cancel_wheel_animation(BongoCatApp *app) {
    if (!app) return;
    app->wheel_animation_active = false;
    app->wheel_gesture_active = false;
}
