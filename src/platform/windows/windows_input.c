#include "windows_input_internal.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static volatile LONG receiver_claimed;

static void free_state(WindowsInputState *state);

static void release_state(WindowsInputState *state) {
    if (InterlockedDecrement(&state->references) == 0) free_state(state);
}

void bongo_cat_windows_input_wake(WindowsInputState *state) {
    if (!state->wake_pending) return;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    if (platform && platform->wake_event_type >= SDL_EVENT_USER) {
        SDL_Event wake;
        SDL_zero(wake);
        wake.type = platform->wake_event_type;
        if (SDL_PushEvent(&wake)) state->wake_pending = false;
        else state->diagnostic_wake_failures++;
    }
    ReleaseSRWLockShared(&state->platform_lock);
}

bool bongo_cat_windows_input_push_event(WindowsInputState *state,
    BongoCatInputKind kind, const char *name, float value) {
    if (!state || !name) return false;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    bool pushed = false;
    if (platform) {
        BongoCatInputEvent event = {0};
        event.kind = kind;
        event.timestamp_ms = SDL_GetTicks();
        event.value = value;
        snprintf(event.name, sizeof(event.name), "%s", name);
        pushed = bongo_cat_input_push(platform->input, &event);
        if (!pushed) state->diagnostic_queue_failures++;
        else if (kind == BONGO_CAT_INPUT_KEY_DOWN || kind == BONGO_CAT_INPUT_KEY_UP)
            state->diagnostic_queued_keys++;
    }
    ReleaseSRWLockShared(&state->platform_lock);
    if (pushed) state->wake_pending = true;
    return pushed;
}

static bool input_desktop_available(void) {
    HDESK active = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!active) return false;
    wchar_t active_name[256] = {0}, receiver_name[256] = {0};
    DWORD needed = 0;
    bool available = GetUserObjectInformationW(active, UOI_NAME,
        active_name, sizeof(active_name), &needed) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),
            UOI_NAME, receiver_name, sizeof(receiver_name), &needed) &&
        wcscmp(active_name, receiver_name) == 0;
    CloseDesktop(active);
    return available;
}

static void check_receiver(WindowsInputState *state) {
    unsigned ownership = bongo_cat_windows_input_ownership(state);
    bool inspected = state->registration_error == ERROR_SUCCESS;
    if (!inspected) ownership = state->ownership;
    bool unavailable = !input_desktop_available();
    if (ownership != state->ownership || unavailable != state->desktop_unavailable) {
        if (ownership != state->ownership) SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
            "[input] Raw Input ownership changed: previous=%u current=%u "
            "mouse_flags=%lu keyboard_flags=%lu", state->ownership, ownership,
            (unsigned long)state->mouse_registration_flags,
            (unsigned long)state->keyboard_registration_flags);
        if (ownership != state->ownership || unavailable)
            bongo_cat_windows_input_clear_devices(state);
        else bongo_cat_windows_input_clear_motion(state);
        state->ownership = ownership;
        state->desktop_unavailable = unavailable;
    }
    /* Repair a lost subscription on the normal input desktop, after releasing
       stale keys. Never infer failure from silence or foreground monitor. */
    if (inspected && !unavailable)
        ownership = bongo_cat_windows_input_restore(state, ownership);
    state->ownership = ownership;
    AcquireSRWLockExclusive(&state->relative_lock);
    /* Desktop inspection may be denied even when valid WM_INPUT still arrives. */
    state->receiving = (ownership & 1u) != 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

static void log_receiver(WindowsInputState *state, const char *phase) {
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    bool monitor_known = foreground && GetMonitorInfoW(
        MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL), &monitor);
    /* Snapshot only: counters span the whole interval, not just this window.
       No process access, titles, device identifiers, or key values are logged. */
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] receiver phase=%s uptime_ms=%llu keys=%llu mouse=%llu "
        "background_keys=%llu filtered_keys=%llu duplicate_keys=%llu "
        "queued_keys=%llu queue_failures=%llu wake_failures=%llu "
        "invalid=%llu device_drops=%llu read_failures=%llu device_changes=%llu "
        "owned=%u mouse_flags=%lu keyboard_flags=%lu desktop_unavailable=%d "
        "query_error=%lu recovery_error=%lu last_read_error=%lu devices=%u "
        "monitors=%d foreground_pid=%lu foreground_monitor_known=%d "
        "foreground_primary=%d foreground_rect=%ld,%ld,%ld,%ld",
        phase, (unsigned long long)GetTickCount64(),
        state->diagnostic_keys, state->diagnostic_mouse,
        state->diagnostic_background_keys, state->diagnostic_filtered_keys,
        state->diagnostic_duplicate_keys,
        state->diagnostic_queued_keys, state->diagnostic_queue_failures,
        state->diagnostic_wake_failures, state->diagnostic_invalid,
        state->diagnostic_device_drops, state->diagnostic_read_failures,
        state->diagnostic_device_changes, state->ownership,
        (unsigned long)state->mouse_registration_flags,
        (unsigned long)state->keyboard_registration_flags,
        state->desktop_unavailable, (unsigned long)state->registration_error,
        (unsigned long)state->recovery_error, (unsigned long)state->read_error,
        state->device_count,
        GetSystemMetrics(SM_CMONITORS), (unsigned long)pid, monitor_known,
        (monitor.dwFlags & MONITORINFOF_PRIMARY) != 0,
        monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right, monitor.rcMonitor.bottom);
}

static DWORD WINAPI input_thread(void *context) {
    WindowsInputState *state = context;
    if (WaitForSingleObject(state->stop, state->test_start_delay_ms) == WAIT_OBJECT_0) {
        SetEvent(state->ready);
        release_state(state);
        return 0;
    }
    state->registered = bongo_cat_windows_input_receiver_create(state);
    if (!state->registered) SDL_LogError(SDL_LOG_CATEGORY_INPUT,
        "Raw Input initialization failed: error=%lu", (unsigned long)state->startup_error);
    if (state->registered) {
        state->ownership = 3;
        check_receiver(state);
        log_receiver(state, "started");
        state->diagnostic_ms = GetTickCount64();
    }
    SetEvent(state->ready);
    ULONGLONG last_check = GetTickCount64(), last_wake = 0;
    while (state->registered) {
        DWORD timeout = state->wake_pending || state->retry_events ? 8 : 1000;
        DWORD wait = MsgWaitForMultipleObjectsEx(1, &state->stop, timeout,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT,
                "[input] Raw Input wait failed: error=%lu", (unsigned long)GetLastError());
            break;
        }
        if (!bongo_cat_windows_input_dispatch(state)) break;
        ULONGLONG now = GetTickCount64();
        if (now - last_check >= 1000) {
            check_receiver(state);
            last_check = now;
        }
        if (now - state->diagnostic_ms >= 10000) {
            log_receiver(state, "periodic");
            state->diagnostic_ms = now;
        }
        bongo_cat_windows_input_flush(state);
        if (now - last_wake >= 8) {
            bongo_cat_windows_input_wake(state);
            last_wake = now;
        }
    }
    AcquireSRWLockExclusive(&state->relative_lock);
    state->receiving = false;
    ReleaseSRWLockExclusive(&state->relative_lock);
    bongo_cat_windows_input_clear_devices(state);
    bongo_cat_windows_input_wake(state);
    if (state->registered) log_receiver(state, "stopping");
    bongo_cat_windows_input_receiver_destroy(state);
    release_state(state);
    return 0;
}

static void free_state(WindowsInputState *state) {
    if (state->thread) CloseHandle(state->thread);
    if (state->ready) CloseHandle(state->ready);
    if (state->stop) CloseHandle(state->stop);
    free(state);
    InterlockedExchange(&receiver_claimed, 0);
}

bool bongo_cat_windows_input_start(BongoCatPlatform *platform) {
    if (!platform || !platform->input || platform->native) return false;
    if (InterlockedCompareExchange(&receiver_claimed, 1, 0) != 0) return false;
    /* SDL retains legacy keyboard messages; this receiver owns Raw Input. */
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "0", SDL_HINT_OVERRIDE);
    WindowsInputState *state = calloc(1, sizeof(*state));
    if (!state) { InterlockedExchange(&receiver_claimed, 0); return false; }
    InitializeSRWLock(&state->platform_lock);
    InitializeSRWLock(&state->relative_lock);
    state->platform = platform;
    const char *delay_text = SDL_getenv("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS");
    if (delay_text) {
        unsigned long delay = strtoul(delay_text, NULL, 10);
        state->test_start_delay_ms = delay > 10000 ? 10000 : (DWORD)delay;
    }
    state->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    state->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    /* The caller retains ownership until the thread handle is published and
       stop finishes waiting; the worker retains ownership until cleanup. */
    state->references = 2;
    state->thread = state->stop && state->ready ?
        CreateThread(NULL, 0, input_thread, state, 0, NULL) : NULL;
    if (!state->thread) { free_state(state); return false; }
    platform->native = state;
    DWORD wait = WaitForSingleObject(state->ready, 1500);
    if (wait == WAIT_OBJECT_0 && state->registered) return true;
    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
        "[input] Raw Input startup failed: wait=%lu", (unsigned long)wait);
    bongo_cat_windows_input_stop(platform);
    return false;
}

void bongo_cat_windows_input_stop(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->platform_lock);
    state->platform = NULL;
    ReleaseSRWLockExclusive(&state->platform_lock);
    SetEvent(state->stop);
    platform->native = NULL;
    if (WaitForSingleObject(state->thread, 3000) != WAIT_OBJECT_0) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT,
            "[input] Raw Input thread did not stop; cleanup is deferred until thread exit");
    }
    release_state(state);
}
#endif
