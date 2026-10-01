#include "runtime.h"
#include "window_wheel_internal.h"

#include <stdio.h>

bool bongo_cat_window_wheel_self_test(BongoCatApp *app) {
    if (!app || !app->window) return false;
    BongoCatWindowState backup = app->session.window;
    int original_x, original_y, original_width, original_height;
    SDL_GetWindowPosition(app->window, &original_x, &original_y);
    SDL_GetWindowSize(app->window, &original_width, &original_height);
    SDL_Keymod modifiers = SDL_GetModState();
    uint_fast8_t original_control = atomic_load(&app->input.control);
    atomic_store(&app->input.control, 1);
    SDL_SetModState(modifiers | SDL_KMOD_CTRL);
    app->session.window.opacity_percent = 80.0f;
    app->session.window.scale_percent = 100.0f;
    bongo_cat_window_cancel_wheel_animation(app);
    SDL_MouseWheelEvent wheel = {.y = -1.0f,
        .windowID = SDL_GetWindowID(app->window),
        .mouse_x = original_width * 0.5f,
        .mouse_y = original_height * 0.5f};
    float untouched_scale = app->session.window.scale_percent;
    SDL_WindowID own_window = wheel.windowID;
    wheel.windowID = 0;
    bongo_cat_window_wheel(app, &wheel);
    bool foreign = !app->wheel_animation_active &&
        app->session.window.scale_percent == untouched_scale;
    wheel.windowID = own_window;
    bongo_cat_window_wheel(app, &wheel);
    uint64_t started = app->wheel_animation_ns;
    for (int i = 1; i <= 30; ++i)
        bongo_cat_window_update_wheel_animation(app, started + i * 16666667ull);
    bool opacity = SDL_fabsf(
        app->session.window.opacity_percent - 75.0f) < 0.1f;
    atomic_store(&app->input.control, 0);
#ifndef _WIN32
    SDL_SetModState(modifiers & ~SDL_KMOD_CTRL);
#endif
    /* On Windows leave SDL Ctrl set to reproduce a stale unfocused cache. */
    wheel.y = 1.0f;
    bongo_cat_window_wheel(app, &wheel);
    started = app->wheel_animation_ns;
    float scale_before_frame = app->session.window.scale_percent;
    for (uint64_t offset = 1000000ull;
        offset < BONGO_CAT_WHEEL_FRAME_INTERVAL_NS; offset += 1000000ull)
        bongo_cat_window_update_wheel_animation(app, started + offset);
    bool coalesced = app->session.window.scale_percent == scale_before_frame &&
        app->wheel_animation_ns == started;
    bongo_cat_window_update_wheel_animation(app,
        started + BONGO_CAT_WHEEL_FRAME_INTERVAL_NS);
    coalesced = coalesced && app->session.window.scale_percent > scale_before_frame &&
        app->session.window.scale_percent < app->wheel_scale_target;
    for (int i = 1; i <= 12; ++i)
        bongo_cat_window_update_wheel_animation(app, started + i * 16666667ull);
    bool responsive = SDL_fabsf(
        app->session.window.scale_percent - 102.0f) < 0.1f;
    bongo_cat_window_update_wheel_animation(app,
        started + 250000000ull);
    bool scale = responsive && !app->wheel_animation_active &&
        SDL_fabsf(app->session.window.opacity_percent - 75.0f) < 0.1f;
    SDL_SetModState(modifiers & ~SDL_KMOD_CTRL);
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_window_apply_geometry(app, original_x, original_y, 100.0f,
        original_width, original_height);
    wheel.y = 1000.0f;
    for (int i = 0; i < 2; ++i) bongo_cat_window_wheel(app, &wheel);
    bool burst = app->wheel_scale_target >= 103.5f &&
        app->wheel_scale_target <= 104.1f;
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_window_apply_geometry(app, original_x, original_y, 100.0f,
        original_width, original_height);
    wheel.y = 3.0f;
    bongo_cat_window_wheel(app, &wheel);
    bool aggregated = app->wheel_scale_target >= 101.5f &&
        app->wheel_scale_target <= 102.1f;
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_window_apply_geometry(app, original_x, original_y, 100.0f,
        original_width, original_height);
    wheel.y = 1.0f;
    bongo_cat_window_wheel(app, &wheel);
    wheel.y = -1.0f;
    bongo_cat_window_wheel(app, &wheel);
    started = app->wheel_animation_ns;
    for (int i = 1; i <= 30; ++i)
        bongo_cat_window_update_wheel_animation(app, started + i * 16666667ull);
    bool reversal = !app->wheel_animation_active &&
        SDL_fabsf(app->session.window.scale_percent - 100.0f) < 0.1f;
    wheel.y = 1.0f;
    wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    bongo_cat_window_wheel(app, &wheel);
    started = app->wheel_animation_ns;
    for (int i = 1; i <= 30; ++i)
        bongo_cat_window_update_wheel_animation(app, started + i * 16666667ull);
    bool flipped = SDL_fabsf(
        app->session.window.scale_percent - 98.0f) < 0.1f;
    wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    bongo_cat_window_cancel_wheel_animation(app);
    app->session.window.scale_percent = 500.0f;
    bongo_cat_window_wheel(app, &wheel);
    bool maximum = !app->wheel_animation_active;
    bongo_cat_window_cancel_wheel_animation(app);
    app->session.window.scale_percent = 10.0f;
    wheel.y = -1.0f;
    bongo_cat_window_wheel(app, &wheel);
    bool minimum = !app->wheel_animation_active;
    bongo_cat_window_cancel_wheel_animation(app);
    /* Right Ctrl must work even when SDL has not received the key press. */
    atomic_store(&app->input.control, 2);
    app->session.window.opacity_percent = 100.0f;
    wheel.y = 1.0f;
    bongo_cat_window_wheel(app, &wheel);
    bool opacity_maximum = !app->wheel_animation_active;
    bongo_cat_window_cancel_wheel_animation(app);
    app->session.window.opacity_percent = 10.0f;
    wheel.y = -1.0f;
    bongo_cat_window_wheel(app, &wheel);
    bool opacity_minimum = !app->wheel_animation_active;
    bool rounding = bongo_cat_window_wheel_round_position(-10.6f) == -11 &&
        bongo_cat_window_wheel_round_position(10.6f) == 11;
    SDL_SetModState(modifiers);
    atomic_store(&app->input.control, original_control);
    app->session.window = backup;
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_platform_set_opacity(&app->platform,
        backup.opacity_percent / 100.0f);
    bongo_cat_platform_set_geometry(&app->platform, original_x, original_y,
        original_width, original_height);
    bool passed = foreign && opacity && scale && burst && aggregated &&
        reversal && flipped && maximum && minimum && opacity_maximum &&
        opacity_minimum && rounding && coalesced;
    if (!passed)
        fprintf(stderr, "wheel self-test: foreign=%d opacity=%d scale=%d "
            "burst=%d aggregated=%d reversal=%d flipped=%d maximum=%d "
            "minimum=%d opacity_max=%d opacity_min=%d rounding=%d coalesced=%d\n",
            foreign, opacity, scale, burst, aggregated, reversal, flipped,
            maximum, minimum, opacity_maximum, opacity_minimum, rounding, coalesced);
    return passed;
}
