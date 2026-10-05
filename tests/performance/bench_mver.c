#define _POSIX_C_SOURCE 200809L
#include "mver_probe.h"
#include "mver_reference_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Scenario {
    const char *name;
    bool geometry_only, moving, mouse, midframe;
} Scenario;
static const Scenario scenarios[] = {
    {"geometry-stationary", true, false, true, false},
    {"geometry-changing", true, true, true, false},
    {"mouse-stationary", false, false, true, false},
    {"mouse-changing", false, true, true, false},
    {"tablet-stationary", false, false, false, false},
    {"tablet-changing", false, true, false, false},
    {"tablet-midframe-changing", false, true, false, true}
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
typedef struct Timing { uint64_t wall, cpu; } Timing;
static Timing run(const Scenario *scenario, bool cached,
    unsigned start, unsigned count) {
    BongoCatMverPointerOverlay value = mver_probe_overlay();
    value.mouse = scenario->mouse;
    value.left_down = value.right_down = value.side_down = true;
    mver_probe_reset();
    /* Warm the single-entry cache outside every timed batch. */
    BongoCatMverPointerGeometry output;
    if (!bongo_cat_mver_pointer_overlay_geometry(&value, &output)) abort();
    uint64_t cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t wall = clock_ns(CLOCK_MONOTONIC);
    for (unsigned i = 0; i < count; i++) {
        if (scenario->moving) anchor(&value, start + i);
        if (scenario->geometry_only) {
            bool valid = cached
                ? bongo_cat_mver_pointer_overlay_geometry(&value, &output)
                : bongo_cat_mver_pointer_geometry(value.x_ratio, value.y_ratio,
                    &value.geometry, &output);
            if (!valid) abort();
            mver_probe_consume_geometry(&output);
        } else {
            if (cached) bongo_cat_mver_pointer_overlay_draw_before_keys(&value);
            else mver_reference_before(&value);
            if (scenario->midframe) anchor(&value, start + i + 503);
            if (cached) bongo_cat_mver_pointer_overlay_draw_after_keys(&value);
            else mver_reference_after(&value);
        }
    }
    wall = clock_ns(CLOCK_MONOTONIC) - wall;
    cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    return (Timing){wall, cpu};
}
int main(int argc, char **argv) {
    unsigned count = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2000;
    int trials = argc > 2 ? atoi(argv[2]) : 21;
    unsigned block = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 100;
    if (!count || !block || trials < 1) return 2;
    mver_trace_enabled = false;
    fprintf(stderr, "overlay_size_before=%zu overlay_size_after=%zu delta=%zu; "
        "no additional heap allocations; block=%u\n", sizeof(struct MverReferenceOverlay),
        sizeof(BongoCatMverPointerOverlay), sizeof(BongoCatMverPointerOverlay) -
        sizeof(struct MverReferenceOverlay), block);
    puts("scenario,trial,variant,iterations,wall_ns,cpu_ns");
    for (int trial = -3; trial < trials; trial++) {
        for (size_t i = 0; i < sizeof(scenarios) / sizeof(*scenarios); i++) {
            Timing total[2] = {{0, 0}, {0, 0}};
            unsigned sequence = 0;
            for (unsigned start = 0; start < count; start += block) {
                unsigned amount = count - start < block ? count - start : block;
                bool first_cached = ((unsigned)trial + sequence++) % 2 != 0;
                for (unsigned pair = 0; pair < 2; pair++) {
                    bool cached = pair ? !first_cached : first_cached;
                    Timing result = run(&scenarios[i], cached, start, amount);
                    total[cached].wall += result.wall;
                    total[cached].cpu += result.cpu;
                }
            }
            if (trial >= 0) for (int cached = 0; cached < 2; cached++) {
                printf("%s,%d,%s,%u,%llu,%llu\n", scenarios[i].name, trial,
                    cached ? "cached" : "upstream", count,
                    (unsigned long long)total[cached].wall,
                    (unsigned long long)total[cached].cpu);
            }
        }
    }
    fprintf(stderr, "payload_sink=%llu\n", (unsigned long long)mver_probe_sink);
    return 0;
}
