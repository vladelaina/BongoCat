#include "mver_pointer_overlay_internal.h"
#include "test.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned computations, comparisons;

bool mver_cache_counted_geometry(float x, float y,
    const BongoCatMverPointerConfig *config, BongoCatMverPointerGeometry *output) {
    computations++;
    return bongo_cat_mver_pointer_geometry(x, y, config, output);
}

static bool compare(BongoCatMverPointerOverlay *value) {
    BongoCatMverPointerGeometry expected, actual;
    memset(&expected, 0xa5, sizeof(expected));
    memset(&actual, 0xa5, sizeof(actual));
    float x = value->left_handed ? 1.0f - value->x_ratio : value->x_ratio;
    bool result = bongo_cat_mver_pointer_geometry(x, value->y_ratio,
        &value->geometry, &expected);
    CHECK(bongo_cat_mver_pointer_overlay_geometry(value, &actual) == result);
    CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
    comparisons++;
    return result;
}

static void dense_grid(void) {
    BongoCatMverPointerOverlay value = {0};
    for (int config = 0; config < 4; config++) {
        value.geometry = (BongoCatMverPointerConfig){
            config * 13.125f, config * -21.5f, config * 7.25f, config * -3.875f};
        for (int hand = 0; hand < 2; hand++) {
            value.left_handed = hand != 0;
            for (int x = -8; x <= 40; x++) {
                for (int y = -8; y <= 40; y++) {
                    value.x_ratio = (float)x / 32.0f;
                    value.y_ratio = (float)y / 32.0f;
                    CHECK(compare(&value));
                    unsigned before = computations;
                    CHECK(compare(&value));
                    CHECK(computations == before);
                }
            }
        }
    }
}

static void boundaries(void) {
    const float special[] = {0.0f, -0.0f, 1.0f, -1.0f, FLT_MAX, -FLT_MAX,
        FLT_MIN, -FLT_MIN, INFINITY, -INFINITY, NAN};
    BongoCatMverPointerOverlay value = {0};
    for (size_t x = 0; x < sizeof(special) / sizeof(*special); x++) {
        for (size_t y = 0; y < sizeof(special) / sizeof(*special); y++) {
            for (int hand = 0; hand < 2; hand++) {
                value.x_ratio = special[x]; value.y_ratio = special[y];
                value.left_handed = hand != 0;
                bool valid = compare(&value);
                unsigned before = computations;
                CHECK(compare(&value) == valid);
                CHECK(computations == before + (unsigned)!valid);
            }
        }
    }
    value = (BongoCatMverPointerOverlay){0};
    CHECK(compare(&value));
    unsigned before = computations;
    value.x_ratio = -0.0f;
    CHECK(compare(&value));
    CHECK(computations == before + 1);
    value.x_ratio = nextafterf(0.0f, 1.0f);
    CHECK(compare(&value));
    CHECK(computations == before + 2);
    value.geometry.offset_x = INFINITY;
    CHECK(!compare(&value));
    CHECK(!value.geometry_cache_valid);
    value.geometry.offset_x = 0.0f;
    CHECK(compare(&value));
    CHECK(value.geometry_cache_valid);
}

static void key_and_phase_changes(void) {
    BongoCatMverPointerOverlay value = {0};
    CHECK(compare(&value));
    float *fields[] = {&value.x_ratio, &value.y_ratio, &value.geometry.offset_x,
        &value.geometry.offset_y, &value.geometry.hand_offset_x,
        &value.geometry.hand_offset_y};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
        unsigned before = computations;
        *fields[i] += .25f;
        CHECK(compare(&value));
        CHECK(computations == before + 1);
        CHECK(compare(&value));
        CHECK(computations == before + 1);
    }
    unsigned before = computations;
    value.left_handed = true;
    CHECK(compare(&value));
    CHECK(computations == before + 1);
    /* A mid-frame anchor update must be checked again before the second draw. */
    value.x_ratio = .75f; value.y_ratio = .125f;
    CHECK(compare(&value));
    CHECK(computations == before + 2);
    value.left_down = value.right_down = value.side_down = true;
    value.scale = 2.0f; value.reference_width = 800;
    CHECK(compare(&value));
    CHECK(computations == before + 2);
    value.geometry_cache_valid = false;
    CHECK(compare(&value));
    CHECK(computations == before + 3);
    BongoCatMverPointerGeometry output;
    CHECK(!bongo_cat_mver_pointer_overlay_geometry(NULL, &output));
    CHECK(!bongo_cat_mver_pointer_overlay_geometry(&value, NULL));
}

static void unusual_bit_patterns(void) {
    BongoCatMverPointerOverlay value = {0};
    const uint32_t bits[] = {0, 0x80000000u, 1, 0x80000001u,
        0x3f000001u, 0x3f7fffffu, 0x7fc00001u, 0x7fc12345u,
        0xffc00001u, 0x7f800000u, 0xff800000u, 0x7f7fffffu};
    float *fields[] = {&value.x_ratio, &value.y_ratio, &value.geometry.offset_x,
        &value.geometry.offset_y, &value.geometry.hand_offset_x,
        &value.geometry.hand_offset_y};
    for (size_t field = 0; field < sizeof(fields) / sizeof(*fields); field++) {
        for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++) {
            memcpy(fields[field], &bits[i], sizeof(bits[i]));
            bool valid = compare(&value);
            unsigned before = computations;
            CHECK(compare(&value) == valid);
            CHECK(computations == before + (unsigned)!valid);
        }
        *fields[field] = 0;
    }
    uint32_t state = 0x613459afu;
    for (unsigned i = 0; i < 1000; i++) {
        for (size_t field = 0; field < sizeof(fields) / sizeof(*fields); field++) {
            state = state * 1664525u + 1013904223u;
            memcpy(fields[field], &state, sizeof(state));
        }
        value.left_handed = (i & 1) != 0;
        bool valid = compare(&value);
        unsigned before = computations;
        CHECK(compare(&value) == valid);
        CHECK(computations == before + (unsigned)!valid);
    }
}

int main(void) {
    dense_grid(); boundaries(); key_and_phase_changes(); unusual_bit_patterns();
    printf("Mver cache: %u byte-exact geometry comparisons, %u computations, "
        "%d failures\n", comparisons, computations, bongo_cat_test_failures);
    return bongo_cat_test_failures ? 1 : 0;
}
