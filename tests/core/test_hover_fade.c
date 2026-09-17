#include "test.h"
#include "runtime.h"
#include "modal_frame.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned opacity_calls, hit_calls;
static bool fail_opacity, hit = true;

bool bongo_cat_platform_set_opacity(BongoCatPlatform *platform, float opacity) {
    opacity_calls++;
    if (fail_opacity) {
        fail_opacity = false;
        return SDL_SetError("injected opacity failure");
    }
    platform->window_opacity = opacity;
    return true;
}
float bongo_cat_platform_get_opacity(const BongoCatPlatform *platform) {
    return platform->window_opacity;
}
bool bongo_cat_platform_pointer_local(BongoCatPlatform *platform,
    double x, double y, float *local_x, float *local_y) {
    (void)platform;
    *local_x = (float)x; *local_y = (float)y;
    return x >= 0 && x < 100 && y >= 0 && y < 100;
}
bool bongo_cat_window_visible_at_pointer(BongoCatApp *app, float x, float y) {
    (void)app; (void)x; (void)y;
    hit_calls++;
    return hit;
}
void bongo_cat_window_schedule_pointer_hit(BongoCatApp *app) { (void)app; }
void bongo_cat_window_sync_click_through(BongoCatApp *app) { (void)app; }
void bongo_cat_window_snapshot_end(BongoCatApp *app) { (void)app; }
void bongo_cat_window_snapshot_update(BongoCatApp *app, uint64_t now) {
    (void)app; (void)now;
}
void bongo_cat_app_drain_input(BongoCatApp *app, bool shortcuts) {
    (void)app; (void)shortcuts;
}
bool bongo_cat_app_step_live2d(BongoCatApp *app, float elapsed) {
    (void)app; (void)elapsed;
    return true;
}
void bongo_cat_app_render_now(BongoCatApp *app) { (void)app; }

static void reset(BongoCatApp *app) {
    memset(app, 0, sizeof(*app));
    app->window = app->platform.window = (SDL_Window *)app;
    app->platform.window_opacity = 0.8f;
    app->session.window.opacity_percent = 80.0f;
    app->session.window.visible = true;
    app->settings.window.hide_on_hover = true;
    app->settings.window.always_on_top = true;
    app->settings.window.pass_through = true;
    app->settings.window.hide_fade_seconds = 0.3f;
    app->pointer_known = true;
    app->pointer_x = app->pointer_y = 50;
    opacity_calls = hit_calls = 0;
    fail_opacity = false;
    hit = true;
}

static void tick(BongoCatApp *app, uint64_t now, bool inside) {
    app->pointer_x = inside ? 50 : -10;
    bongo_cat_app_update_hover(app, now);
    bongo_cat_app_update_hover_fade(app, now);
}

int main(void) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    const uint64_t start = 1000000000ull;
    reset(app);
    tick(app, start, true);
    tick(app, start + 100000000ull, true);
    float partial = app->platform.window_opacity;
    CHECK(partial > 0.0f && partial < 0.8f);
    tick(app, start + 100000000ull, false);
    CHECK(app->platform.window_opacity == partial);
    tick(app, start + 201000000ull, false);
    CHECK(!app->hover_fade_active && app->platform.window_opacity == 0.8f);

    /* Repeated reversal stays bounded and finishes at the user's opacity. */
    reset(app);
    uint64_t now = start;
    for (int i = 0; i < 100; ++i) {
        bool inside = i % 2 == 0;
        tick(app, now, inside);
        now += 24000000ull;
        tick(app, now, inside);
        CHECK(app->platform.window_opacity >= 0.0f &&
            app->platform.window_opacity <= 0.8f);
    }
    tick(app, now + 1000000000ull, false);
    CHECK(!app->hover_hidden && !app->hover_fade_active &&
        app->platform.window_opacity == 0.8f);

    /* 1000 Hz input must not generate 1000 opacity writes or hit reads. */
    reset(app);
    app->settings.window.hide_fade_seconds = 3.0f;
    for (uint64_t i = 0; i <= 1000; ++i)
        tick(app, start + i * 1000000ull, true);
    CHECK(opacity_calls <= 125 && hit_calls <= 126);
    CHECK(opacity_calls > 0 && app->hover_fade_active);
    partial = app->platform.window_opacity;
    app->settings.window.hide_fade_seconds = 1.0f;
    tick(app, start + 1008000000ull, true);
    CHECK(app->platform.window_opacity < partial &&
        partial - app->platform.window_opacity < 0.03f);
    app->settings.window.hide_fade_seconds = 0.0f;
    tick(app, start + 1009000000ull, true);
    CHECK(!app->hover_fade_active && app->platform.window_opacity == 0.0f);
    tick(app, start + 1010000000ull, false);
    CHECK(app->platform.window_opacity == 0.8f);

    reset(app);
    for (uint64_t i = 0; i <= 1000; ++i)
        tick(app, start + i * 1000000ull, i % 2 == 0);
    CHECK(hit_calls <= 126 && opacity_calls <= 125);

    /* A lower base opacity chosen during fade-out is the new restore target. */
    reset(app);
    tick(app, start, true);
    tick(app, start + 100000000ull, true);
    app->session.window.opacity_percent = 20.0f;
    tick(app, start + 101000000ull, false);
    tick(app, start + 1000000000ull, false);
    CHECK(SDL_fabsf(app->platform.window_opacity - 0.2f) < 0.00001f &&
        !app->hover_fade_active);

    /* Disabled capability performs no sampling or opacity work. */
    reset(app);
    app->platform.hover_hide_unavailable = true;
    tick(app, start, true);
    CHECK(!app->hover_hidden && !app->hover_fade_active &&
        opacity_calls == 0 && hit_calls == 0);
    reset(app);
    tick(app, start, true);
    fail_opacity = true;
    tick(app, start + 10000000ull, true);
    CHECK(app->platform.hover_hide_unavailable && !app->hover_hidden &&
        !app->hover_fade_active && app->platform.window_opacity == 0.8f);
    unsigned calls = opacity_calls;
    tick(app, start + 1000000000ull, true);
    CHECK(opacity_calls == calls);

    /* Model alpha changes while the pointer is stationary are still sampled. */
    reset(app);
    tick(app, start, true);
    tick(app, start + 100000000ull, true);
    hit = false;
    tick(app, start + 108000000ull, true);
    CHECK(!app->hover_hidden);
    tick(app, start + 500000000ull, true);
    CHECK(app->platform.window_opacity == 0.8f);

    /* The menu/modal loop must advance an existing fade without the app loop. */
    reset(app);
    tick(app, SDL_GetTicksNS(), true);
    app->settings.window.hide_fade_seconds = 0.0f;
    BongoCatModalFrame modal;
    bongo_cat_modal_frame_init(&modal, app);
    bongo_cat_modal_frame_tick(&modal);
    CHECK(app->platform.window_opacity == 0.0f && !app->hover_fade_active);
    free(app);
    return bongo_cat_test_failures ? 1 : 0;
}
