#include "windows_input_detection.h"

#ifdef _WIN32
#include <math.h>
#include <stdlib.h>

static bool interior(POINT point, const RECT *bounds) {
    const int margin = 4;
    return (long long)point.x > (long long)bounds->left + margin &&
        (long long)point.x < (long long)bounds->right - 1 - margin &&
        (long long)point.y > (long long)bounds->top + margin &&
        (long long)point.y < (long long)bounds->bottom - 1 - margin;
}

bool bongo_cat_windows_pointer_clip_locked(const RECT *clip) {
    if (!clip || clip->right < clip->left || clip->bottom < clip->top)
        return false;
    /* Ordered LONG endpoints have an exact DWORD distance, even across zero.
       Keep the subtraction unsigned instead of widening signed coordinates. */
    DWORD width = (DWORD)clip->right - (DWORD)clip->left;
    DWORD height = (DWORD)clip->bottom - (DWORD)clip->top;
    return width <= 2 && height <= 2;
}

static WindowsPointerReason lock_hint(const WindowsPointerObservation *sample) {
    if (sample->clip_known && bongo_cat_windows_pointer_clip_locked(&sample->clip))
        return WINDOWS_POINTER_CLIPPED;
    if (sample->cursor_known && sample->cursor_flags == 0)
        return WINDOWS_POINTER_HIDDEN;
    return WINDOWS_POINTER_DESKTOP;
}

static void begin_window(WindowsPointerDetection *state, POINT point,
    ULONGLONG now_ms) {
    state->anchor = point;
    state->window_ms = now_ms;
    state->raw_x = state->raw_y = 0;
    state->absolute_x = state->absolute_y = 0;
    state->moving_samples = state->returns = 0;
    state->away = false;
    state->edge_seen = false;
    state->excursion = 0;
}

bool bongo_cat_windows_pointer_detect(WindowsPointerDetection *state,
    const WindowsPointerObservation *sample, ULONGLONG now_ms) {
    if (!state || !sample) return false;
    if (!sample->foreign || !sample->foreground || !sample->position_known) {
        *state = (WindowsPointerDetection){0};
        return false;
    }
    /* Never reuse movement from another foreground window or a suspended loop. */
    if (!state->initialized || state->foreground != sample->foreground ||
        state->pid != sample->pid || now_ms < state->last_ms ||
        now_ms - state->last_ms > 500) {
        *state = (WindowsPointerDetection){.initialized = true,
            .foreground = sample->foreground, .pid = sample->pid,
            .last_ms = now_ms};
        begin_window(state, sample->position, now_ms);
        return false;
    }
    state->last_ms = now_ms;
    state->raw_x += sample->raw_x;
    state->raw_y += sample->raw_y;
    state->absolute_x += sample->absolute_x;
    state->absolute_y += sample->absolute_y;
    if (sample->motion_packets) state->moving_samples++;
    long long dx = llabs((long long)sample->position.x - state->anchor.x);
    long long dy = llabs((long long)sample->position.y - state->anchor.y);
    long long distance = dx > dy ? dx : dy;
    if (distance > state->excursion) state->excursion = distance;
    if (distance > 2) state->away = true;
    else if (state->away) { state->returns++; state->away = false; }
    bool inside = sample->monitor_known &&
        interior(sample->position, &sample->monitor) &&
        interior(state->anchor, &sample->monitor) &&
        (!sample->clip_known || (interior(sample->position, &sample->clip) &&
            interior(state->anchor, &sample->clip)));
    if (!inside) state->edge_seen = true;

    WindowsPointerReason hint = lock_hint(sample);
    if (hint == WINDOWS_POINTER_CLIPPED) {
        if (!state->hint_pending) {
            state->hint_pending = true;
            state->hint_ms = now_ms;
        }
        /* Only a tiny clip is direct evidence. Hidden cursors may still travel
           normally, so they use the same motion checks as visible cursors. */
        if (state->relative || (now_ms - state->hint_ms >= 32 &&
            state->moving_samples >= 2)) {
            state->relative = true;
            state->reason = hint;
            state->evidence_ms = now_ms;
        }
    } else state->hint_pending = false;

    ULONGLONG window_age = now_ms - state->window_ms;
    if (window_age >= 128 && (state->moving_samples >= 4 || window_age >= 256)) {
        /* Net device counts reject ordinary back-and-forth movement. The
           monitor and clipping edges reject a cursor pushed against a border. */
        /* Device counts and absolute-device pixels remain separate units. */
        bool raw_travel = fabs(state->raw_x) >= 32 || fabs(state->raw_y) >= 32 ||
            fabs(state->absolute_x) >= 32 || fabs(state->absolute_y) >= 32;
        bool evidence = !state->edge_seen && raw_travel && state->moving_samples >= 4;
        bool stalled = evidence && distance <= 1 && state->excursion <= 1;
        bool recentered = evidence && state->returns >= 2 && state->excursion <= 48;
        if (hint != WINDOWS_POINTER_CLIPPED && (stalled || recentered)) {
            state->relative = true;
            state->reason = hint == WINDOWS_POINTER_HIDDEN ? WINDOWS_POINTER_HIDDEN :
                stalled ? WINDOWS_POINTER_STALLED : WINDOWS_POINTER_RECENTERED;
            state->evidence_ms = now_ms;
        } else if (hint != WINDOWS_POINTER_CLIPPED && state->relative &&
            now_ms - state->evidence_ms >= 192 &&
            (distance > 2 || state->reason == WINDOWS_POINTER_CLIPPED ||
                (hint == WINDOWS_POINTER_DESKTOP &&
                    state->reason == WINDOWS_POINTER_HIDDEN))) {
            state->relative = false;
            state->reason = WINDOWS_POINTER_DESKTOP;
        }
        /* A stationary inferred lock stays active until actual cursor travel
           disproves it; stopping the mouse must not reset the virtual position. */
        begin_window(state, sample->position, now_ms);
    }
    return state->relative;
}
#endif
