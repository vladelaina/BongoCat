#include "modal_frame.h"
#include "bongo_cat/resource_trace.h"

static float modal_elapsed(BongoCatModalFrame *state, uint64_t app_frame_ns,
    uint64_t now) {
    uint64_t baseline = state->last_tick_ns;
    if (app_frame_ns > baseline) baseline = app_frame_ns;
    state->last_tick_ns = now;
    if (now <= baseline) return 0.0f;
    float elapsed = (float)((now - baseline) / 1000000000.0);
    return elapsed > 0.25f ? 0.25f : elapsed;
}

void bongo_cat_modal_frame_init(BongoCatModalFrame *state, BongoCatApp *app) {
    if (!state) return;
    *state = (BongoCatModalFrame){.app = app,
        .last_tick_ns = SDL_GetTicksNS()};
}

void bongo_cat_modal_frame_tick(void *userdata) {
    BongoCatModalFrame *state = userdata;
    if (!state || !state->app) return;
    BongoCatApp *app = state->app;
    /* Native event pumping during a model handoff can reenter modal timers.
       The outgoing model no longer owns render resources until completion. */
    if (app->loading_model[0]) return;
    bongo_cat_resource_trace_poll();
    /* Include pause expiry and settled reclamation, not just busy uploads.
       The service queries its deadline before switching GL contexts and
       explicitly forbids starting a new job from this native modal loop. */
    bongo_cat_app_refresh_texture_resolution(app, false);
    uint64_t now = SDL_GetTicksNS();
    float elapsed = modal_elapsed(state, app->last_frame_ns, now);
    app->last_frame_ns = now;
    state->tick_count++;
    bongo_cat_app_drain_input(app, true);
    bongo_cat_window_snapshot_update(app, SDL_GetTicksNS());
    now = SDL_GetTicksNS();
    bongo_cat_app_update_hover(app, now);
    bongo_cat_app_update_hover_fade(app, now);
    if (!app->smoke_freeze_model && elapsed > 0.0f)
        bongo_cat_app_step_live2d(app, elapsed);
    bongo_cat_app_render_now(app);
}

bool bongo_cat_modal_frame_self_test(void) {
    BongoCatModalFrame state = {0};
    float first = modal_elapsed(&state, 983333333ull, 1000000000ull);
    float second = modal_elapsed(&state, 0, 1020000000ull);
    state.last_tick_ns = 1000000000ull;
    float main_loop_newer = modal_elapsed(&state, 1040000000ull, 1050000000ull);
    float capped = modal_elapsed(&state, 0, 1550000000ull);
    float backwards = modal_elapsed(&state, 0, 1500000000ull);
    return SDL_fabsf(first - 1.0f / 60.0f) < 0.0001f &&
        SDL_fabsf(second - 0.02f) < 0.0001f &&
        SDL_fabsf(main_loop_newer - 0.01f) < 0.0001f &&
        SDL_fabsf(capped - 0.25f) < 0.0001f && backwards == 0.0f;
}
