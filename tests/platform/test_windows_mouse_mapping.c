#include "test.h"
#include "mouse_internal.h"
#include "windows_input_internal.h"

#include <math.h>
#include <stdlib.h>

static void expect_position(BongoCatApp *app, const BongoCatMouseProjection *projection,
    bool relative, double seed_x, double seed_y, float expected_x, float expected_y,
    bool expected_change) {
    double x, y;
    bool changed;
    bool mapped = bongo_cat_app_map_pointer(app, projection, relative,
        seed_x, seed_y, &x, &y, &changed);
    CHECK(mapped);
    if (!mapped) return;
    float ratio_x, ratio_y;
    bool normalized = bongo_cat_mver_pointer_ratios(x, y, &projection->bounds,
        &ratio_x, &ratio_y);
    CHECK(normalized);
    if (normalized) {
        CHECK(fabsf(ratio_x - expected_x) < 0.0001f);
        CHECK(fabsf(ratio_y - expected_y) < 0.0001f);
    }
    CHECK(changed == expected_change);
}

static void test_display_projection(BongoCatApp *app,
    BongoCatMverPointerBounds bounds) {
    WindowsInputState input = {0};
    WindowsRawDevice device = {0};
    InitializeSRWLock(&input.relative_lock);
    app->platform.native = &input;
    app->mver_pointer = (BongoCatMverPointerState){0};
    bongo_cat_windows_input_reset_relative(&app->platform);
    BongoCatMouseProjection projection = {.bounds = bounds, .centered = true,
        .center_x = bounds.left + bounds.width * 0.5,
        .center_y = bounds.top + bounds.height * 0.5};
    /* The pet display differs from the model's authored workarea. Tracking
       must use the same per-update projection as model output. */
    app->settings.model.mouse_centered = true;
    RAWMOUSE mouse = {.lLastX = (LONG)(bounds.width * 0.1),
        .lLastY = (LONG)(bounds.height * 0.1)};
    bongo_cat_windows_input_motion(&input, &device, &mouse, NULL);
    expect_position(app, &projection, true, projection.center_x, projection.center_y,
        0.6f, 0.6f, true);
    /* An unavailable source cannot replace the virtual point with a cursor
       position from another monitor. A delivered packet remains usable. */
    expect_position(app, &projection, true, 9000, 9000, 0.6f, 0.6f, false);
    mouse.lLastX = -mouse.lLastX;
    mouse.lLastY = -mouse.lLastY;
    bongo_cat_windows_input_motion(&input, &device, &mouse, NULL);
    expect_position(app, &projection, true, 9000, 9000, 0.5f, 0.5f, true);
    expect_position(app, &projection, true, 9000, 9000, 0.5f, 0.5f, false);
    mouse.lLastX = mouse.lLastY = 10000;
    bongo_cat_windows_input_motion(&input, &device, &mouse, NULL);
    expect_position(app, &projection, true, 9000, 9000, 1.0f, 1.0f, true);
    mouse.lLastX = (LONG)(-bounds.width * 0.1);
    mouse.lLastY = (LONG)(-bounds.height * 0.1);
    bongo_cat_windows_input_motion(&input, &device, &mouse, NULL);
    expect_position(app, &projection, true, 9000, 9000, 0.9f, 0.9f, true);
    expect_position(app, &projection, false, bounds.left + bounds.width * 0.25,
        bounds.top + bounds.height * 0.25, 0.25f, 0.25f, true);
    app->platform.native = NULL;
}

void test_windows_mouse_mapping(void) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) return;
    app->model_render_options.mver_projection = true;
    app->model_render_options.custom_pointer_bounds = true;
    app->model_render_options.pointer_left = 100;
    app->model_render_options.pointer_top = 200;
    app->model_render_options.pointer_right = 900;
    app->model_render_options.pointer_bottom = 800;
    BongoCatMouseProjection custom;
    bool resolved = bongo_cat_app_mouse_projection(app, 500, 500, &custom);
    CHECK(resolved);
    if (resolved) {
        CHECK(!custom.centered);
        CHECK(custom.bounds.left == 100 && custom.bounds.top == 200);
        CHECK(custom.bounds.width == 800 && custom.bounds.height == 600);
        expect_position(app, &custom, false, 500, 500, 0.5f, 0.5f, true);
    }
    test_display_projection(app, (BongoCatMverPointerBounds){0, 0, 1920, 1080});
    test_display_projection(app, (BongoCatMverPointerBounds){-1920, -200, 1920, 1080});
    test_display_projection(app, (BongoCatMverPointerBounds){2560, 0, 1080, 1920});
    free(app);
}
