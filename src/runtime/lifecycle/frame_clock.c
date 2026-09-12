#include "runtime.h"
#include "bongo_cat/preferences.h"
#include <stdlib.h>

static uint64_t frame_interval_ns(const BongoCatApp *app) {
#ifdef BONGO_CAT_HAS_CUBISM
    int fps = app && app->settings.model.max_fps > 0 ?
        app->settings.model.max_fps : 60;
    return 1000000000ull / (uint64_t)fps;
#else
    (void)app;
    return 100000000ull;
#endif
}

static int remaining_ms(uint64_t deadline, uint64_t now) {
    if (deadline <= now) return 0;
    uint64_t ns = deadline - now;
    uint64_t ms = ns / 1000000ull + (ns % 1000000ull != 0);
    return ms > 250 ? 250 : (int)ms;
}

bool bongo_cat_model_frame_due(const BongoCatApp *app, uint64_t now) {
    return app && app->session.window.visible && !app->window_minimized &&
        (!app->last_frame_ns || now >= app->last_frame_ns +
            frame_interval_ns(app));
}

int bongo_cat_window_wait_timeout(const BongoCatApp *app, uint64_t now) {
    if (!app) return 250;
    uint64_t frame_deadline = app->last_frame_ns + frame_interval_ns(app);
    int wait_ms = app->session.window.visible && !app->window_minimized ? remaining_ms(
        frame_deadline, now) : 250;
    if (bongo_cat_preferences_needs_frame(app->preferences) && wait_ms > 4)
        wait_ms = 4;
    if (app->wheel_animation_active && wait_ms > 8) wait_ms = 8;
    if (app->hover_fade_active && wait_ms > 8) wait_ms = 8;
    if (app->session.window.visible && !app->window_minimized &&
        (app->click_through_applied || (app->settings.window.pass_through &&
            app->settings.window.always_on_top && app->settings.window.hide_on_hover)) &&
        wait_ms > 16) wait_ms = 16;
    bool pending_hit = app->session.window.visible && !app->window_minimized &&
        app->pointer_hit_dirty &&
        app->pointer_hit_deadline_ns && !app->settings.window.pass_through &&
        !app->hover_hidden && !app->left_mouse_down && !app->right_mouse_down;
    if (!pending_hit) return wait_ms;
    int hit_wait = remaining_ms(app->pointer_hit_deadline_ns, now);
    return hit_wait < wait_ms ? hit_wait : wait_ms;
}

bool bongo_cat_wait_event(SDL_Event *event, int timeout_ms) {
    if (!event) return false;
    if (timeout_ms <= 0) return SDL_PollEvent(event);
    uint64_t deadline = SDL_GetTicksNS() + (uint64_t)timeout_ms * 1000000ull;
    for (;;) {
        int remaining = remaining_ms(deadline, SDL_GetTicksNS());
        if (!remaining) return false;
        if (SDL_WaitEventTimeout(event, remaining)) return true;
        uint64_t now = SDL_GetTicksNS();
        if (now >= deadline) return false;
        SDL_Delay(1);
    }
}

bool bongo_cat_window_wait_timeout_self_test(void) {
    const uint64_t now = 1000000000ull;
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) return false;
    bool passed = false;
    app->session.window.visible = true;
    app->settings.model.max_fps = 60;
    app->last_frame_ns = now;
#ifdef BONGO_CAT_HAS_CUBISM
    app->settings.model.max_fps = 30;
    uint64_t interval_30 = frame_interval_ns(app);
    app->settings.model.max_fps = 60;
    uint64_t interval_60 = frame_interval_ns(app);
    app->settings.model.max_fps = 120;
    uint64_t interval_120 = frame_interval_ns(app);
    if (!(interval_30 > interval_60 && interval_60 > interval_120) ||
        bongo_cat_model_frame_due(app, now + interval_120 - 1) ||
        !bongo_cat_model_frame_due(app, now + interval_120)) goto done;
    app->settings.model.max_fps = 60;
#endif
    int frame_wait = remaining_ms(now + frame_interval_ns(app), now);
    if (bongo_cat_window_wait_timeout(app, now) != frame_wait ||
        bongo_cat_model_frame_due(app, now)) goto done;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = now + 8000000ull;
    if (bongo_cat_window_wait_timeout(app, now) != 8) goto done;
    app->pointer_hit_deadline_ns = now;
    if (bongo_cat_window_wait_timeout(app, now) != 0) goto done;
    app->pointer_hit_dirty = false;
#ifdef BONGO_CAT_HAS_CUBISM
    app->settings.model.max_fps = 1;
#endif
    app->click_through_applied = true;
    if (bongo_cat_window_wait_timeout(app, now) != 16) goto done;
    app->click_through_applied = false;
    app->settings.window.pass_through = true;
    app->settings.window.always_on_top = true;
    app->settings.window.hide_on_hover = true;
    if (bongo_cat_window_wait_timeout(app, now) != 16) goto done;
    app->settings.window.always_on_top = false;
    if (bongo_cat_window_wait_timeout(app, now) !=
        remaining_ms(now + frame_interval_ns(app), now)) goto done;
    app->settings.window.pass_through = false;
    app->settings.window.hide_on_hover = false;
#ifdef BONGO_CAT_HAS_CUBISM
    app->settings.model.max_fps = 60;
#endif
    if (!bongo_cat_model_frame_due(app,
        now + frame_interval_ns(app))) goto done;
    app->window_minimized = true;
    if (bongo_cat_model_frame_due(app, now + frame_interval_ns(app)) ||
        bongo_cat_window_wait_timeout(app, now) != 250) goto done;
    app->window_minimized = false;
    app->session.window.visible = false;
    if (bongo_cat_window_wait_timeout(app, now) != 250) goto done;
    app->wheel_animation_active = true;
    passed = bongo_cat_window_wait_timeout(app, now) == 8;
done:
    free(app);
    return passed;
}
