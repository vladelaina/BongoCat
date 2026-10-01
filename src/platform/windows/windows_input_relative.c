#include "windows_input_internal.h"

#ifdef _WIN32
#include <string.h>

void bongo_cat_windows_input_motion(WindowsInputState *state,
    WindowsRawDevice *device, const RAWMOUSE *mouse, const RECT *bounds) {
    AcquireSRWLockExclusive(&state->relative_lock);
    if (!(mouse->usFlags & MOUSE_MOVE_ABSOLUTE)) {
        device->absolute_known = false;
        if (mouse->lLastX || mouse->lLastY) {
            state->observed_x += mouse->lLastX;
            state->observed_y += mouse->lLastY;
            state->observed_motion++;
            if (state->relative_active) {
                state->relative_x += mouse->lLastX;
                state->relative_y += mouse->lLastY;
                state->relative_samples++;
            }
        }
    } else {
        if (!bounds || bounds->right <= bounds->left ||
            bounds->bottom <= bounds->top || mouse->lLastX < 0 ||
            mouse->lLastX > 65535 || mouse->lLastY < 0 || mouse->lLastY > 65535) {
            device->absolute_known = false;
            ReleaseSRWLockExclusive(&state->relative_lock);
            return;
        }
        double x = bounds->left + (double)mouse->lLastX *
            ((double)bounds->right - bounds->left - 1.0) / 65535.0;
        double y = bounds->top + (double)mouse->lLastY *
            ((double)bounds->bottom - bounds->top - 1.0) / 65535.0;
        bool baseline = device->absolute_known &&
            !(mouse->usFlags & MOUSE_ATTRIBUTES_CHANGED) &&
            device->generation == state->generation &&
            memcmp(bounds, &device->absolute_bounds, sizeof(*bounds)) == 0;
        if (baseline && (x != device->absolute_x || y != device->absolute_y)) {
            state->observed_absolute_x += x - device->absolute_x;
            state->observed_absolute_y += y - device->absolute_y;
            state->observed_motion++;
            if (state->relative_active) {
                state->absolute_x += x - device->absolute_x;
                state->absolute_y += y - device->absolute_y;
                state->absolute_samples++;
            }
        }
        device->absolute_x = x;
        device->absolute_y = y;
        device->absolute_bounds = *bounds;
        device->absolute_known = true;
        device->generation = state->generation;
    }
    ReleaseSRWLockExclusive(&state->relative_lock);
}

static void clear_motion(WindowsInputState *state) {
    state->relative_x = state->relative_y = 0;
    state->absolute_x = state->absolute_y = 0.0;
    state->relative_samples = state->absolute_samples = 0;
    state->generation++;
}

void bongo_cat_windows_input_clear_motion(WindowsInputState *state) {
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    state->observed_x = state->observed_y = 0;
    state->observed_absolute_x = state->observed_absolute_y = 0;
    state->observed_motion = 0;
    state->observed_generation++;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

unsigned long long bongo_cat_windows_input_take_observation(
    WindowsInputState *state, WindowsPointerObservation *sample) {
    if (!state || !sample) return 0;
    AcquireSRWLockExclusive(&state->relative_lock);
    sample->raw_x = (double)state->observed_x;
    sample->raw_y = (double)state->observed_y;
    sample->absolute_x = state->observed_absolute_x;
    sample->absolute_y = state->observed_absolute_y;
    sample->motion_packets = state->observed_motion;
    unsigned long long generation = state->observed_generation;
    state->observed_x = state->observed_y = 0;
    state->observed_absolute_x = state->observed_absolute_y = 0;
    state->observed_motion = 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
    return generation;
}

bool bongo_cat_windows_input_take_relative(BongoCatPlatform *platform,
    double *x, double *y, unsigned long long *sample_count) {
    if (!x || !y) return false;
    *x = *y = 0.0;
    if (sample_count) *sample_count = 0;
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return false;
    AcquireSRWLockExclusive(&state->relative_lock);
    /* Delivered packets remain valid even if the registration probe disagrees. */
    bool delivered = state->relative_samples || state->absolute_samples;
    bool available = state->relative_active && (state->receiving || delivered);
    if (available) {
        /* Device counts and absolute-device pixels must never be added together. */
        bool relative = state->relative_samples != 0;
        *x = relative ? (double)state->relative_x : state->absolute_x;
        *y = relative ? (double)state->relative_y : state->absolute_y;
        if (sample_count) *sample_count = relative ? state->relative_samples :
            state->absolute_samples;
    }
    state->relative_x = state->relative_y = 0;
    state->absolute_x = state->absolute_y = 0.0;
    state->relative_samples = state->absolute_samples = 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
    return available;
}

void bongo_cat_windows_input_reset_relative(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    state->relative_active = true;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

void bongo_cat_windows_input_release_relative(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    state->relative_active = false;
    ReleaseSRWLockExclusive(&state->relative_lock);
}
#endif
