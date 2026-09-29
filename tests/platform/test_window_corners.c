#include "window_corner_policy.h"
#include "test.h"

int bongo_cat_test_failures;

int main(void) {
    /* No expansion: preserve the existing short-edge percentage. */
    BongoCatCornerRect plain = bongo_cat_corner_rect(640, 320,
        true, 0, 0, 640, 320, 20.0f);
    CHECK(plain.x == 0 && plain.y == 0);
    CHECK(plain.width == 640 && plain.height == 320);
    CHECK(plain.radius_milli == 64000);

    /* Asymmetric extension moves the content origin, not its rounding size. */
    BongoCatCornerRect expanded = bongo_cat_corner_rect(880, 480,
        true, 160, 40, 640, 320, 20.0f);
    CHECK(expanded.x == 160 && expanded.y == 40);
    CHECK(expanded.width == plain.width && expanded.height == plain.height);
    CHECK(expanded.radius_milli == plain.radius_milli);

    /* The viewport already accounts for vertical flip and fallback fitting. */
    BongoCatCornerRect flipped = bongo_cat_corner_rect(880, 480,
        true, 160, 120, 640, 320, 20.0f);
    CHECK(flipped.y == 120 && flipped.radius_milli == plain.radius_milli);
    BongoCatCornerRect fitted = bongo_cat_corner_rect(880, 480,
        true, 200, 80, 320, 160, 20.0f);
    CHECK(fitted.radius_milli == plain.radius_milli / 2);

    BongoCatCornerRect dpi = bongo_cat_corner_rect(1760, 960,
        true, 320, 80, 1280, 640, 20.0f);
    CHECK(dpi.x == expanded.x * 2 && dpi.y == expanded.y * 2);
    CHECK(dpi.radius_milli == expanded.radius_milli * 2);

    /* OBS/background mode rounds the complete filled surface. */
    BongoCatCornerRect background = bongo_cat_corner_rect(880, 480,
        false, 160, 40, 640, 320, 20.0f);
    CHECK(background.x == 0 && background.y == 0);
    CHECK(background.width == 880 && background.height == 480);
    CHECK(background.radius_milli == 96000);

    BongoCatCornerRect stale = bongo_cat_corner_rect(320, 160,
        true, 160, 40, 640, 320, 20.0f);
    CHECK(stale.x == 0 && stale.width == 320 && stale.height == 160);
    CHECK(stale.radius_milli == 32000);
    CHECK(bongo_cat_corner_rect(320, 160,
        true, -1, 0, 100, 100, 20.0f).width == 320);
    CHECK(bongo_cat_corner_rect(320, 160,
        true, 0, 0, 0, 0, 20.0f).width == 320);

    CHECK(bongo_cat_corner_rect(640, 320,
        false, 0, 0, 0, 0, 0.0f).radius_milli == 0);
    CHECK(bongo_cat_corner_rect(640, 320,
        false, 0, 0, 0, 0, -5.0f).radius_milli == 0);
    CHECK(bongo_cat_corner_rect(640, 320,
        false, 0, 0, 0, 0, NAN).radius_milli == 0);
    CHECK(bongo_cat_corner_rect(640, 320,
        false, 0, 0, 0, 0, INFINITY).radius_milli == 0);
    CHECK(bongo_cat_corner_rect(640, 320,
        false, 0, 0, 0, 0, 90.0f).radius_milli == 160000);
    CHECK(bongo_cat_corner_rect(0, 320,
        false, 0, 0, 0, 0, 20.0f).radius_milli == 0);
    return bongo_cat_test_failures ? 1 : 0;
}
