#include "model_frame_policy.h"
#include "test.h"

#include <cmath>
#include <limits>

int bongo_cat_test_failures;

static void contains(BongoCatLive2DFrame allocated, BongoCatLive2DFrame required,
    int width, int height, bool flip) {
    BongoCatFrameViewport v = bongo_cat_frame_viewport(allocated, required,
        width, height, flip);
    required = bongo_cat_frame_mix(allocated, required, 1.0);
    double top = flip ? required.bottom : required.top;
    double bottom = flip ? required.top : required.bottom;
    /* Allow integer raster rounding at the outermost boundary. */
    CHECK(v.x - required.left * v.width >= -1.0);
    CHECK(v.x + (1.0 + required.right) * v.width <= width + 1.0);
    CHECK(v.y - bottom * v.height >= -1.0);
    CHECK(v.y + (1.0 + top) * v.height <= height + 1.0);
    CHECK(v.width > 0 && v.height > 0 && v.scale > 0 && v.scale <= 1.0f);
}

int main() {
    BongoCatLive2DFrame empty{};
    CHECK(bongo_cat_frame_equal(empty,
        bongo_cat_frame_observe(empty, -.8f, -.9f, .9f, .8f)));
    auto extended = bongo_cat_frame_observe(empty, -1.3f, -.8f, .8f, 1.1f);
    CHECK(extended.left >= .15f && extended.top >= .05f);
    CHECK(extended.right == 0 && extended.bottom == 0);
    for (int i = 0; i < 1000; ++i)
        CHECK(bongo_cat_frame_equal(extended,
            bongo_cat_frame_observe(extended, -1.3f, -.8f, .8f, 1.1f)));
    CHECK(bongo_cat_frame_equal(extended,
        bongo_cat_frame_observe(extended, -.8f, -.8f, .8f, .8f)));
    CHECK(bongo_cat_frame_equal(extended, bongo_cat_frame_observe(extended,
        std::numeric_limits<float>::quiet_NaN(), -2, 3, 4)));
    CHECK(bongo_cat_frame_equal(extended,
        bongo_cat_frame_observe(extended, 2, -2, -2, 2)));

    /* Modest extension preserves the content's pixel dimensions and origin. */
    BongoCatLive2DFrame normal{.25f, .125f, .125f, 0};
    auto v = bongo_cat_frame_viewport(normal, normal, 880, 360, false);
    CHECK(v.width == 640 && v.height == 320 && v.x == 160 && v.y == 0);
    auto flipped = bongo_cat_frame_viewport(normal, normal, 880, 360, true);
    CHECK(flipped.width == v.width && flipped.height == v.height);
    CHECK(flipped.x == v.x && flipped.y == 40);
    int original_x = 300, original_y = 240;
    int expanded_x = original_x - 160, expanded_y = original_y - 40;
    CHECK(expanded_x + v.x == original_x);
    CHECK(expanded_y + 360 - v.y - v.height == original_y);

    for (int left = 0; left < 8; ++left) {
        for (int top = 0; top < 8; ++top) {
            BongoCatLive2DFrame requested{left * .25f, top * .25f,
                top * .125f, left * .125f};
            auto allocated = bongo_cat_frame_limit(empty, requested, 2.0, 2.0, 2.0);
            CHECK(bongo_cat_frame_area(allocated) <= 2.0);
            CHECK(allocated.left <= requested.left && allocated.top <= requested.top);
            CHECK(allocated.right <= requested.right && allocated.bottom <= requested.bottom);
            auto again = bongo_cat_frame_limit(allocated, requested, 2.0, 2.0, 2.0);
            CHECK(bongo_cat_frame_equal(allocated, again));
            contains(allocated, requested, 640, 360, false);
            contains(allocated, requested, 640, 360, true);
        }
    }
    BongoCatLive2DFrame huge{10, 4, 8, 9};
    auto pixels_limited = bongo_cat_frame_limit(empty, huge, 1.125, 1.25, 1.5);
    CHECK(bongo_cat_frame_area(pixels_limited) <= 1.125);
    CHECK(bongo_cat_frame_equal(normal,
        bongo_cat_frame_limit(normal, huge, 1.0, 1.0, 1.0)));
    contains(empty, huge, 612, 354, false);
    return bongo_cat_test_failures ? 1 : 0;
}
