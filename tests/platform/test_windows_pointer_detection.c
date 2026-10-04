#include "test.h"
#include "windows_input_detection.h"
#include <limits.h>

static void test_clip_bounds(void) {
    static const struct {
        RECT clip;
        bool locked;
    } cases[] = {
        {{-1920, -1080, 0, 0}, false},
        {{-1920, -1080, -1, -1}, false},
        {{-1920, -1080, 1920, 1080}, false},
        {{0, 0, 1920, 1080}, false},
        {{-2, -2, 0, 0}, true},
        {{-1, -1, 1, 1}, true},
        {{960, 540, 961, 541}, true},
        {{0, 0, 0, 0}, true},
        {{-3, -2, 0, 0}, false},
        {{-2, -3, 0, 0}, false},
        {{1, 0, 0, 1}, false},
        {{0, 1, 1, 0}, false},
        {{LONG_MIN, LONG_MIN, LONG_MAX, LONG_MAX}, false},
        {{LONG_MIN, LONG_MIN, LONG_MIN + 2, LONG_MIN + 2}, true},
        {{LONG_MAX - 2, LONG_MAX - 2, LONG_MAX, LONG_MAX}, true}
    };
    CHECK(!bongo_cat_windows_pointer_clip_locked(NULL));
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bool locked = bongo_cat_windows_pointer_clip_locked(&cases[i].clip);
        if (locked != cases[i].locked)
            fprintf(stderr, "clip bounds case=%u locked=%d\n", i, (int)locked);
        CHECK(locked == cases[i].locked);
    }
}

static WindowsPointerObservation observation(void) {
    return (WindowsPointerObservation){.foreground = (HWND)(uintptr_t)1,
        .pid = 2, .foreign = true, .position_known = true, .monitor_known = true,
        .clip_known = true, .cursor_known = true, .cursor_flags = CURSOR_SHOWING,
        .position = {960, 540}, .monitor = {0, 0, 1920, 1080},
        .clip = {0, 0, 1920, 1080}};
}

static void test_stalled_and_resume(void) {
    WindowsPointerDetection state = {0};
    WindowsPointerObservation sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    sample.raw_x = 12;
    sample.motion_packets = 128;
    for (unsigned t = 16; t < 128; t += 16)
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, t));
    CHECK(bongo_cat_windows_pointer_detect(&state, &sample, 128));
    CHECK(state.reason == WINDOWS_POINTER_STALLED);
    sample.raw_x = 0;
    sample.motion_packets = 0;
    for (unsigned t = 144; t <= 1024; t += 16)
        CHECK(bongo_cat_windows_pointer_detect(&state, &sample, t));
    sample.raw_x = 10;
    sample.motion_packets = 1;
    for (unsigned t = 1040; t <= 1280; t += 16) {
        sample.position.x += 10;
        bongo_cat_windows_pointer_detect(&state, &sample, t);
    }
    CHECK(!state.relative && state.reason == WINDOWS_POINTER_DESKTOP);
}

static void test_recenter_and_real_travel(void) {
    WindowsPointerDetection state = {0};
    WindowsPointerObservation sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    sample.raw_x = 10;
    sample.motion_packets = 1;
    for (unsigned i = 1; i <= 8; ++i) {
        sample.position.x = i % 2 ? 974 : 960;
        bongo_cat_windows_pointer_detect(&state, &sample, i * 16);
    }
    CHECK(state.relative && state.reason == WINDOWS_POINTER_RECENTERED);
    state = (WindowsPointerDetection){0};
    sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    for (unsigned i = 1; i <= 64; ++i) {
        sample.raw_x = i % 2 ? 14.0 : -14.0;
        sample.motion_packets = 1;
        sample.position.x = i % 2 ? 974 : 960;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, i * 16));
    }
}

static void test_desktop_edges_and_slow_motion(void) {
    static const char *const scenarios[] = {
        "monitor edge", "negative monitor edge", "clip edge",
        "unknown monitor", "slow cursor travel", "small raw movement",
        "alternating raw movement"
    };
    for (unsigned scenario = 0;
        scenario < sizeof(scenarios) / sizeof(scenarios[0]); ++scenario) {
        WindowsPointerDetection state = {0};
        WindowsPointerObservation sample = observation();
        if (scenario == 0) sample.position.x = 1919;
        if (scenario == 1) {
            sample.monitor = (RECT){-1920, -1080, 0, 0};
            sample.clip = sample.monitor;
            sample.position = (POINT){-1920, -500};
        }
        if (scenario == 2) sample.clip.right = 961;
        if (scenario == 3) sample.monitor_known = false;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
        for (unsigned i = 1; i <= 64; ++i) {
            sample.motion_packets = 128;
            sample.raw_x = 32;
            if (scenario == 4) sample.position.x += 1;
            if (scenario == 5) sample.raw_x = 1;
            if (scenario == 6) sample.raw_x = i % 2 ? 4.0 : -4.0;
            bool relative = bongo_cat_windows_pointer_detect(&state, &sample, i * 16);
            if (relative) {
                fprintf(stderr, "desktop scenario=%s sample=%u reason=%d raw_x=%.1f\n",
                    scenarios[scenario], i, (int)state.reason, sample.raw_x);
                fprintf(stderr, "clip=(%ld,%ld,%ld,%ld)\n", sample.clip.left,
                    sample.clip.top, sample.clip.right, sample.clip.bottom);
            }
            CHECK(!relative);
        }
    }
}

static void test_hidden_and_clipped(void) {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        WindowsPointerDetection state = {0};
        WindowsPointerObservation sample = observation();
        if (scenario == 0) sample.cursor_flags = 0;
        if (scenario == 1) sample.clip = (RECT){960, 540, 961, 541};
        if (scenario == 2) sample.cursor_known = false;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
        /* A hidden cursor without mouse input must not activate tracking. */
        for (unsigned t = 16; t <= 256; t += 16)
            CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, t));
        sample.raw_y = 5;
        sample.motion_packets = 1;
        for (unsigned t = 272; t <= 304; t += 16)
            bongo_cat_windows_pointer_detect(&state, &sample, t);
        CHECK(state.relative == (scenario == 1));
        for (unsigned t = 320; t <= 384; t += 16)
            bongo_cat_windows_pointer_detect(&state, &sample, t);
        CHECK(state.relative);
        CHECK(state.reason == (scenario == 0 ? WINDOWS_POINTER_HIDDEN :
            scenario == 1 ? WINDOWS_POINTER_CLIPPED : WINDOWS_POINTER_STALLED));
        if (scenario == 2) continue;
        sample.cursor_flags = CURSOR_SHOWING;
        sample.clip = sample.monitor;
        sample.raw_y = 0;
        sample.motion_packets = 0;
        for (unsigned t = 400; t <= 768; t += 16)
            bongo_cat_windows_pointer_detect(&state, &sample, t);
        CHECK(!state.relative);
    }
}

static void test_focus_suspend_and_missing_position(void) {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        WindowsPointerDetection state = {0};
        WindowsPointerObservation sample = observation();
        sample.cursor_flags = 0;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
        sample.raw_x = 8;
        sample.motion_packets = 1;
        for (unsigned t = 16; t <= 128; t += 16)
            bongo_cat_windows_pointer_detect(&state, &sample, t);
        CHECK(state.relative);
        if (scenario == 0) sample.foreground = (HWND)(uintptr_t)3;
        if (scenario == 1) sample.pid++;
        if (scenario == 2) sample.foreign = false;
        if (scenario == 3) sample.position_known = false;
        ULONGLONG now = scenario == 4 ? 1000 : scenario == 5 ? 1 : 144;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, now));
        CHECK(!state.relative);
    }
}

static void test_transition_flicker(void) {
    WindowsPointerDetection state = {0};
    WindowsPointerObservation sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    sample.raw_x = 8;
    sample.motion_packets = 1;
    for (unsigned i = 1; i <= 32; ++i) {
        sample.position.x += 8;
        sample.cursor_flags = i % 2 ? 0 : CURSOR_SHOWING;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, i * 16));
    }
    sample.cursor_flags = 0;
    for (unsigned t = 528; t <= 768; t += 16)
        bongo_cat_windows_pointer_detect(&state, &sample, t);
    CHECK(state.relative);
    for (unsigned i = 1; i <= 16; ++i) {
        sample.cursor_flags = i % 2 ? CURSOR_SHOWING : 0;
        CHECK(bongo_cat_windows_pointer_detect(&state, &sample, 768 + i * 16));
    }
}

static void test_slow_sampling_and_edge_returns(void) {
    WindowsPointerDetection state = {0};
    WindowsPointerObservation sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    sample.raw_x = 20;
    sample.motion_packets = 1;
    for (unsigned t = 60; t < 240; t += 60)
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, t));
    CHECK(bongo_cat_windows_pointer_detect(&state, &sample, 240));
    state = (WindowsPointerDetection){0};
    sample = observation();
    sample.position.x = 1910;
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    for (unsigned i = 1; i <= 32; ++i) {
        sample.position.x = i % 2 ? 1919 : 1910;
        sample.raw_x = 20;
        sample.motion_packets = 1;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, i * 16));
    }
    state = (WindowsPointerDetection){0};
    sample = observation();
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    for (unsigned i = 1; i <= 16; ++i) {
        /* One large packet is not sustained movement over the time window. */
        sample.raw_x = i == 1 ? 10000 : 0;
        sample.motion_packets = i == 1 ? 8000 : 0;
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, i * 16));
    }
}

static void test_hidden_desktop_and_resume(void) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        WindowsPointerDetection state = {0};
        WindowsPointerObservation sample = observation();
        sample.cursor_flags = 0;
        if (scenario == 2) {
            sample.monitor = sample.clip = (RECT){-1920, -1080, 0, 0};
            sample.position = (POINT){-1920, -500};
        }
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
        sample.motion_packets = 1;
        for (unsigned i = 1; i <= 64; ++i) {
            sample.raw_x = 8;
            if (scenario == 0) sample.position.x += 8;
            if (scenario == 1) sample.position.x += 1;
            if (scenario == 3) {
                sample.raw_x = i % 2 ? 14 : -14;
                sample.position.x = i % 2 ? 974 : 960;
            }
            CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, i * 16));
        }
    }
    WindowsPointerDetection state = {0};
    WindowsPointerObservation sample = observation();
    sample.cursor_flags = 0;
    CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
    sample.raw_x = 12;
    sample.motion_packets = 1;
    for (unsigned t = 16; t <= 256; t += 16) {
        sample.position.x = t % 32 ? 974 : 960;
        bongo_cat_windows_pointer_detect(&state, &sample, t);
    }
    CHECK(state.relative && state.reason == WINDOWS_POINTER_HIDDEN);
    /* Normal coordinate travel must release even while the cursor is hidden. */
    for (unsigned t = 272; t <= 640; t += 16) {
        sample.position.x += 12;
        bongo_cat_windows_pointer_detect(&state, &sample, t);
    }
    CHECK(!state.relative);
}

static void test_absolute_observation(void) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        WindowsPointerDetection state = {0};
        WindowsPointerObservation sample = observation();
        CHECK(!bongo_cat_windows_pointer_detect(&state, &sample, 0));
        sample.motion_packets = 1;
        for (unsigned i = 1; i <= 32; ++i) {
            sample.absolute_x = 8;
            if (scenario == 1) sample.position.x += 8;
            if (scenario == 2) sample.absolute_x = i % 2 ? 8 : -8;
            if (scenario == 3) {
                /* Individually subthreshold signals must not be added. */
                sample.absolute_x = 2;
                sample.raw_x = 2;
            }
            bool relative = bongo_cat_windows_pointer_detect(&state, &sample, i * 16);
            CHECK(relative == (scenario == 0 && i >= 8));
        }
    }
}

void test_windows_pointer_detection(void) {
    test_hidden_desktop_and_resume();
    test_absolute_observation();
    test_clip_bounds();
    test_stalled_and_resume();
    test_recenter_and_real_travel();
    test_desktop_edges_and_slow_motion();
    test_hidden_and_clipped();
    test_focus_suspend_and_missing_position();
    test_transition_flicker();
    test_slow_sampling_and_edge_returns();
}
