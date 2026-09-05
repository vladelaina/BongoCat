#include "windows_input.h"
#include "windows_keys.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct WindowsInputState {
    BongoCatPlatform *platform;
    SRWLOCK platform_lock;
    SRWLOCK relative_lock;
    HANDLE thread;
    HANDLE stop;
    HHOOK keyboard;
    HHOOK mouse;
    BongoCatWindowsKeyboard keyboard_state;
    UINT test_drop_key_up;
    DWORD test_start_delay_ms;
    DWORD keyboard_hook_error, mouse_hook_error;
    unsigned long long keyboard_events, mouse_moves, mouse_buttons;
    unsigned long long keyboard_emitted, keyboard_ignored;
    unsigned long long keyboard_queue_failures;
    unsigned long long mouse_button_queue_failures;
    unsigned long long reported_keyboard_events, reported_mouse_moves;
    unsigned long long reported_mouse_buttons;
    ULONGLONG last_diagnostic_ms;
    bool diagnostic_ready;
    POINT last_mouse;
    DWORD last_mouse_flags;
    bool last_mouse_known;
    DWORD last_key_code, last_key_message, last_key_flags;
    char last_key_name[16];
    bool last_key_known;
    POINT relative_mouse;
    bool relative_mouse_known;
    long long relative_x, relative_y;
    unsigned long long relative_samples, relative_resets;
} WindowsInputState;

static WindowsInputState *global_state;

static void wake_main_thread(WindowsInputState *state) {
    if (!state || !state->platform) return;
    SDL_Event wake = {0};
    wake.type = state->platform->wake_event_type;
    SDL_PushEvent(&wake);
}

static bool push_event(BongoCatInputKind kind, const char *name, float value) {
    WindowsInputState *state = global_state;
    if (!state || !name) return false;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    if (!platform) {
        ReleaseSRWLockShared(&state->platform_lock);
        return false;
    }
    BongoCatInputEvent event = {0};
    event.kind = kind;
    event.timestamp_ms = GetTickCount64();
    event.value = value;
    snprintf(event.name, sizeof(event.name), "%s", name);
    bool pushed = bongo_cat_input_push(platform->input, &event);
    if (pushed) wake_main_thread(state);
    ReleaseSRWLockShared(&state->platform_lock);
    return pushed;
}

static void emit_key(bool down, const char *name, void *userdata) {
    (void)userdata;
    WindowsInputState *state = global_state;
    bool pushed = push_event(down ? BONGO_CAT_INPUT_KEY_DOWN :
        BONGO_CAT_INPUT_KEY_UP, name, down ? 1.0f : 0.0f);
    if (!state) return;
    if (pushed) state->keyboard_emitted++;
    else state->keyboard_queue_failures++;
}

static LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM data) {
    WindowsInputState *state = global_state;
    if (code == HC_ACTION && state && data) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)data;
        state->keyboard_events++;
        char key_name[16];
        const char *mapped_name = bongo_cat_windows_key_name(key, key_name);
        state->last_key_code = key->vkCode;
        state->last_key_message = (DWORD)message;
        state->last_key_flags = key->flags;
        state->last_key_known = mapped_name != NULL;
        snprintf(state->last_key_name, sizeof(state->last_key_name), "%s",
            mapped_name ? mapped_name : "none");
        bool emitted = bongo_cat_windows_keyboard_event(&state->keyboard_state,
            key, message,
            &state->test_drop_key_up, emit_key, NULL);
        if (!emitted) state->keyboard_ignored++;
    }
    return CallNextHookEx(NULL, code, message, data);
}

static const char *mouse_button(WPARAM message, DWORD mouse_data) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return "Left";
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return "Right";
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return "Middle";
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        return HIWORD(mouse_data) == XBUTTON1 ? "Back" : "Forward";
    default: return NULL;
    }
}

static LRESULT CALLBACK mouse_hook(int code, WPARAM message, LPARAM data) {
    WindowsInputState *state = global_state;
    if (code == HC_ACTION && state && data) {
        const MSLLHOOKSTRUCT *mouse = (const MSLLHOOKSTRUCT *)data;
        AcquireSRWLockExclusive(&state->relative_lock);
        if (message == WM_MOUSEMOVE) {
            if (state->relative_mouse_known) {
                state->relative_x += (long long)mouse->pt.x -
                    (long long)state->relative_mouse.x;
                state->relative_y += (long long)mouse->pt.y -
                    (long long)state->relative_mouse.y;
                state->relative_samples++;
            }
            state->relative_mouse = mouse->pt;
            state->relative_mouse_known = true;
        }
        state->last_mouse = mouse->pt;
        state->last_mouse_flags = mouse->flags;
        state->last_mouse_known = true;
        ReleaseSRWLockExclusive(&state->relative_lock);
        if (message == WM_MOUSEMOVE) {
            state->mouse_moves++;
            AcquireSRWLockShared(&state->platform_lock);
            BongoCatPlatform *platform = state->platform;
            if (platform && bongo_cat_input_mouse(platform->input,
                mouse->pt.x, mouse->pt.y)) wake_main_thread(state);
            ReleaseSRWLockShared(&state->platform_lock);
        } else {
            const char *name = mouse_button(message, mouse->mouseData);
            bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
            if (name) {
                state->mouse_buttons++;
                if (!push_event(down ? BONGO_CAT_INPUT_MOUSE_DOWN :
                    BONGO_CAT_INPUT_MOUSE_UP, name, down ? 1.0f : 0.0f))
                    state->mouse_button_queue_failures++;
            }
        }
    }
    return CallNextHookEx(NULL, code, message, data);
}

static void dispatch_messages(void) {
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void log_input_summary(WindowsInputState *state, ULONGLONG now_ms) {
    RECT clip = {0};
    bool clip_known = GetClipCursor(&clip) != FALSE;
    RECT desktop = {
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0
    };
    desktop.right = desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    desktop.bottom = desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    bool confined = clip_known && (clip.left > desktop.left ||
        clip.top > desktop.top || clip.right < desktop.right ||
        clip.bottom < desktop.bottom);
    HWND foreground = GetForegroundWindow();
    DWORD foreground_pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foreground_pid);
    POINT physical = {0};
    bool physical_known = GetPhysicalCursorPos(&physical) != FALSE;
    CURSORINFO cursor = {.cbSize = sizeof(cursor)};
    bool cursor_known = GetCursorInfo(&cursor) != FALSE;
    bool cursor_visible = !cursor_known ||
        (cursor.flags & CURSOR_SHOWING) != 0;
    unsigned long long key_delta = state->keyboard_events -
        state->reported_keyboard_events;
    unsigned long long move_delta = state->mouse_moves -
        state->reported_mouse_moves;
    unsigned long long button_delta = state->mouse_buttons -
        state->reported_mouse_buttons;
    bool active = confined || !cursor_visible || key_delta ||
        move_delta || button_delta;
    ULONGLONG interval_ms = active ? 30000 : 60000;
    if (state->diagnostic_ready &&
        now_ms - state->last_diagnostic_ms < interval_ms) return;
    long long relative_x, relative_y;
    unsigned long long relative_samples;
    AcquireSRWLockShared(&state->relative_lock);
    relative_x = state->relative_x;
    relative_y = state->relative_y;
    relative_samples = state->relative_samples;
    ReleaseSRWLockShared(&state->relative_lock);
    SDL_Log("[input] Windows hooks: keyboard=%d mouse=%d "
        "key_events=%llu(+%llu) mouse_moves=%llu(+%llu) "
        "mouse_buttons=%llu(+%llu) last_known=%d last=%ld,%ld flags=0x%lx "
        "last_key_known=%d last_key=%s code=%lu message=%lu flags=0x%lx "
        "key_emitted=%llu key_ignored=%llu key_queue_failures=%llu "
        "mouse_button_queue_failures=%llu relative_pending=%lld,%lld "
        "relative_samples=%llu "
        "physical_known=%d physical=%ld,%ld cursor_known=%d cursor_visible=%d "
        "cursor_confined=%d clip_known=%d clip=%ld,%ld,%ld,%ld "
        "desktop=%ld,%ld,%ld,%ld foreground_pid=%lu own_foreground=%d",
        state->keyboard != NULL, state->mouse != NULL,
        state->keyboard_events, key_delta, state->mouse_moves, move_delta,
        state->mouse_buttons, button_delta,
        state->last_mouse_known, (long)state->last_mouse.x,
        (long)state->last_mouse.y, (unsigned long)state->last_mouse_flags,
        state->last_key_known, state->last_key_name[0] ? state->last_key_name : "none",
        (unsigned long)state->last_key_code,
        (unsigned long)state->last_key_message,
        (unsigned long)state->last_key_flags,
        state->keyboard_emitted, state->keyboard_ignored,
        state->keyboard_queue_failures, state->mouse_button_queue_failures,
        relative_x, relative_y, relative_samples,
        physical_known,
        (long)physical.x, (long)physical.y,
        cursor_known, cursor_visible,
        confined, clip_known,
        (long)clip.left, (long)clip.top, (long)clip.right, (long)clip.bottom,
        (long)desktop.left, (long)desktop.top,
        (long)desktop.right, (long)desktop.bottom,
        (unsigned long)foreground_pid,
        foreground_pid == GetCurrentProcessId());
    state->reported_keyboard_events = state->keyboard_events;
    state->reported_mouse_moves = state->mouse_moves;
    state->reported_mouse_buttons = state->mouse_buttons;
    state->last_diagnostic_ms = now_ms;
    state->diagnostic_ready = true;
}

static DWORD WINAPI input_thread(void *context) {
    WindowsInputState *state = context;
    if (state->test_start_delay_ms && WaitForSingleObject(state->stop,
        state->test_start_delay_ms) == WAIT_OBJECT_0) return 0;
    if (WaitForSingleObject(state->stop, 0) == WAIT_OBJECT_0) return 0;
    global_state = state;
    SetLastError(ERROR_SUCCESS);
    state->keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, NULL, 0);
    if (!state->keyboard) state->keyboard_hook_error = GetLastError();
    SetLastError(ERROR_SUCCESS);
    state->mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, NULL, 0);
    if (!state->mouse) state->mouse_hook_error = GetLastError();
    SDL_Log("[input] Windows hook installation: keyboard=%d error=%lu "
        "mouse=%d error=%lu", state->keyboard != NULL,
        (unsigned long)state->keyboard_hook_error, state->mouse != NULL,
        (unsigned long)state->mouse_hook_error);
    ULONGLONG last_reconcile_ms = GetTickCount64();
    ULONGLONG next_diagnostic_probe_ms = last_reconcile_ms;
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjects(1, &state->stop, FALSE, 25,
            QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) dispatch_messages();
        else if (wait == WAIT_FAILED) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Windows input thread wait failed");
            break;
        }
        ULONGLONG now_ms = GetTickCount64();
        if (now_ms >= next_diagnostic_probe_ms) {
            log_input_summary(state, now_ms);
            next_diagnostic_probe_ms = now_ms + 1000;
        }
        if (now_ms - last_reconcile_ms >= 25) {
            bongo_cat_windows_keyboard_reconcile(&state->keyboard_state,
                now_ms, emit_key, NULL);
            last_reconcile_ms = now_ms;
        }
    }
    state->diagnostic_ready = false;
    log_input_summary(state, GetTickCount64());
    if (state->keyboard) UnhookWindowsHookEx(state->keyboard);
    if (state->mouse) UnhookWindowsHookEx(state->mouse);
    global_state = NULL;
    return 0;
}

bool bongo_cat_windows_input_take_relative(BongoCatPlatform *platform,
    double *x, double *y) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state || !x || !y) return false;
    AcquireSRWLockExclusive(&state->relative_lock);
    long long relative_x = state->relative_x;
    long long relative_y = state->relative_y;
    state->relative_x = 0;
    state->relative_y = 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
    *x = (double)relative_x;
    *y = (double)relative_y;
    return relative_x != 0 || relative_y != 0;
}

void bongo_cat_windows_input_reset_relative(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->relative_lock);
    state->relative_x = 0;
    state->relative_y = 0;
    state->relative_mouse_known = state->last_mouse_known;
    if (state->last_mouse_known) state->relative_mouse = state->last_mouse;
    state->relative_resets++;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

bool bongo_cat_windows_input_start(BongoCatPlatform *platform) {
    if (!platform || SDL_getenv("BONGO_CAT_TEST_HOOK_FAILURE")) return false;
    WindowsInputState *state = calloc(1, sizeof(*state));
    if (!state) return false;
    InitializeSRWLock(&state->platform_lock);
    InitializeSRWLock(&state->relative_lock);
    state->platform = platform;
    const char *drop_key_up = SDL_getenv("BONGO_CAT_TEST_DROP_KEY_UP");
    if (drop_key_up) state->test_drop_key_up =
        (UINT)strtoul(drop_key_up, NULL, 10);
    const char *start_delay = SDL_getenv("BONGO_CAT_TEST_HOOK_DELAY_MS");
    if (start_delay) {
        unsigned long delay = strtoul(start_delay, NULL, 10);
        state->test_start_delay_ms = delay > 10000 ? 10000 : (DWORD)delay;
    }
    state->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    state->thread = state->stop ?
        CreateThread(NULL, 0, input_thread, state, 0, NULL) : NULL;
    if (!state->stop || !state->thread) {
        if (state->thread) CloseHandle(state->thread);
        if (state->stop) CloseHandle(state->stop);
        free(state);
        return false;
    }
    platform->native = state;
    return true;
}

void bongo_cat_windows_input_stop(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->platform_lock);
    state->platform = NULL;
    ReleaseSRWLockExclusive(&state->platform_lock);
    SetEvent(state->stop);
    if (WaitForSingleObject(state->thread, 3000) != WAIT_OBJECT_0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Windows input thread did not stop; its state will remain isolated until exit");
        platform->native = NULL;
        return;
    }
    CloseHandle(state->thread);
    CloseHandle(state->stop);
    free(state);
    platform->native = NULL;
}
#endif
