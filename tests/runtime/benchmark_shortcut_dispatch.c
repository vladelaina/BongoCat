#include "shortcut_test_support.h"
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define SAMPLES 31
typedef void (*Dispatch)(BongoCatApp *, const BongoCatInputEvent *);
typedef struct Timing { double wall, cpu; } Timing;
typedef struct Scenario {
    const char *name;
    size_t behaviors, bindings;
    unsigned mode;
} Scenario;

static const Scenario scenarios[] = {
    {"empty-catalog", 0, 64, 0},
    {"no-bindings", 8, 0, 0},
    {"single-valid-miss", 1, 1, 0},
    {"single-stale-binding", 1, 1, 2},
    {"small-key-miss", 8, 4, 0},
    {"small-mixed-keys", 8, 4, 1},
    {"lower-boundary-key-miss", 31, 16, 0},
    {"medium-key-miss", 32, 16, 0},
    {"upper-boundary-key-miss", 33, 16, 0},
    {"stale-long-16", 16, 64, 2},
    {"stale-long-31", 31, 64, 2},
    {"stale-long-32", 32, 64, 2},
    {"stale-long-33", 33, 64, 2},
    {"stale-long-64", 64, 64, 2},
    {"large-key-miss", 128, 128, 0},
    {"large-mixed-keys", 128, 128, 1},
    {"large-few-bindings", 128, 8, 1},
    {"large-label-overrides", 128, 128, 6},
    {"capacity-stale-long", 128, 128, 2},
    {"small-stale-long", 1, 64, 2},
    {"gamepad-miss", 128, 128, 3},
    {"gamepad-mixed", 128, 128, 4},
    {"non-key-event", 128, 128, 5},
    {"alt-fallback-31", 31, 16, 7},
    {"alt-fallback-32", 32, 16, 7},
    {"alt-fallback-33", 33, 16, 7}
};

static BongoCatInputEvent events[8];
static ShortcutTrace trace;
static volatile unsigned checksum;

static double process_cpu_ns(void) {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        fputs("Cannot read process CPU time: GetProcessTimes failed\n", stderr);
        exit(2);
    }
    uint64_t ticks = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    ticks += ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (double)ticks * 100.0;
#else
    clock_t ticks = clock();
    if (ticks == (clock_t)-1) {
        fputs("Cannot read process CPU time: clock failed\n", stderr);
        exit(2);
    }
    return (double)ticks * 1e9 / (double)CLOCKS_PER_SEC;
#endif
}

static void prepare(BongoCatApp *app, const Scenario *scenario) {
    static const char *prefixes[] = {
        "Control+", "Control+Shift+", "Control+Alt+", "Control+Super+",
        "Shift+", "Alt+", "Super+", "Control+Shift+Alt+",
        "Control+Shift+Super+", "Control+Alt+Super+", "Shift+Alt+"
    };
    shortcut_fixture(app, scenario->behaviors, scenario->bindings);
    app->shortcut_state.control = scenario->mode == 7 ? 0 : 1;
    app->shortcut_state.alt = scenario->mode == 7 ? 1 : 0;
    for (size_t i = 0; i < scenario->bindings; ++i) {
        BongoCatBehaviorShortcut *binding = &app->config.behavior_shortcuts[i];
        /* Unique bindings, including different-model overrides at capacity. */
        snprintf(binding->shortcut, sizeof(binding->shortcut), "%sF%zu",
            prefixes[i / 24], i % 24 + 1);
        if (i >= scenario->behaviors)
            snprintf(binding->id, sizeof(binding->id), "other-model/behavior-%zu", i);
        if (scenario->mode == 2) {
            snprintf(binding->id, sizeof(binding->id), "other-model/stale-%zu", i);
            snprintf(binding->shortcut, sizeof(binding->shortcut),
                "Control+Shift+Alt+Super+F%zu", i + 1);
        } else if (scenario->mode == 4 && i == 0)
            strcpy(binding->shortcut, "Gamepad:South");
        else if (scenario->mode == 6 && i >= 8) {
            binding->shortcut[0] = '\0';
            snprintf(binding->label, sizeof(binding->label), "custom-label-%zu", i);
        }
    }
    for (size_t i = 0; i < 8; ++i) {
        const char *name = (scenario->mode == 1 || scenario->mode == 6) &&
            i % 4 < 2 ? "F1" : "KeyZ";
        if (scenario->mode == 7) name = i % 4 < 2 ? "Num1" : "Num0";
        events[i] = shortcut_event(i % 2 ? BONGO_CAT_INPUT_KEY_UP :
            BONGO_CAT_INPUT_KEY_DOWN, name, i % 2 ? 0 : 1);
        if (scenario->mode == 3 || scenario->mode == 4)
            events[i] = shortcut_event(BONGO_CAT_INPUT_GAMEPAD_BUTTON,
                "South", i % 2 ? 0 : 1);
        if (scenario->mode == 5)
            events[i] = shortcut_event(BONGO_CAT_INPUT_MOUSE_MOVE, "", 0);
    }
    memset(&trace, 0, sizeof(trace));
    trace.expression = -1;
    shortcut_trace = &trace;
}

static bool benchmark_outputs_match(BongoCatApp *before, BongoCatApp *after,
    const Scenario *scenario) {
    static ShortcutTrace baseline_trace, candidate_trace;
    prepare(before, scenario);
    prepare(after, scenario);
    memset(&baseline_trace, 0, sizeof(baseline_trace));
    baseline_trace.expression = -1;
    candidate_trace = baseline_trace;
    shortcut_record_actions = true;
    bool equal = true;
    /* Two complete cycles verify stateful dispatch before any timed batch. */
    for (size_t i = 0; equal && i < 16; ++i) {
        baseline_trace.count = candidate_trace.count = 0;
        memset(baseline_trace.actions, 0, sizeof(baseline_trace.actions));
        memset(candidate_trace.actions, 0, sizeof(candidate_trace.actions));
        shortcut_trace = &baseline_trace;
        reference_app_shortcuts(before, &events[i % 8]);
        shortcut_trace = &candidate_trace;
        bongo_cat_app_shortcuts(after, &events[i % 8]);
        equal = memcmp(before, after, sizeof(*before)) == 0 &&
            memcmp(&baseline_trace, &candidate_trace, sizeof(baseline_trace)) == 0;
        if (!equal) fprintf(stderr, "Benchmark parity mismatch: %s event %zu\n",
            scenario->name, i);
    }
    shortcut_record_actions = false;
    prepare(before, scenario);
    return equal;
}

static Timing batch(Dispatch dispatch, BongoCatApp *app, size_t count) {
    double cpu_start = process_cpu_ns();
    Uint64 start = SDL_GetPerformanceCounter();
    for (size_t i = 0; i < count; ++i) dispatch(app, &events[i % 8]);
    Uint64 elapsed = SDL_GetPerformanceCounter() - start;
    double cpu_elapsed = process_cpu_ns() - cpu_start;
    checksum += (unsigned)app->dirty + (unsigned char)app->shortcut_state.pressed[0] +
        (unsigned)(trace.expression + 1);
    return (Timing){
        (double)elapsed * 1e9 / (double)SDL_GetPerformanceFrequency() / (double)count,
        cpu_elapsed / (double)count};
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    BongoCatApp *before = malloc(sizeof(*before)), *after = malloc(sizeof(*after));
    BongoCatConfig *validated = malloc(sizeof(*validated));
    if (!before || !after || !validated) return 2;
    FILE *raw = argc > 1 ? fopen(argv[1], "w") : NULL;
    if (argc > 1 && !raw) return 2;
    if (raw) fputs("scenario,sample,baseline_first,events,baseline_wall_ns,candidate_wall_ns,"
        "baseline_cpu_ns,candidate_cpu_ns\n", raw);
    shortcut_record_actions = false;
    puts("# Dispatch microbenchmark only; audio/render/window are stubs.");
    puts("# 3 warmup pairs; 31 measured pairs; alternating order; ~8ms baseline batches.");
    puts("scenario,behaviors,bindings,events_per_batch,baseline_wall_median_ns,candidate_wall_median_ns,"
        "baseline_wall_p95_ns,candidate_wall_p95_ns,wall_median_speedup,"
        "baseline_cpu_median_ns,candidate_cpu_median_ns,baseline_cpu_p95_ns,candidate_cpu_p95_ns");
    for (size_t c = 0; c < sizeof(scenarios) / sizeof(scenarios[0]); ++c) {
        const Scenario *scenario = &scenarios[c];
        prepare(before, scenario);
        *validated = before->config;
        bongo_cat_config_validate(validated);
        bool valid = validated->behavior_shortcut_count == before->config.behavior_shortcut_count;
        for (size_t i = 0; valid && i < validated->behavior_shortcut_count; ++i) {
            const BongoCatBehaviorShortcut *a = &validated->behavior_shortcuts[i];
            const BongoCatBehaviorShortcut *b = &before->config.behavior_shortcuts[i];
            valid = strcmp(a->id, b->id) == 0 && strcmp(a->shortcut, b->shortcut) == 0 &&
                strcmp(a->label, b->label) == 0;
        }
        if (!valid) {
            fprintf(stderr, "Invalid benchmark bindings: %s\n", scenario->name);
            return 2;
        }
        if (!benchmark_outputs_match(before, after, scenario)) return 2;
        double estimate = batch(reference_app_shortcuts, before, 512).wall;
        size_t count = estimate > 0 ? (size_t)(8e6 / estimate) : 32768;
        if (count < 128) count = 128;
        if (count > 32768) count = 32768;
        count = (count + 7) / 8 * 8;
        double old_times[SAMPLES], new_times[SAMPLES];
        double old_cpu[SAMPLES], new_cpu[SAMPLES];
        for (int sample = -3; sample < SAMPLES; ++sample) {
            prepare(before, scenario);
            prepare(after, scenario);
            Timing old_time, new_time;
            if (sample % 2 == 0) {
                old_time = batch(reference_app_shortcuts, before, count);
                new_time = batch(bongo_cat_app_shortcuts, after, count);
            } else {
                new_time = batch(bongo_cat_app_shortcuts, after, count);
                old_time = batch(reference_app_shortcuts, before, count);
            }
            if (sample >= 0) {
                old_times[sample] = old_time.wall;
                new_times[sample] = new_time.wall;
                old_cpu[sample] = old_time.cpu;
                new_cpu[sample] = new_time.cpu;
                if (raw) fprintf(raw, "%s,%d,%d,%zu,%.2f,%.2f,%.2f,%.2f\n",
                    scenario->name, sample, sample % 2 == 0, count,
                    old_time.wall, new_time.wall, old_time.cpu, new_time.cpu);
            }
        }
        qsort(old_times, SAMPLES, sizeof(double), compare_double);
        qsort(new_times, SAMPLES, sizeof(double), compare_double);
        qsort(old_cpu, SAMPLES, sizeof(double), compare_double);
        qsort(new_cpu, SAMPLES, sizeof(double), compare_double);
        printf("%s,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%.3f,%.2f,%.2f,%.2f,%.2f\n", scenario->name,
            scenario->behaviors, scenario->bindings, count, old_times[15], new_times[15],
            old_times[29], new_times[29], old_times[15] / new_times[15],
            old_cpu[15], new_cpu[15], old_cpu[29], new_cpu[29]);
        fflush(stdout);
    }
    printf("# output parity: %zu fixtures, 16 events each\n",
        sizeof(scenarios) / sizeof(scenarios[0]));
    free(after);
    free(before);
    free(validated);
    printf("# observable checksum: %u\n", checksum);
    if (raw && fclose(raw) != 0) return 2;
    return 0;
}
