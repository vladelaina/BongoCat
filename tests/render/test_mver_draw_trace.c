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
/* TG-02/04/05/06: count a separate production copy, then also check the
   ordinary production entry point from the identical starting state. */
static void counted_phase(BongoCatMverPointerOverlay *value, bool after,
    unsigned expected_calls) {
    BongoCatMverPointerOverlay direct = *value;
    unsigned calls = mver_geometry_calls, reference = mver_reference_geometry_calls;
    mver_probe_reset();
    if (after) mver_counted_reference_after(value);
    else mver_counted_reference_before(value);
    expected = mver_trace;
    CHECK(mver_reference_geometry_calls - reference ==
        (unsigned)(value->enabled && (!after || !value->mouse)));
    mver_probe_reset();
    if (after) mver_counted_after(value); else mver_counted_before(value);
    CHECK(mver_geometry_calls - calls == expected_calls);
    CHECK(mver_trace.size == expected.size && mver_trace.draws == expected.draws);
    CHECK(memcmp(mver_trace.bytes, expected.bytes, expected.size) == 0);
    mver_probe_reset();
    if (after) bongo_cat_mver_pointer_overlay_draw_after_keys(&direct);
    else bongo_cat_mver_pointer_overlay_draw_before_keys(&direct);
    CHECK(mver_trace.size == expected.size && mver_trace.draws == expected.draws);
    CHECK(memcmp(mver_trace.bytes, expected.bytes, expected.size) == 0);
    comparisons += 2; compared_bytes += 2 * expected.size;
}
static void phase_order_and_dimensions(void) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    value.mouse = false; value.x_ratio = .25f; value.y_ratio = .125f;
    counted_phase(&value, true, 1);  /* Cold after-only call. */
    counted_phase(&value, true, 0);  /* Repeated after-only call. */
    counted_phase(&value, false, 0);
    value.left_handed = true;       /* Hand changes between real phases. */
    counted_phase(&value, true, 1);
    counted_phase(&value, true, 0);
    BongoCatPointerTexture *textures[] = {&value.device, &value.left, &value.right};
    for (size_t i = 0; i < sizeof(textures) / sizeof(*textures); i++) {
        value.left_down = i == 1; value.right_down = i == 2;
        counted_phase(&value, true, 0);
        textures[i]->width += 37;
        counted_phase(&value, true, 0);
        textures[i]->height += 23;
        counted_phase(&value, true, 0);
    }
    value = mver_probe_overlay(); value.mouse = false;
    counted_phase(&value, false, 1); /* Cold and repeated before-only calls. */
    counted_phase(&value, false, 0);
}
static void interleaved_lifecycle(void) {
    BongoCatMverPointerOverlay first = mver_probe_overlay();
    BongoCatMverPointerOverlay second = mver_probe_overlay();
    first.mouse = second.mouse = false;
    first.x_ratio = .125f; first.y_ratio = .25f;
    second.x_ratio = .75f; second.y_ratio = .875f;
    second.left_handed = true;
    second.geometry = (BongoCatMverPointerConfig){17, -23, 31, -47};
    /* Distinct fake resources as well as distinct input/cache storage. */
    second.arm.id += 100; second.device.id += 100;
    second.left.id += 100; second.right.id += 100; second.side.id += 100;
    second.program += 100; second.vao += 100; second.vbo += 100;
    counted_phase(&first, false, 1); counted_phase(&second, true, 1);
    counted_phase(&first, true, 0); counted_phase(&second, false, 0);
    const BongoCatMverPointerOverlay saved = second;
    const char *fixtures[] = {"/valid", "/valid", "/disabled", "/valid", "/missing"};
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures); i++) {
        char path[1024]; BongoCatError error = {0};
        int length = snprintf(path, sizeof(path), "%s%s",
            BONGO_CAT_MVER_FIXTURE_DIR, fixtures[i]);
        CHECK(length > 0 && (size_t)length < sizeof(path));
        mver_probe_reset();
        bool result = bongo_cat_mver_pointer_overlay_load(&first, path, &error);
        CHECK(result == (i != 4));
        CHECK(!first.geometry_cache_valid);
        CHECK(first.enabled == (i != 2 && i != 4));
        CHECK(second.geometry_cache_valid);
        CHECK(second.geometry_cache_left_handed == saved.geometry_cache_left_handed);
        CHECK(memcmp(second.geometry_cache_key, saved.geometry_cache_key,
            sizeof(saved.geometry_cache_key)) == 0);
        CHECK(memcmp(&second.cached_geometry, &saved.cached_geometry,
            sizeof(saved.cached_geometry)) == 0);
        counted_phase(&second, true, 0);
        counted_phase(&first, true, (unsigned)first.enabled);
        counted_phase(&second, false, 0);
    }
}
int main(void) {
    grid(); changed_state(); special_values(); lifecycle();
    phase_order_and_dimensions(); interleaved_lifecycle();
    printf("Mver ordered GL trace: %u phase comparisons, %zu exact bytes, "
        "%d failures; targeted geometry calls=%u, reference calls=%u\n",
        comparisons, compared_bytes, bongo_cat_test_failures,
        mver_geometry_calls, mver_reference_geometry_calls);
    return bongo_cat_test_failures ? 1 : 0;
}
