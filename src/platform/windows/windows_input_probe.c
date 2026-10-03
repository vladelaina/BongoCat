#include "windows_input_internal.h"

#ifdef _WIN32
static void observe_cursor(WindowsPointerObservation *sample) {
    CURSORINFO cursor = {.cbSize = sizeof(cursor)};
    sample->cursor_known = GetCursorInfo(&cursor) != FALSE;
    sample->cursor_flags = cursor.flags;
    if (sample->cursor_known) {
        sample->position = cursor.ptScreenPos;
        sample->position_known = true;
    } else sample->position_known = GetCursorPos(&sample->position) != FALSE;
    sample->clip_known = GetClipCursor(&sample->clip) != FALSE;
    if (sample->position_known) {
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        sample->monitor_known = GetMonitorInfoW(MonitorFromPoint(sample->position,
            MONITOR_DEFAULTTONULL), &monitor) != FALSE;
        sample->monitor = monitor.rcMonitor;
    }
}

bool bongo_cat_windows_input_pointer_locked(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return false;
    ULONGLONG now_ms = GetTickCount64();
    if (state->pointer_probe_ready && now_ms - state->pointer_probe_ms < 16)
        return state->pointer_detection.relative;
    state->pointer_probe_ready = true;
    state->pointer_probe_ms = now_ms;
    WindowsPointerObservation sample = {0};
    sample.foreground = GetForegroundWindow();
    if (sample.foreground) GetWindowThreadProcessId(sample.foreground, &sample.pid);
    sample.foreign = sample.pid && sample.pid != GetCurrentProcessId();
    /* Our own windows cannot trigger game mode. Still consume observations so
       old desktop movement cannot leak into the next foreground application. */
    if (sample.foreign) observe_cursor(&sample);
    unsigned long long generation =
        bongo_cat_windows_input_take_observation(state, &sample);
    if (state->pointer_generation != generation) {
        state->pointer_detection = (WindowsPointerDetection){0};
        state->pointer_generation = generation;
    }
    return bongo_cat_windows_pointer_detect(&state->pointer_detection,
        &sample, now_ms);
}
#endif
