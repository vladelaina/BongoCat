#include "runtime.h"
#include "hover_fade.h"

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

static bool hover_opacity(BongoCatApp *app, float opacity) {
    bongo_cat_window_snapshot_end(app);
    if (bongo_cat_platform_set_opacity(&app->platform, opacity)) return true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Hover hiding disabled after opacity update failed: %s", SDL_GetError());
    app->platform.hover_hide_unavailable = true;
    app->hover_fade_active = false;
    app->hover_hidden = app->hover_inside = false;
    /* Best effort: retain the successful cached opacity if restoration fails. */
    bongo_cat_platform_set_opacity(&app->platform,
        app->session.window.opacity_percent / 100.0f);
    bongo_cat_window_sync_click_through(app);
    return false;
}

/* Direction changes retrace the same curve, without restarting its easing.
   Integrating elapsed time also makes duration edits continuous. */
void bongo_cat_app_update_hover_fade(BongoCatApp *app, uint64_t now) {
    if (!app || !app->hover_fade_active) return;
    float duration = app->settings.window.hide_fade_seconds;
    if (duration > 0.0f && now < app->hover_fade_next_ns) return;
    float seconds = now > app->hover_fade_tick_ns ?
        (float)((double)(now - app->hover_fade_tick_ns) / 1000000000.0) : 0.0f;
    app->hover_fade_tick_ns = now;
    app->hover_fade_next_ns = now + 8000000ull;
    bool decreasing = app->hover_fade_target < app->hover_fade_phase;
    float next = bongo_cat_hover_fade_step(app->hover_fade_phase,
        decreasing, seconds, duration);
    app->hover_fade_phase = decreasing ? SDL_max(next, app->hover_fade_target) :
        SDL_min(next, app->hover_fade_target);
    float phase = app->hover_fade_phase;
    float opacity = phase == app->hover_fade_target ?
        (app->hover_hidden ? 0.0f : app->session.window.opacity_percent / 100.0f) :
        app->hover_fade_opacity * bongo_cat_hover_fade_curve(phase);
    if (hover_opacity(app, opacity))
        app->hover_fade_active = phase != app->hover_fade_target;
}

void bongo_cat_app_cancel_hover_fade(BongoCatApp *app) {
    if (app) {
        app->hover_fade_active = false;
        app->hover_fade_next_ns = 0;
    }
}

static void hover_start_fade(BongoCatApp *app, bool hidden, uint64_t now) {
    float target = hidden ? 0.0f :
        app->session.window.opacity_percent / 100.0f;
    app->hover_hidden = hidden;
    if (app->settings.window.hide_fade_seconds <= 0.0f) {
        app->hover_fade_active = false;
        hover_opacity(app, target);
        return;
    }
    float current = bongo_cat_platform_get_opacity(&app->platform);
    app->hover_fade_opacity = SDL_max(current,
        app->session.window.opacity_percent / 100.0f);
    app->hover_fade_phase = bongo_cat_hover_fade_phase(
        app->hover_fade_opacity > 0.0f ? current / app->hover_fade_opacity : 0.0f);
    app->hover_fade_target = bongo_cat_hover_fade_phase(
        app->hover_fade_opacity > 0.0f ? target / app->hover_fade_opacity : 0.0f);
    app->hover_fade_tick_ns = now;
    app->hover_fade_next_ns = now + 8000000ull;
    app->hover_fade_active = current != target;
}

void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now) {
    if (!app || !app->window) return;
    bool enabled = !app->platform.hover_hide_unavailable &&
        app->settings.window.hide_on_hover &&
        app->settings.window.pass_through && app->settings.window.always_on_top &&
        app->session.window.visible && !app->window_minimized &&
        !app->startup_visibility_pending;
    float local_x = 0.0f, local_y = 0.0f;
    /* Window opacity does not change the frame alpha, so hidden pets can
       still detect the pointer leaving their visible model pixels. */
    bool inside = enabled && app->pointer_known &&
        bongo_cat_platform_pointer_local(&app->platform,
            app->pointer_x, app->pointer_y, &local_x, &local_y);
    if (inside) {
        /* Bound GPU readbacks on backends without a CPU alpha cache, even
           when high-rate input wakes the event loop faster than rendering. */
        if (now < app->hover_deadline_ns) return;
        app->hover_deadline_ns = now + 8000000ull;
        inside = bongo_cat_window_visible_at_pointer(app, local_x, local_y);
    } else if (!enabled) app->hover_deadline_ns = 0;
    app->hover_inside = inside;
    if (app->hover_hidden == inside) return;
    hover_start_fade(app, inside, now);
    bongo_cat_window_sync_click_through(app);
}
