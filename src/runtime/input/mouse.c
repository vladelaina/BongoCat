#include "runtime.h"

#include <SDL3/SDL.h>

/* Window-facing pointer state: hover hiding and hit-test scheduling. The
   sampling pipeline, display bounds and model output have their own modules. */
void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y) {
    app->pointer_known = true;
    app->pointer_x = x;
    app->pointer_y = y;
    bongo_cat_window_schedule_pointer_hit(app);
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
}

/* Fade progress follows absolute timestamps, so a duration change or an
   interrupted reversal restarts from the opacity currently on screen. */
void bongo_cat_app_update_hover_fade(BongoCatApp *app, uint64_t now) {
    if (!app || !app->hover_fade_active) return;
    float duration = app->settings.window.hide_fade_seconds;
    float target = app->hover_fade_to;
    if (duration <= 0.0f) {
        app->hover_fade_active = false;
        bongo_cat_platform_set_opacity(&app->platform, target);
        return;
    }
    float seconds = (float)((double)(now >= app->hover_fade_start_ns ?
        now - app->hover_fade_start_ns : 0) / 1000000000.0);
    float t = seconds / duration;
    if (t >= 1.0f) {
        app->hover_fade_active = false;
        t = 1.0f;
    } else if (t < 0.0f) t = 0.0f;
    float eased = t * t * (3.0f - 2.0f * t);
    bongo_cat_platform_set_opacity(&app->platform,
        app->hover_fade_from +
            (target - app->hover_fade_from) * eased);
}

void bongo_cat_app_cancel_hover_fade(BongoCatApp *app) {
    if (app) app->hover_fade_active = false;
}

static void hover_start_fade(BongoCatApp *app, bool hidden, uint64_t now) {
    float target = hidden ? 0.0f :
        app->session.window.opacity_percent / 100.0f;
    if (app->settings.window.hide_fade_seconds <= 0.0f) {
        app->hover_fade_active = false;
        bongo_cat_platform_set_opacity(&app->platform, target);
        return;
    }
    app->hover_fade_from = bongo_cat_platform_get_opacity(&app->platform);
    app->hover_fade_to = target;
    app->hover_fade_start_ns = now;
    app->hover_fade_active = true;
    bongo_cat_app_update_hover_fade(app, now);
}

void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now) {
    if (!app || !app->window) return;
    bool enabled = app->settings.window.hide_on_hover &&
        app->settings.window.pass_through && app->settings.window.always_on_top &&
        app->session.window.visible && !app->window_minimized &&
        !app->startup_visibility_pending;
    float local_x, local_y;
    /* Window opacity does not change the frame alpha, so hidden pets can
       still detect the pointer leaving their visible model pixels. */
    bool inside = enabled && app->pointer_known &&
        bongo_cat_platform_pointer_local(&app->platform,
            app->pointer_x, app->pointer_y, &local_x, &local_y) &&
        bongo_cat_window_visible_at_pointer(app, local_x, local_y);
    app->hover_inside = inside;
    app->hover_deadline_ns = 0;
    if (app->hover_hidden == inside) return;
    hover_start_fade(app, inside, now);
    app->hover_hidden = inside;
    bongo_cat_window_sync_click_through(app);
}
