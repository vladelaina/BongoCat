#include "bongo_cat/model_memory.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/log.h"
#include "bongo_cat/resource_trace.h"

#include <SDL3/SDL_timer.h>
#include <stdarg.h>
#include <stdio.h>

enum { STAGE_LOG_LIMIT = 48 };
static unsigned next_serial;
static bool ui_retained, ui_visible;
static struct {
    bool active, loading, trimmed, available;
    unsigned serial, lines, suppressed;
    uint64_t started_ms, last_sample_ms, settled_ms;
    uint64_t peak_working_set, peak_private;
    const char *stage, *peak_working_set_stage, *peak_private_stage;
    BongoCatMemoryUsage current;
} trace;

static void sample_now(void) {
    trace.last_sample_ms = SDL_GetTicks();
    trace.available = bongo_cat_platform_memory_usage(&trace.current);
    if (!trace.available) return;
    if (trace.current.working_set_bytes > trace.peak_working_set) {
        trace.peak_working_set = trace.current.working_set_bytes;
        trace.peak_working_set_stage = trace.stage;
    }
    if (trace.current.private_bytes > trace.peak_private) {
        trace.peak_private = trace.current.private_bytes;
        trace.peak_private_stage = trace.stage;
    }
}

void bongo_cat_model_memory_sample(void) {
    bongo_cat_resource_trace_poll();
    if (trace.active && trace.loading &&
        SDL_GetTicks() - trace.last_sample_ms >= 100)
        sample_now();
}

static void log_stage(const char *stage, const char *details, bool force) {
    if (!trace.active) return;
    trace.stage = stage;
    sample_now();
    if (!force && trace.lines >= STAGE_LOG_LIMIT) {
        trace.suppressed++;
        return;
    }
    trace.lines++;
    double elapsed = (double)(trace.last_sample_ms - trace.started_ms);
    if (!trace.available) {
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[memory] load=%u stage=%s elapsed_ms=%.0f memory=unavailable %s",
            trace.serial, stage, elapsed, details);
        return;
    }
    const double mib = 1024.0 * 1024.0;
    char summary[192] = {0};
    if (force) snprintf(summary, sizeof(summary),
        "peak_ws_stage=%s peak_private_stage=%s suppressed=%u",
        trace.peak_working_set_stage, trace.peak_private_stage, trace.suppressed);
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[memory] load=%u stage=%s elapsed_ms=%.0f "
        "ws_mib=%.1f private_mib=%.1f sampled_peak_ws_mib=%.1f "
        "sampled_peak_private_mib=%.1f process_peak_ws_mib=%.1f "
        "ui_retained=%d ui_visible=%d %s %s",
        trace.serial, stage, elapsed,
        (double)trace.current.working_set_bytes / mib,
        (double)trace.current.private_bytes / mib,
        (double)trace.peak_working_set / mib, (double)trace.peak_private / mib,
        (double)trace.current.process_peak_working_set_bytes / mib,
        ui_retained, ui_visible, details, summary);
}

void bongo_cat_model_memory_log(const char *stage, const char *format, ...) {
    if (!trace.active) return;
    char details[1200] = {0};
    va_list args;
    va_start(args, format);
    if (format) vsnprintf(details, sizeof(details), format, args);
    va_end(args);
    log_stage(stage, details, false);
}

void bongo_cat_model_memory_begin(const char *previous, const char *next,
    bool dynamic, int width, int height) {
    bongo_cat_resource_trace_begin(BONGO_CAT_RESOURCE_MODEL, next);
    /* A new switch supersedes any pending settled sample of the old one. */
    SDL_zero(trace);
    trace.active = trace.loading = true;
    trace.serial = ++next_serial;
    trace.started_ms = SDL_GetTicks();
    trace.peak_working_set_stage = trace.peak_private_stage = "begin";
    bongo_cat_model_memory_log("begin",
        "version=2 dynamic=%d window=%dx%d previous=%s next=%s",
        dynamic, width, height, previous ? previous : "none",
        next ? next : "unknown");
}

void bongo_cat_model_memory_complete(bool success, int width, int height) {
    if (!trace.active) return;
    char details[64];
    snprintf(details, sizeof(details), "window=%dx%d", width, height);
    log_stage(success ? "complete" : "failed", details, true);
    trace.loading = false;
    trace.active = success;
    trace.settled_ms = SDL_GetTicks() + 3000;
    bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_MODEL,
        success ? "complete" : "failed", "window_physical=%dx%d", width, height);
}

void bongo_cat_model_memory_after_trim(void) {
    if (!trace.active || trace.loading || trace.trimmed) return;
    trace.trimmed = true;
    log_stage("after-trim", "", true);
}

void bongo_cat_model_memory_ui_state(bool retained, bool visible) {
    visible = retained && visible;
    if (ui_retained == retained && ui_visible == visible) return;
    ui_retained = retained;
    ui_visible = visible;
    bongo_cat_resource_trace_settings(retained, visible);
}

void bongo_cat_model_memory_poll(void) {
    uint64_t now = SDL_GetTicks();
    if (trace.active && trace.loading) return;
    if (trace.active && now >= trace.settled_ms) {
        log_stage("settled", "", true);
        trace.active = false;
    }
}

double bongo_cat_model_texture_mib(int width, int height, bool mipmaps) {
    if (width <= 0 || height <= 0) return 0.0;
    double bytes = 0.0;
    for (;;) {
        bytes += (double)width * height * 4.0;
        if (!mipmaps || (width == 1 && height == 1)) break;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return bytes / (1024.0 * 1024.0);
}
