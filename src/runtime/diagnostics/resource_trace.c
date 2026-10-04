#include "bongo_cat/resource_trace.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/log.h"

#include <SDL3/SDL_timer.h>
#include <stdarg.h>
#include <stdio.h>

/* Peak samples are approximate, not allocation instrumentation. All scopes
   share one 250 ms sample and one output budget, including modal menus. */
#define SAMPLE_MS 250
#define LOG_WINDOW_MS 60000
#define LOG_LIMIT 60
#define NOTE_LIMIT 6

typedef struct ResourceTrace {
    bool active, watching, baseline_valid, long_reported;
    unsigned id, notes, overlap, observations;
    uint64_t started_ms, ended_ms, peak_ws, peak_private;
    BongoCatProcessResources baseline;
    char label[256];
} ResourceTrace;

static ResourceTrace traces[BONGO_CAT_RESOURCE_SCOPE_COUNT];
static const char *const names[] = {"model", "resize", "settings", "menu", "snapshot"};
static unsigned next_id, lines, suppressed;
static uint64_t log_window_ms, sampled_ms;
static bool sampled;
static bool shutdown_logged;
static BongoCatProcessResources current;
static bool settings_retained, settings_visible;
static double atlas_mib;
static double last_mask_mib = -1.0;
static unsigned last_pool_targets;
static int last_render_width, last_render_height;
static struct {
    bool known, gesture, pending, snapshot;
    int width, height, from_w, from_h;
    unsigned changes;
    uint64_t changed_ms;
} resize_state;

static unsigned active_mask(void) {
    unsigned mask = 0;
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i)
        if (traces[i].active) mask |= 1u << i;
    return mask;
}

static unsigned watching_mask(void) {
    unsigned mask = 0;
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i)
        if (traces[i].watching) mask |= 1u << i;
    return mask;
}

static void sample(bool force) {
    uint64_t now = SDL_GetTicks();
    if (!force && sampled && now - sampled_ms < SAMPLE_MS) return;
    sampled_ms = now;
    sampled = true;
    bongo_cat_platform_process_resources(&current);
    if (!current.memory_available) return;
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i) {
        ResourceTrace *t = &traces[i];
        if (!t->active && !t->watching) continue;
        t->peak_ws = SDL_max(t->peak_ws, current.memory.working_set_bytes);
        t->peak_private = SDL_max(t->peak_private, current.memory.private_bytes);
    }
}

static bool allow_log(void) {
    uint64_t now = SDL_GetTicks();
    if (now - log_window_ms >= LOG_WINDOW_MS) {
        log_window_ms = now;
        lines = 0;
    }
    if (lines >= LOG_LIMIT - 1) {
        ++suppressed;
        if (lines < LOG_LIMIT) {
            ++lines;
            SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
                "[resources] scope=all stage=rate-limited window_ms=%u limit=%u "
                "suppressed=%u", (unsigned)LOG_WINDOW_MS, (unsigned)LOG_LIMIT, suppressed);
        }
        return false;
    }
    ++lines;
    return true;
}

static void emit(BongoCatResourceScope scope, const char *stage, const char *details) {
    if (!allow_log()) return;
    ResourceTrace *t = &traces[scope];
    const double mib = 1024.0 * 1024.0;
    bool delta = t->baseline_valid && current.memory_available;
    double cpu_ms = t->baseline.cpu_available && current.cpu_available ?
        (double)(current.cpu_time_ns - t->baseline.cpu_time_ns) / 1000000.0 : -1.0;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[resources] op=%u scope=%s stage=%s model_op=%u elapsed_ms=%llu since_end_ms=%llu "
        "memory_available=%d ws_mib=%.1f private_mib=%.1f delta_valid=%d "
        "delta_ws_mib=%.1f delta_private_mib=%.1f sampled_peak_ws_mib=%.1f "
        "sampled_peak_private_mib=%.1f process_cpu_ms=%.1f "
        "handles=%lld gdi=%lld user=%lld delta_handles=%lld "
        "active_mask=0x%x overlap_mask=0x%x ui_retained=%d ui_visible=%d "
        "live_atlas_est_mib=%.1f last_mask_est_mib=%.1f last_pool_targets=%u "
        "last_render_physical=%dx%d window_logical=%dx%d texture_pending=%d "
        "gesture=%d snapshot=%d suppressed=%u label=\"%s\" %s",
        t->id, names[scope], stage, traces[BONGO_CAT_RESOURCE_MODEL].id,
        (unsigned long long)(SDL_GetTicks() - t->started_ms),
        (unsigned long long)(t->watching ? SDL_GetTicks() - t->ended_ms : 0),
        current.memory_available,
        current.memory_available ? current.memory.working_set_bytes / mib : -1.0,
        current.memory_available ? current.memory.private_bytes / mib : -1.0,
        delta,
        delta ? ((double)current.memory.working_set_bytes - t->baseline.memory.working_set_bytes) / mib : 0.0,
        delta ? ((double)current.memory.private_bytes - t->baseline.memory.private_bytes) / mib : 0.0,
        t->baseline_valid ? t->peak_ws / mib : -1.0,
        t->baseline_valid ? t->peak_private / mib : -1.0, cpu_ms,
        current.handles_available ? (long long)current.handles : -1ll,
        current.gui_available ? (long long)current.gdi_objects : -1ll,
        current.gui_available ? (long long)current.user_objects : -1ll,
        current.handles_available && t->baseline.handles_available ?
            (long long)current.handles - t->baseline.handles : -1ll,
        active_mask(), t->overlap, settings_retained, settings_visible,
        atlas_mib, last_mask_mib, last_pool_targets, last_render_width, last_render_height,
        resize_state.width, resize_state.height, resize_state.pending,
        resize_state.gesture, resize_state.snapshot, suppressed, t->label, details);
    suppressed = 0;
}

static void overlap(BongoCatResourceScope scope) {
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i) {
        if (i == (unsigned)scope || (!traces[i].active && !traces[i].watching)) continue;
        traces[i].overlap |= 1u << scope;
        traces[scope].overlap |= 1u << i;
    }
}

void bongo_cat_resource_trace_begin(BongoCatResourceScope scope, const char *label) {
    if ((unsigned)scope >= BONGO_CAT_RESOURCE_SCOPE_COUNT) return;
    if (scope == BONGO_CAT_RESOURCE_MODEL) {
        bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_RESIZE, "interrupted", "reason=model-switch");
        resize_state.known = false;
    }
    ResourceTrace *t = &traces[scope];
    if (t->active) return;
    sample(true);
    if (t->watching) emit(scope, "superseded", "next_operation=1");
    *t = (ResourceTrace){0};
    t->id = ++next_id;
    t->active = true;
    t->started_ms = SDL_GetTicks();
    t->baseline = current;
    t->baseline_valid = current.memory_available;
    t->peak_ws = current.memory.working_set_bytes;
    t->peak_private = current.memory.private_bytes;
    SDL_strlcpy(t->label, label ? label : "", sizeof(t->label));
    for (char *p = t->label; *p; ++p)
        if (*p == '\n' || *p == '\r' || *p == '"') *p = ' ';
    overlap(scope);
    emit(scope, "begin", "version=1");
}

static void event(BongoCatResourceScope scope, bool end, const char *stage,
    const char *format, va_list args) {
    if ((unsigned)scope >= BONGO_CAT_RESOURCE_SCOPE_COUNT || !traces[scope].active) return;
    ResourceTrace *t = &traces[scope];
    if (!end && SDL_strcmp(stage, "before-release") && t->notes++ >= NOTE_LIMIT) {
        ++suppressed;
        return;
    }
    char details[512] = {0};
    if (format) vsnprintf(details, sizeof(details), format, args);
    for (char *p = details; *p; ++p) if (*p == '\n' || *p == '\r') *p = ' ';
    overlap(scope);
    sample(true);
    if (end) {
        t->active = false;
        t->watching = true;
        t->ended_ms = SDL_GetTicks();
        if (scope == BONGO_CAT_RESOURCE_MODEL) resize_state.known = false;
    }
    emit(scope, stage, details);
}

void bongo_cat_resource_trace_note(BongoCatResourceScope scope,
    const char *stage, const char *format, ...) {
    va_list args;
    va_start(args, format); event(scope, false, stage, format, args); va_end(args);
}

void bongo_cat_resource_trace_end(BongoCatResourceScope scope,
    const char *stage, const char *format, ...) {
    va_list args;
    va_start(args, format); event(scope, true, stage, format, args); va_end(args);
}

void bongo_cat_resource_trace_poll(void) {
    uint64_t now = SDL_GetTicks();
    bool needed = false;
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i)
        needed |= traces[i].active || traces[i].watching;
    if (!needed) return;
    sample(false);
    for (unsigned i = 0; i < BONGO_CAT_RESOURCE_SCOPE_COUNT; ++i) {
        ResourceTrace *t = &traces[i];
        if (t->active && !t->long_reported && now - t->started_ms >= 10000 &&
            (i == BONGO_CAT_RESOURCE_MODEL || i == BONGO_CAT_RESOURCE_RESIZE)) {
            t->long_reported = true;
            emit((BongoCatResourceScope)i, "pending-10s", "");
        }
        if (!t->watching) continue;
        if (now - t->ended_ms >= (t->observations ? 10000u : 3000u)) {
            emit((BongoCatResourceScope)i, t->observations ? "settled-10s" : "settled-3s", "");
            if (++t->observations == 2) t->watching = false;
        }
    }
}

void bongo_cat_resource_trace_shutdown(void) {
    /* Shutdown is a useful observation boundary even when the process exits
       before the normal three/ten-second settled samples. */
    if (shutdown_logged) return;
    shutdown_logged = true;
    sample(true);
    const double mib = 1024.0 * 1024.0;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[resources] scope=all stage=shutdown-summary memory_available=%d "
        "ws_mib=%.1f private_mib=%.1f handles=%lld gdi=%lld user=%lld "
        "active_mask=0x%x watching_mask=0x%x ui_retained=%d ui_visible=%d "
        "live_atlas_est_mib=%.1f last_mask_est_mib=%.1f last_pool_targets=%u "
        "last_render_physical=%dx%d window_logical=%dx%d texture_pending=%d "
        "gesture=%d snapshot=%d suppressed=%u",
        current.memory_available,
        current.memory_available ? current.memory.working_set_bytes / mib : -1.0,
        current.memory_available ? current.memory.private_bytes / mib : -1.0,
        current.handles_available ? (long long)current.handles : -1ll,
        current.gui_available ? (long long)current.gdi_objects : -1ll,
        current.gui_available ? (long long)current.user_objects : -1ll,
        active_mask(), watching_mask(), settings_retained, settings_visible,
        atlas_mib, last_mask_mib, last_pool_targets, last_render_width,
        last_render_height, resize_state.width, resize_state.height,
        resize_state.pending, resize_state.gesture, resize_state.snapshot,
        suppressed);
    suppressed = 0;
}

void bongo_cat_resource_trace_resize_request(int from_w, int from_h, int to_w, int to_h) {
    if (traces[BONGO_CAT_RESOURCE_MODEL].active || (from_w == to_w && from_h == to_h)) return;
    if (!traces[BONGO_CAT_RESOURCE_RESIZE].active) {
        resize_state.from_w = from_w; resize_state.from_h = from_h;
        resize_state.changes = 0;
        bongo_cat_resource_trace_begin(BONGO_CAT_RESOURCE_RESIZE, "window-size");
    }
    ++resize_state.changes;
    resize_state.changed_ms = SDL_GetTicks();
}

void bongo_cat_resource_trace_resize_observe(int width, int height,
    bool gesture, bool pending, bool snapshot) {
    if (resize_state.known && (width != resize_state.width || height != resize_state.height))
        bongo_cat_resource_trace_resize_request(resize_state.width, resize_state.height, width, height);
    resize_state.width = width; resize_state.height = height;
    resize_state.known = true;
    resize_state.gesture = gesture; resize_state.pending = pending; resize_state.snapshot = snapshot;
    if (traces[BONGO_CAT_RESOURCE_RESIZE].active && !gesture && !pending && !snapshot &&
        SDL_GetTicks() - resize_state.changed_ms >= 600)
        bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_RESIZE, "complete",
            "from_logical=%dx%d to_logical=%dx%d geometry_events=%u",
            resize_state.from_w, resize_state.from_h, width, height, resize_state.changes);
}

void bongo_cat_resource_trace_atlas(double mib) { atlas_mib = mib; }
void bongo_cat_resource_trace_render(double mask_mib, unsigned pool_targets,
    int width, int height) {
    last_mask_mib = mask_mib; last_pool_targets = pool_targets;
    last_render_width = width; last_render_height = height;
}
void bongo_cat_resource_trace_settings(bool retained, bool visible) {
    settings_retained = retained; settings_visible = visible;
}
