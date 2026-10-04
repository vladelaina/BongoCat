#include "test.h"
#include "bongo_cat/mver_pointer.h"

#include <math.h>

static bool close_to(float actual, float expected) {
    return fabsf(actual - expected) < 0.001f;
}

void test_mver_pointer(void) {
    BongoCatMverPointerBounds bounds = {100.0, 200.0, 2000.0, 1200.0};
    float x_ratio = -1.0f, y_ratio = -1.0f;
    CHECK(bongo_cat_mver_pointer_ratios(1100.0, 500.0, &bounds,
        &x_ratio, &y_ratio));
    CHECK(close_to(x_ratio, 0.5f));
    CHECK(close_to(y_ratio, 0.25f));
    CHECK(bongo_cat_mver_pointer_ratios(-100.0, 2000.0, &bounds,
        &x_ratio, &y_ratio));
    CHECK(close_to(x_ratio, 0.0f));
    CHECK(close_to(y_ratio, 1.0f));
    bounds = (BongoCatMverPointerBounds){-1920.0, -1200.0, 1920.0, 1200.0};
    CHECK(bongo_cat_mver_pointer_ratios(-960.0, -600.0, &bounds,
        &x_ratio, &y_ratio));
    CHECK(close_to(x_ratio, 0.5f));
    CHECK(close_to(y_ratio, 0.5f));
    bounds.width = 0.0;
    CHECK(!bongo_cat_mver_pointer_ratios(100.0, 200.0, &bounds,
        &x_ratio, &y_ratio));

    BongoCatMverPointerConfig config = {
        .offset_x = 10.0f,
        .offset_y = -10.0f,
        .hand_offset_x = 3.0f,
        .hand_offset_y = -4.0f
    };
    BongoCatMverPointerGeometry top_left, bottom_right;
    CHECK(bongo_cat_mver_pointer_geometry(0.0f, 0.0f, &config, &top_left));
    CHECK(bongo_cat_mver_pointer_geometry(1.0f, 1.0f, &config, &bottom_right));
    CHECK(close_to(top_left.arm[0].x, 176.0f));
    CHECK(close_to(top_left.arm[0].y, 105.0f));
    CHECK(close_to(top_left.arm[25].x, 223.0f));
    CHECK(close_to(top_left.arm[25].y, 174.0f));
    CHECK(close_to(bottom_right.arm[0].x, top_left.arm[0].x));
    CHECK(close_to(bottom_right.arm[0].y, top_left.arm[0].y));
    CHECK(close_to(bottom_right.arm[25].x, top_left.arm[25].x));
    CHECK(close_to(bottom_right.arm[25].y, top_left.arm[25].y));
    CHECK(bottom_right.device_x < top_left.device_x);
    CHECK(bottom_right.device_y < top_left.device_y);

    BongoCatMverPointerGeometry clamped;
    CHECK(bongo_cat_mver_pointer_geometry(-2.0f, 3.0f, &config, &clamped));
    BongoCatMverPointerGeometry corner;
    CHECK(bongo_cat_mver_pointer_geometry(0.0f, 1.0f, &config, &corner));
    CHECK(close_to(clamped.device_x, corner.device_x));
    CHECK(close_to(clamped.device_y, corner.device_y));

    BongoCatMverPointerState state = {0};
    bounds = (BongoCatMverPointerBounds){100.0, 200.0, 2000.0, 1200.0};
    double pointer_x = 0.0, pointer_y = 0.0;
    CHECK(bongo_cat_mver_pointer_update(&state, 1100.0, 500.0,
        25.0, -10.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 1125.0 && pointer_y == 490.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 400.0, 1200.0,
        0.0, 0.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 1125.0 && pointer_y == 490.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 1400.0, 900.0,
        5000.0, -5000.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 2100.0 && pointer_y == 200.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 800.0, 700.0,
        0.0, 0.0, false, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 800.0 && pointer_y == 700.0);

    state = (BongoCatMverPointerState){0};
    bounds = (BongoCatMverPointerBounds){0.0, 0.0, 3440.0, 1440.0};
    CHECK(bongo_cat_mver_pointer_update(&state, 1720.0, 720.0,
        0.0, 0.0, false, &bounds, &pointer_x, &pointer_y));
    for (int index = 0; index < 10; ++index)
        CHECK(bongo_cat_mver_pointer_update(&state, 1720.0, 720.0,
            -1000.0, -1000.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 0.0 && pointer_y == 0.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 1720.0, 720.0,
        5.0, 7.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 5.0 && pointer_y == 7.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 1720.0, 720.0,
        0.0, 0.0, true, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 5.0 && pointer_y == 7.0);
    CHECK(bongo_cat_mver_pointer_update(&state, 2700.0, 900.0,
        0.0, 0.0, false, &bounds, &pointer_x, &pointer_y));
    CHECK(pointer_x == 2700.0 && pointer_y == 900.0);
}
