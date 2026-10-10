#define _POSIX_C_SOURCE 200809L
#include "mver_probe.h"
#include "mver_reference_layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Scenario {
    const char *name;
    bool geometry_only, moving, mouse, midframe;
    bool left_handed, handedness, after_only, invalid_recovery;
} Scenario;
static const Scenario scenarios[] = {
    {"geometry-stationary", .geometry_only = true, .mouse = true},
    {"geometry-changing", .geometry_only = true, .moving = true, .mouse = true},
    {"mouse-stationary", .mouse = true},
    {"mouse-changing", .moving = true, .mouse = true},
    {.name = "tablet-stationary"},
    {"tablet-changing", .moving = true},
    {"tablet-midframe-changing", .moving = true, .midframe = true},
    {"tablet-left-handed", .left_handed = true},
    {"tablet-handedness-midframe", .handedness = true},
    {"tablet-after-only-changing", .moving = true, .after_only = true},
    {"tablet-invalid-recovery", .invalid_recovery = true}
};
static uint64_t clock_ns(clockid_t which) {
    struct timespec value;
    if (clock_gettime(which, &value)) abort();
    return (uint64_t)value.tv_sec * 1000000000ull + (uint64_t)value.tv_nsec;
}
static void anchor(BongoCatMverPointerOverlay *value, unsigned frame) {
    value->x_ratio = (float)(frame % 997) / 996;
    value->y_ratio = (float)((frame * 37) % 991) / 990;
}
static BongoCatMverPointerOverlay fixture(const Scenario *scenario) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    value.mouse = scenario->mouse;
    value.left_handed = scenario->left_handed;
    value.left_down = value.right_down = value.side_down = true;
    if (scenario->left_handed || scenario->handedness) value.x_ratio = .25f;
    /* Warm the same single-entry cache outside every batch, including checks. */
    BongoCatMverPointerGeometry output;
    if (!bongo_cat_mver_pointer_overlay_geometry(&value, &output)) abort();
    return value;
}
static void begin_frame(const Scenario *scenario,
    BongoCatMverPointerOverlay *value, unsigned frame) {
    if (scenario->moving) anchor(value, frame);
    /* Two failing pairs, then recovery and reuse. Failed results stay cold. */
    if (scenario->invalid_recovery) value->x_ratio = frame % 4 < 2 ? NAN : .25f;
}
static void after_inputs(const Scenario *scenario,
    BongoCatMverPointerOverlay *value, unsigned frame) {
    if (scenario->midframe) anchor(value, frame + 503);
    if (scenario->handedness) value->left_handed = !value->left_handed;
}
typedef struct Timing { uint64_t wall, cpu; } Timing;
static Timing run(const Scenario *scenario, bool cached,
    unsigned start, unsigned count) {
    BongoCatMverPointerOverlay value = fixture(scenario);
    BongoCatMverPointerGeometry output;
    mver_probe_reset();
    uint64_t cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t wall = clock_ns(CLOCK_MONOTONIC);
    for (unsigned i = 0; i < count; i++) {
        begin_frame(scenario, &value, start + i);
        if (scenario->geometry_only) {
            bool valid = cached
                ? bongo_cat_mver_pointer_overlay_geometry(&value, &output)
                : bongo_cat_mver_pointer_geometry(value.x_ratio, value.y_ratio,
                    &value.geometry, &output);
            if (!valid) abort();
            mver_probe_consume_geometry(&output);
        } else {
            if (!scenario->after_only) {
                if (cached) bongo_cat_mver_pointer_overlay_draw_before_keys(&value);
                else mver_reference_before(&value);
            }
            after_inputs(scenario, &value, start + i);
            if (cached) bongo_cat_mver_pointer_overlay_draw_after_keys(&value);
            else mver_reference_after(&value);
        }
    }
    wall = clock_ns(CLOCK_MONOTONIC) - wall;
    cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    return (Timing){wall, cpu};
}
/* This independent oracle tracks only the inputs varied by these fixtures.
   It never reads production cache state. Both counters are untimed copies. */
typedef struct ExpectedCache { float x, y; bool hand, valid; } ExpectedCache;
typedef struct Preflight { uint64_t baseline, cached, phases, bytes; } Preflight;
static void require(bool success, const Scenario *scenario, unsigned frame,
    const char *check) {
    if (success) return;
    fprintf(stderr, "preflight failed: %s frame=%u %s\n", scenario->name,
        frame, check);
    exit(1);
}
static unsigned expected_cached(const Scenario *scenario, ExpectedCache *cache,
    const BongoCatMverPointerOverlay *value, bool valid, bool after) {
    if (scenario->mouse && !scenario->geometry_only) return after ? 0 : 1;
    bool hit = valid && cache->valid && cache->x == value->x_ratio &&
        cache->y == value->y_ratio && cache->hand == value->left_handed;
    *cache = (ExpectedCache){value->x_ratio, value->y_ratio,
        value->left_handed, valid};
    return hit ? 0 : 1;
}
static void check_phase(const Scenario *scenario,
    BongoCatMverPointerOverlay values[4], ExpectedCache *cache,
    Preflight *total, unsigned frame, bool after) {
    static MverTrace expected;
    BongoCatMverPointerGeometry geometry = {0};
    bool valid = !scenario->invalid_recovery || frame % 4 >= 2;
    unsigned baseline_calls = scenario->mouse && !scenario->geometry_only &&
        after ? 0 : 1;
    unsigned cached_calls = expected_cached(scenario, cache, values, valid, after);
    mver_geometry_calls = mver_reference_geometry_calls = 0;
    /* Compare the original timed paths and both instrumented copies. */
    for (unsigned variant = 0; variant < 4; variant++) {
        BongoCatMverPointerOverlay *value = &values[variant];
        mver_probe_reset();
        if (scenario->geometry_only) {
            BongoCatMverPointerGeometry output = {0};
            bool success;
            if (variant == 0) success = bongo_cat_mver_pointer_geometry(
                value->x_ratio, value->y_ratio, &value->geometry, &output);
            else if (variant == 1) success =
                bongo_cat_mver_pointer_overlay_geometry(value, &output);
            else if (variant == 2) success = mver_counted_reference_geometry(
                value->x_ratio, value->y_ratio, &value->geometry, &output);
            else success = mver_counted_overlay_geometry(value, &output);
            require(success == valid, scenario, frame, "geometry validity");
            if (variant == 0) geometry = output;
            else require(memcmp(&geometry, &output, sizeof(output)) == 0,
                scenario, frame, "geometry payload");
        } else {
            if (variant == 0) {
                if (after) mver_reference_after(value); else mver_reference_before(value);
            } else if (variant == 1) {
                if (after) bongo_cat_mver_pointer_overlay_draw_after_keys(value);
                else bongo_cat_mver_pointer_overlay_draw_before_keys(value);
            } else if (variant == 2) {
                if (after) mver_counted_reference_after(value);
                else mver_counted_reference_before(value);
            } else {
                if (after) mver_counted_after(value); else mver_counted_before(value);
            }
            if (variant == 0) expected = mver_trace;
            else require(mver_trace.size == expected.size &&
                mver_trace.draws == expected.draws &&
                memcmp(mver_trace.bytes, expected.bytes, expected.size) == 0,
                scenario, frame, "ordered draw payload");
            if (!valid) require(mver_trace.size == 0, scenario, frame,
                "invalid input drew stale geometry");
        }
    }
    require(mver_reference_geometry_calls == baseline_calls, scenario, frame,
        "baseline geometry calls");
    require(mver_geometry_calls == cached_calls, scenario, frame,
        "cached geometry calls");
    total->baseline += baseline_calls; total->cached += cached_calls;
    total->phases++;
    total->bytes += 3 * (scenario->geometry_only ? sizeof(geometry) : expected.size);
}
static void preflight(const Scenario *scenario, unsigned count, unsigned block) {
    Preflight total = {0};
    for (unsigned start = 0; start < count;) {
        unsigned amount = count - start < block ? count - start : block;
        BongoCatMverPointerOverlay values[4];
        for (unsigned v = 0; v < 4; v++) values[v] = fixture(scenario);
        ExpectedCache cache = {values[0].x_ratio, values[0].y_ratio,
            values[0].left_handed, true};
        for (unsigned i = 0; i < amount; i++) {
            unsigned frame = start + i;
            for (unsigned v = 0; v < 4; v++) begin_frame(scenario, &values[v], frame);
            if (!scenario->after_only)
                check_phase(scenario, values, &cache, &total, frame, false);
            if (!scenario->geometry_only) {
                for (unsigned v = 0; v < 4; v++) after_inputs(scenario, &values[v], frame);
                check_phase(scenario, values, &cache, &total, frame, true);
            }
        }
        start += amount;
    }
    fprintf(stderr, "preflight=%s iterations=%u block=%u baseline_geometry_calls=%llu "
        "cached_geometry_calls=%llu phases=%llu exact_bytes=%llu\n", scenario->name,
        count, block, (unsigned long long)total.baseline,
        (unsigned long long)total.cached, (unsigned long long)total.phases,
        (unsigned long long)total.bytes);
}
int main(int argc, char **argv) {
    bool checks_only = argc > 1 && strcmp(argv[1], "--preflight-only") == 0;
    if (checks_only) { argc--; argv++; }
    unsigned count = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2000;
    int trials = argc > 2 ? atoi(argv[2]) : 21;
    unsigned block = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 100;
    if (!count || !block || trials < 1) return 2;
    fprintf(stderr, "overlay_size_before=%zu overlay_size_after=%zu delta=%zu; "
        "no additional heap allocations; block=%u\n", sizeof(struct MverReferenceOverlay),
        sizeof(BongoCatMverPointerOverlay), sizeof(BongoCatMverPointerOverlay) -
        sizeof(struct MverReferenceOverlay), block);
    mver_trace_enabled = true;
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(*scenarios); i++)
        preflight(&scenarios[i], count, block);
    if (checks_only) return 0;
    mver_trace_enabled = false;
    mver_geometry_calls = mver_reference_geometry_calls = 0;
    puts("scenario,trial,variant,iterations,wall_ns,cpu_ns");
    for (int trial = -3; trial < trials; trial++) {
        for (size_t i = 0; i < sizeof(scenarios) / sizeof(*scenarios); i++) {
            Timing total[2] = {{0, 0}, {0, 0}};
            unsigned sequence = 0;
            for (unsigned start = 0; start < count;) {
                unsigned amount = count - start < block ? count - start : block;
                bool first_cached = ((unsigned)trial + sequence++) % 2 != 0;
                for (unsigned pair = 0; pair < 2; pair++) {
                    bool cached = pair ? !first_cached : first_cached;
                    Timing result = run(&scenarios[i], cached, start, amount);
                    total[cached].wall += result.wall;
                    total[cached].cpu += result.cpu;
                }
                start += amount;
            }
            if (trial >= 0) for (int cached = 0; cached < 2; cached++) {
                printf("%s,%d,%s,%u,%llu,%llu\n", scenarios[i].name, trial,
                    cached ? "cached" : "upstream", count,
                    (unsigned long long)total[cached].wall,
                    (unsigned long long)total[cached].cpu);
            }
        }
    }
    if (mver_geometry_calls || mver_reference_geometry_calls) {
        fputs("counted geometry entered the timing path\n", stderr);
        return 1;
    }
    fprintf(stderr, "payload_sink=%llu\n", (unsigned long long)mver_probe_sink);
    return 0;
}
