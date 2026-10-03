#include "windows_input_internal.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <stdlib.h>

static bool registrations(WindowsInputState *state, RAWINPUTDEVICE **items,
    UINT *count) {
    *items = NULL;
    *count = 0;
    state->registration_error = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        UINT queried = GetRegisteredRawInputDevices(NULL, count, sizeof(**items));
        if (queried == (UINT)-1) {
            DWORD error = GetLastError();
            /* A size query may report the required count as insufficient buffer. */
            if (error != ERROR_INSUFFICIENT_BUFFER || !*count) {
                state->registration_error = error;
                return false;
            }
        }
        if (!*count) return true;
        if (*count > 1024) {
            state->registration_error = ERROR_INVALID_DATA;
            return false;
        }
        *items = calloc(*count, sizeof(**items));
        if (!*items) {
            state->registration_error = ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }
        UINT received = GetRegisteredRawInputDevices(*items, count, sizeof(**items));
        if (received != (UINT)-1) { *count = received; return true; }
        DWORD error = GetLastError();
        free(*items);
        *items = NULL;
        if (error != ERROR_INSUFFICIENT_BUFFER) {
            state->registration_error = error;
            return false;
        }
    }
    state->registration_error = ERROR_INSUFFICIENT_BUFFER;
    return false;
}

static unsigned device_class(const RAWINPUTDEVICE *device) {
    if (device->usUsagePage != 1) return 0;
    return device->usUsage == 2 ? 1u : device->usUsage == 6 ? 2u : 0u;
}

unsigned bongo_cat_windows_input_ownership(WindowsInputState *state) {
    RAWINPUTDEVICE *items;
    UINT count;
    if (!registrations(state, &items, &count)) return 0;
    unsigned owned = 0;
    state->mouse_registration_flags = state->keyboard_registration_flags = 0;
    for (UINT i = 0; i < count; ++i) {
        if (items[i].hwndTarget != state->window) continue;
        unsigned kind = device_class(&items[i]);
        if (kind == 1) state->mouse_registration_flags = items[i].dwFlags;
        if (kind == 2) state->keyboard_registration_flags = items[i].dwFlags;
        /* Windows may omit DEVNOTIFY when querying a successful registration. */
        if (items[i].dwFlags & RIDEV_INPUTSINK) owned |= kind;
    }
    free(items);
    return owned;
}

bool bongo_cat_windows_input_register(WindowsInputState *state) {
    if (SDL_getenv("BONGO_CAT_TEST_RAW_INPUT_FAILURE")) {
        state->startup_error = ERROR_ACCESS_DENIED;
        return false;
    }
    RAWINPUTDEVICE *items;
    UINT count;
    if (!registrations(state, &items, &count)) {
        state->startup_error = state->registration_error;
        return false;
    }
    bool occupied = false;
    for (UINT i = 0; i < count; ++i)
        if (device_class(&items[i])) occupied = true;
    free(items);
    if (occupied) { state->startup_error = ERROR_BUSY; return false; }
    const RAWINPUTDEVICE devices[] = {
        {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state->window},
        {1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state->window}
    };
    if (!RegisterRawInputDevices(devices, 2, sizeof(devices[0]))) {
        state->startup_error = GetLastError();
        return false;
    }
    return true;
}

unsigned bongo_cat_windows_input_restore(WindowsInputState *state, unsigned owned) {
    if (owned == 3) return owned;
    RAWINPUTDEVICE *items;
    UINT count;
    if (!registrations(state, &items, &count)) return owned;
    unsigned blocked = 0;
    for (UINT i = 0; i < count; ++i) {
        /* Registrations are process-local. Leave another receiver (including
           a foreground-only registration with no target) alone. */
        if (items[i].hwndTarget != state->window)
            blocked |= device_class(&items[i]);
    }
    free(items);
    unsigned missing = 3u & ~(owned | blocked);
    if (!missing) return owned;
    RAWINPUTDEVICE devices[2];
    UINT pending = 0;
    if (missing & 1u) devices[pending++] = (RAWINPUTDEVICE){
        1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state->window};
    if (missing & 2u) devices[pending++] = (RAWINPUTDEVICE){
        1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state->window};
    if (!RegisterRawInputDevices(devices, pending, sizeof(devices[0]))) {
        DWORD error = GetLastError();
        if (error != state->recovery_error) SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
            "[input] Raw Input recovery failed: classes=%u error=%lu",
            missing, (unsigned long)error);
        state->recovery_error = error;
        return owned;
    }
    state->recovery_error = ERROR_SUCCESS;
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] Raw Input background registration restored: classes=%u", missing);
    return owned | missing;
}

void bongo_cat_windows_input_unregister(WindowsInputState *state) {
    RAWINPUTDEVICE *items;
    UINT count;
    if (!registrations(state, &items, &count)) return;
    for (UINT i = 0; i < count; ++i) {
        if (!device_class(&items[i]) || items[i].hwndTarget != state->window)
            continue;
        items[i].dwFlags = RIDEV_REMOVE;
        items[i].hwndTarget = NULL;
        if (!RegisterRawInputDevices(&items[i], 1, sizeof(items[i])))
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                "[input] Raw Input unregister failed: error=%lu",
                (unsigned long)GetLastError());
    }
    free(items);
}
#endif
