#include "test.h"
#include "windows_input_internal.h"

static void test_relative_motion(void) {
    WindowsInputState state = {0};
    WindowsRawDevice device = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    state.receiving = true;
    RAWMOUSE mouse = {.lLastX = 17, .lLastY = -9};
    double x, y;
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    bongo_cat_windows_input_reset_relative(&platform);
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    mouse.lLastX = -5;
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    unsigned long long samples;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    CHECK(x == 12.0 && y == -18.0 && samples == 2);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    CHECK(x == 0.0 && y == 0.0 && samples == 0);
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    bongo_cat_windows_input_reset_relative(&platform);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 0.0 && y == 0.0);
    bongo_cat_windows_input_release_relative(&platform);
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    bongo_cat_windows_input_reset_relative(&platform);
    state.receiving = false;
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
}

static void test_absolute_devices(void) {
    WindowsInputState state = {0};
    WindowsRawDevice a = {0}, b = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    state.receiving = true;
    bongo_cat_windows_input_reset_relative(&platform);
    RECT bounds = {-1920, -200, 3440, 1440};
    RAWMOUSE mouse = {.usFlags = MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP};
    bongo_cat_windows_input_motion(&state, &a, &mouse, &bounds);
    CHECK(a.absolute_x == -1920.0 && a.absolute_y == -200.0);
    mouse.lLastX = mouse.lLastY = 65535;
    bongo_cat_windows_input_motion(&state, &b, &mouse, &bounds);
    double x, y;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 0.0 && y == 0.0);
    bongo_cat_windows_input_motion(&state, &a, &mouse, &bounds);
    CHECK(state.observed_absolute_x == 5359.0);
    CHECK(state.observed_absolute_y == 1639.0);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 5359.0 && y == 1639.0);
    CHECK(a.absolute_x == 3439.0 && a.absolute_y == 1439.0);
    bongo_cat_windows_input_reset_relative(&platform);
    mouse.lLastX = mouse.lLastY = 32768;
    bongo_cat_windows_input_motion(&state, &a, &mouse, &bounds);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 0.0 && y == 0.0);
    bounds.right = 2560;
    mouse.lLastX = 30000;
    bongo_cat_windows_input_motion(&state, &a, &mouse, &bounds);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 0.0 && y == 0.0);
    mouse.lLastX = -1;
    bongo_cat_windows_input_motion(&state, &a, &mouse, &bounds);
    CHECK(!a.absolute_known);
    CHECK(state.observed_absolute_x == 5359.0);
    CHECK(state.observed_absolute_y == 1639.0);
    bongo_cat_windows_input_clear_motion(&state);
    CHECK(state.observed_absolute_x == 0.0 && state.observed_absolute_y == 0.0);
}

static void test_motion_units(void) {
    WindowsInputState state = {0};
    WindowsRawDevice absolute = {0}, relative = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    state.receiving = true;
    bongo_cat_windows_input_reset_relative(&platform);
    RECT bounds = {0, 0, 2560, 1440};
    RAWMOUSE mouse = {.usFlags = MOUSE_MOVE_ABSOLUTE};
    bongo_cat_windows_input_motion(&state, &absolute, &mouse, &bounds);
    mouse.lLastX = 32768;
    bongo_cat_windows_input_motion(&state, &absolute, &mouse, &bounds);
    mouse = (RAWMOUSE){.lLastX = 3, .lLastY = -4};
    bongo_cat_windows_input_motion(&state, &relative, &mouse, NULL);
    double x, y;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 3.0 && y == -4.0);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    CHECK(x == 0.0 && y == 0.0);
}

static void test_delivered_motion_without_ownership(void) {
    WindowsInputState state = {0};
    WindowsRawDevice device = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    bongo_cat_windows_input_reset_relative(&platform);
    RAWMOUSE mouse = {.lLastX = 12, .lLastY = -3};
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    mouse.lLastX = -12;
    mouse.lLastY = 3;
    bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    double x, y;
    unsigned long long samples;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    CHECK(x == 0.0 && y == 0.0 && samples == 2);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    RECT bounds = {0, 0, 1920, 1080};
    mouse = (RAWMOUSE){.usFlags = MOUSE_MOVE_ABSOLUTE};
    bongo_cat_windows_input_motion(&state, &device, &mouse, &bounds);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
    mouse.lLastX = mouse.lLastY = 65535;
    bongo_cat_windows_input_motion(&state, &device, &mouse, &bounds);
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    CHECK(x == 1919.0 && y == 1079.0 && samples == 1);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, NULL));
}

static void test_observation_does_not_consume_motion(void) {
    WindowsInputState state = {0};
    WindowsRawDevice relative = {0}, absolute = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    bongo_cat_windows_input_reset_relative(&platform);
    RAWMOUSE mouse = {.lLastX = 12, .lLastY = -3};
    bongo_cat_windows_input_motion(&state, &relative, &mouse, NULL);
    RECT bounds = {0, 0, 1920, 1080};
    mouse = (RAWMOUSE){.usFlags = MOUSE_MOVE_ABSOLUTE};
    bongo_cat_windows_input_motion(&state, &absolute, &mouse, &bounds);
    mouse.lLastX = mouse.lLastY = 65535;
    bongo_cat_windows_input_motion(&state, &absolute, &mouse, &bounds);
    WindowsPointerObservation sample = {.position_known = true, .position = {42, 24}};
    CHECK(bongo_cat_windows_input_take_observation(&state, &sample) == 0);
    CHECK(sample.raw_x == 12 && sample.raw_y == -3);
    CHECK(sample.absolute_x == 1919 && sample.absolute_y == 1079);
    CHECK(sample.motion_packets == 2);
    CHECK(sample.position_known && sample.position.x == 42 && sample.position.y == 24);
    double x, y;
    unsigned long long count;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &count));
    CHECK(x == 12 && y == -3 && count == 1);
    CHECK(bongo_cat_windows_input_take_observation(&state, &sample) == 0);
    CHECK(sample.raw_x == 0 && sample.absolute_x == 0 && sample.motion_packets == 0);
    bongo_cat_windows_input_clear_motion(&state);
    CHECK(bongo_cat_windows_input_take_observation(&state, &sample) == 1);
    CHECK(sample.raw_y == 0 && sample.absolute_y == 0 && sample.motion_packets == 0);
}

static void test_high_rate_observation(void) {
    WindowsInputState state = {0};
    WindowsRawDevice device = {0};
    BongoCatPlatform platform = {.native = &state};
    InitializeSRWLock(&state.relative_lock);
    RAWMOUSE mouse = {.lLastX = 3, .lLastY = -2};
    for (unsigned i = 0; i < 8000; ++i)
        bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    CHECK(state.observed_x == 24000 && state.observed_y == -16000);
    CHECK(state.observed_motion == 8000 && state.relative_samples == 0);
    bongo_cat_windows_input_reset_relative(&platform);
    CHECK(state.observed_motion == 8000);
    CHECK(state.observed_generation == 0);
    for (unsigned i = 0; i < 8000; ++i)
        bongo_cat_windows_input_motion(&state, &device, &mouse, NULL);
    double x, y;
    unsigned long long samples;
    CHECK(bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    CHECK(x == 24000 && y == -16000 && samples == 8000);
    CHECK(state.observed_motion == 16000);
    CHECK(!bongo_cat_windows_input_take_relative(&platform, &x, &y, &samples));
    bongo_cat_windows_input_clear_motion(&state);
    CHECK(state.observed_x == 0 && state.observed_y == 0);
    CHECK(state.observed_motion == 0 && state.observed_generation == 1);
}

void test_windows_relative_sources(void) {
    test_relative_motion();
    test_absolute_devices();
    test_motion_units();
    test_delivered_motion_without_ownership();
    test_observation_does_not_consume_motion();
    test_high_rate_observation();
}
