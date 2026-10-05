#include "mver_probe.h"
#include "test.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned comparisons;
static size_t compared_bytes;
static MverTrace expected;
static void compare_phase(BongoCatMverPointerOverlay *value, bool after) {
    mver_probe_reset();
    if (after) mver_reference_after(value); else mver_reference_before(value);
    expected = mver_trace;
    mver_probe_reset();
    if (after) bongo_cat_mver_pointer_overlay_draw_after_keys(value);
    else bongo_cat_mver_pointer_overlay_draw_before_keys(value);
    CHECK(mver_trace.size == expected.size);
    CHECK(mver_trace.draws == expected.draws);
    CHECK(memcmp(mver_trace.bytes, expected.bytes, expected.size) == 0);
    compared_bytes += expected.size;
    comparisons++;
}
static void frame(BongoCatMverPointerOverlay *value) {
    compare_phase(value, false); compare_phase(value, true);
}
static void grid(void) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    for (int hand = 0; hand < 2; hand++) {
        value.left_handed = hand != 0;
        for (int mouse = 0; mouse < 2; mouse++) {
            value.mouse = mouse != 0;
            for (int x = -1; x <= 5; x++) {
                for (int y = -1; y <= 5; y++) {
                    for (int buttons = 0; buttons < 8; buttons++) {
                        bongo_cat_mver_pointer_overlay_set(&value,
                            (float)x / 4, (float)y / 4,
                            (buttons & 1) != 0, (buttons & 2) != 0,
                            (buttons & 4) != 0);
                        frame(&value); frame(&value);
                    }
                }
            }
        }
    }
}
static void changed_state(void) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    value.mouse = false;
    /* A change between phases must not reuse the preceding anchor. */
    compare_phase(&value, false);
    bongo_cat_mver_pointer_overlay_set(&value, .75f, .125f, true, true, true);
    compare_phase(&value, true);
    float *fields[] = {&value.geometry.offset_x, &value.geometry.offset_y,
        &value.geometry.hand_offset_x, &value.geometry.hand_offset_y};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
        compare_phase(&value, false);
        *fields[i] += 23.125f;
        compare_phase(&value, true);
        frame(&value);
    }
    value.left_handed = true; frame(&value);
    /* These are deliberately not part of the geometry key. They must still
       affect the current draw on an otherwise cached frame. */
    value.scale = 1.75f; frame(&value);
    value.reference_width = 853; value.reference_height = 577; frame(&value);
    value.line_red = .875f; value.line_green = .125f; value.line_blue = .5f;
    frame(&value);
    value.right.id = 0; frame(&value);
    value.left.id = 0; frame(&value);
    value.device.id = 0; frame(&value);
    value.arm.id = 0; frame(&value);
    value.enabled = false; frame(&value);
    value.enabled = true; value.mouse = true; frame(&value);
    frame(NULL);
}
static void special_values(void) {
    const float special[] = {0, -0.0f, 1, -1, FLT_MAX, -FLT_MAX,
        FLT_MIN, -FLT_MIN, INFINITY, -INFINITY, NAN};
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    for (int hand = 0; hand < 2; hand++) {
        value.left_handed = hand != 0;
        for (int mouse = 0; mouse < 2; mouse++) {
            value.mouse = mouse != 0;
            for (size_t i = 0; i < sizeof(special) / sizeof(*special); i++) {
                for (size_t j = 0; j < sizeof(special) / sizeof(*special); j++) {
                    value.x_ratio = special[i]; value.y_ratio = special[j];
                    frame(&value); frame(&value);
                }
            }
        }
    }
    value = mver_probe_overlay();
    for (size_t i = 0; i < sizeof(special) / sizeof(*special); i++) {
        value.geometry = (BongoCatMverPointerConfig){special[i],
            -special[i], special[i], -special[i]};
        frame(&value); frame(&value);
    }
}
static void lifecycle(void) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    BongoCatError error = {0};
    value.mouse = false;
    frame(&value);
    CHECK(value.geometry_cache_valid);
    mver_probe_reset();
    CHECK(bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/disabled", &error));
    CHECK(!value.geometry_cache_valid && !value.enabled);
    frame(&value);
    mver_probe_reset();
    CHECK(bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/valid", &error));
    CHECK(!value.geometry_cache_valid && value.enabled);
    frame(&value); frame(&value);
    CHECK(value.geometry_cache_valid);
    mver_probe_reset();
    CHECK(bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/valid", &error));
    CHECK(!value.geometry_cache_valid);
    frame(&value);
    mver_probe_reset();
    CHECK(bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/disabled", &error));
    CHECK(!value.geometry_cache_valid && !value.enabled);
    frame(&value);
    mver_probe_reset();
    CHECK(bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/valid", &error));
    frame(&value);
    CHECK(value.geometry_cache_valid && value.enabled);
    mver_probe_reset();
    CHECK(!bongo_cat_mver_pointer_overlay_load(&value,
        BONGO_CAT_MVER_FIXTURE_DIR "/missing", &error));
    CHECK(!value.geometry_cache_valid && !value.enabled);
    frame(&value);
}
int main(void) {
    grid(); changed_state(); special_values(); lifecycle();
    printf("Mver ordered GL trace: %u phase comparisons, %zu exact bytes, "
        "%d failures\n", comparisons, compared_bytes, bongo_cat_test_failures);
    return bongo_cat_test_failures ? 1 : 0;
}
