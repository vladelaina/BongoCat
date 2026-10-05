#include "config_store_test.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define SAMPLE_COUNT 31
static uint64_t target_ns = 30000000ull;

int bongo_cat_test_failures;
static volatile uint64_t sink;
typedef void (*Update)(BongoCatApp *, uint64_t);

typedef struct Measurement {
    Update update;
    BongoCatApp *app;
    size_t iterations;
    double wall[SAMPLE_COUNT], cpu[SAMPLE_COUNT];
} Measurement;

#ifdef _WIN32
static double process_cpu_ns(void) {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        abort();
    uint64_t ticks = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    ticks += ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (double)ticks * 100.0;
}
#endif

static uint64_t run(Measurement *measurement, size_t iterations, double *cpu) {
#ifdef _WIN32
    double cpu_start = process_cpu_ns();
#else
    clock_t cpu_start = clock();
#endif
    uint64_t start = SDL_GetTicksNS();
    for (size_t i = 0; i < iterations; ++i)
        measurement->update(measurement->app, 0);
    uint64_t elapsed = SDL_GetTicksNS() - start;
#ifdef _WIN32
    double cpu_end = process_cpu_ns();
    if (cpu) *cpu = cpu_end - cpu_start;
#else
    clock_t cpu_end = clock();
    if (cpu_start == (clock_t)-1 || cpu_end == (clock_t)-1) abort();
    if (cpu) *cpu = (double)(cpu_end - cpu_start) * 1e9 / CLOCKS_PER_SEC;
#endif
    sink ^= measurement->app->settings_observed_hash;
    sink ^= measurement->app->session_observed_hash;
    return elapsed;
}

static void warmup(Measurement *measurement) {
    size_t iterations = 1;
    while (run(measurement, iterations, NULL) < target_ns && iterations < (1u << 24))
        iterations *= 2;
    measurement->iterations = iterations;
    (void)run(measurement, iterations, NULL);
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

static void sample(Measurement *measurement, size_t index, FILE *raw,
    const char *fixture, const char *mode, const char *variant) {
    double cpu_ns;
    uint64_t elapsed = run(measurement, measurement->iterations, &cpu_ns);
    double ns = (double)elapsed / (double)measurement->iterations;
    cpu_ns /= (double)measurement->iterations;
    measurement->wall[index] = ns;
    measurement->cpu[index] = cpu_ns;
    if (raw) fprintf(raw, "%s,%s,%s,%zu,%zu,%.6f,%.6f\n", fixture, mode,
        variant, index, measurement->iterations, ns, cpu_ns);
}

static void benchmark(bool saturated, unsigned mode, FILE *raw) {
    const char *names[] = {"primary", "secondary", "settings-blocked",
        "session-blocked", "both-blocked"};
    const char *fixture = saturated ? "saturated" : "defaults";
    BongoCatApp *app = config_test_app(saturated, true);
    CHECK(app->settings.behavior_shortcut_count ==
        (saturated ? BONGO_CAT_BEHAVIOR_BINDING_CAP : 0));
    CHECK(app->settings.model_label_count == (saturated ? BONGO_CAT_MODEL_CAP : 0));
    CHECK(app->settings.removed_model_count == (saturated ? BONGO_CAT_MODEL_CAP : 0));
    CHECK(app->session.additional_model_count ==
        (saturated ? BONGO_CAT_ADDITIONAL_MODEL_CAP : 0));
    CHECK(app->session.active_behavior_count ==
        (saturated ? BONGO_CAT_BEHAVIOR_BINDING_CAP : 0));
    if (bongo_cat_test_failures) abort();
    app->secondary_pet = mode == 1;
    app->settings_store_blocked = mode == 2 || mode == 4;
    app->session_store_blocked = mode == 3 || mode == 4;
    BongoCatApp *reference = malloc(sizeof(*reference));
    if (!reference) abort();
    memcpy(reference, app, sizeof(*reference));
    Measurement baseline = {.update = config_reference_update, .app = reference};
    Measurement current = {.update = bongo_cat_config_store_update, .app = app};
    /* Independent calibration keeps even the guard-only case measurable. */
    warmup(&baseline);
    warmup(&current);
    for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
        if (i % 2) {
            sample(&current, i, raw, fixture, names[mode], "optimized");
            sample(&baseline, i, raw, fixture, names[mode], "baseline");
        } else {
            sample(&baseline, i, raw, fixture, names[mode], "baseline");
            sample(&current, i, raw, fixture, names[mode], "optimized");
        }
    }
    config_test_compare(app, reference);
    qsort(baseline.wall, SAMPLE_COUNT, sizeof(double), compare_double);
    qsort(current.wall, SAMPLE_COUNT, sizeof(double), compare_double);
    qsort(baseline.cpu, SAMPLE_COUNT, sizeof(double), compare_double);
    qsort(current.cpu, SAMPLE_COUNT, sizeof(double), compare_double);
    /* Nearest-rank p95 for 31 independent batch averages is element 30. */
    printf("%s,%s,%zu,%zu,%.3f,%.3f,%.3f,%.3f,%.2f,"
        "%.3f,%.3f,%.3f,%.3f,%.2f\n", fixture, names[mode],
        baseline.iterations, current.iterations,
        baseline.wall[15], baseline.wall[29], current.wall[15], current.wall[29],
        100.0 * (baseline.wall[15] - current.wall[15]) / baseline.wall[15],
        baseline.cpu[15], baseline.cpu[29], current.cpu[15], current.cpu[29],
        100.0 * (baseline.cpu[15] - current.cpu[15]) / baseline.cpu[15]);
    free(reference);
    free(app);
}

int main(int argc, char **argv) {
    if (argc > 2) {
        char *end;
        unsigned long milliseconds = strtoul(argv[2], &end, 10);
        if (*end || milliseconds < 1 || milliseconds > 1000) return EXIT_FAILURE;
        target_ns = milliseconds * 1000000ull;
    }
    FILE *raw = argc > 1 ? fopen(argv[1], "w") : NULL;
    if (argc > 1 && !raw) return EXIT_FAILURE;
    if (raw) fprintf(raw, "fixture,mode,variant,sample,iterations,wall_ns_per_call,cpu_ns_per_call\n");
    printf("fixture,mode,baseline_iterations,optimized_iterations,"
        "baseline_wall_median_ns,baseline_wall_p95_ns,optimized_wall_median_ns,"
        "optimized_wall_p95_ns,wall_reduction_pct,"
        "baseline_cpu_median_ns,baseline_cpu_p95_ns,optimized_cpu_median_ns,"
        "optimized_cpu_p95_ns,cpu_reduction_pct\n");
    /* Any unexpected save aborts: only steady-state update CPU cost is timed. */
    config_test_trace(NULL);
    for (unsigned saturated = 0; saturated < 2; ++saturated)
        for (unsigned mode = 0; mode < 5; ++mode)
            benchmark(saturated != 0, mode, raw);
    if (raw && fclose(raw)) return EXIT_FAILURE;
    return bongo_cat_test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
