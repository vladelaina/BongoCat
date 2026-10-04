#include "test.h"
#include "windows_input_internal.h"

#include <SDL3/SDL.h>

static unsigned registered_to(HWND window) {
    WindowsInputState state = {.window = window};
    return bongo_cat_windows_input_ownership(&state);
}

static HANDLE blocked_receiver_entered, blocked_receiver_release;
static WNDPROC original_receiver_proc;
#define TEST_BLOCK_RECEIVER (WM_APP + 271)

static LRESULT CALLBACK blocked_receiver_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message == TEST_BLOCK_RECEIVER) {
        SetEvent(blocked_receiver_entered);
        WaitForSingleObject(blocked_receiver_release, INFINITE);
        return 0;
    }
    return CallWindowProcW(original_receiver_proc, window, message, wparam, lparam);
}

static void test_deferred_cleanup(BongoCatPlatform *platform) {
    DWORD before = 0, after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    CHECK(bongo_cat_windows_input_start(platform));
    WindowsInputState *state = platform->native;
    if (!state) return;
    HANDLE worker = NULL;
    blocked_receiver_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    blocked_receiver_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(DuplicateHandle(GetCurrentProcess(), state->thread, GetCurrentProcess(),
        &worker, SYNCHRONIZE, FALSE, 0));
    CHECK(blocked_receiver_entered && blocked_receiver_release);
    if (worker && blocked_receiver_entered && blocked_receiver_release) {
        original_receiver_proc = (WNDPROC)GetWindowLongPtrW(state->window, GWLP_WNDPROC);
        CHECK(SetWindowLongPtrW(state->window, GWLP_WNDPROC,
            (LONG_PTR)blocked_receiver_proc) != 0);
        CHECK(PostMessageW(state->window, TEST_BLOCK_RECEIVER, 0, 0));
        DWORD entered = WaitForSingleObject(blocked_receiver_entered, 3000);
        CHECK(entered == WAIT_OBJECT_0);
        if (entered == WAIT_OBJECT_0) {
            bongo_cat_windows_input_stop(platform);
            CHECK(platform->native == NULL);
            CHECK(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT);
            BongoCatPlatform duplicate = {.input = platform->input};
            CHECK(!bongo_cat_windows_input_start(&duplicate));
            bongo_cat_windows_input_stop(&duplicate);
        }
    }
    if (blocked_receiver_release) SetEvent(blocked_receiver_release);
    bongo_cat_windows_input_stop(platform);
    if (worker) {
        CHECK(WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0);
        CloseHandle(worker);
    }
    if (blocked_receiver_entered) CloseHandle(blocked_receiver_entered);
    if (blocked_receiver_release) CloseHandle(blocked_receiver_release);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    CHECK(after == before);
    CHECK(bongo_cat_windows_input_start(platform));
    bongo_cat_windows_input_stop(platform);
}

static void test_registration_flags(void) {
    HWND receiver = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(receiver != NULL);
    if (!receiver) return;
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK, receiver};
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    WindowsInputState state = {.window = receiver};
    CHECK(bongo_cat_windows_input_ownership(&state) == 1);
    CHECK(state.registration_error == ERROR_SUCCESS);
    CHECK((state.mouse_registration_flags & RIDEV_INPUTSINK) != 0);
    CHECK(registered_to(NULL) == 0);
    mouse.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 1);
    mouse.dwFlags = 0;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 0);
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 0);
    DestroyWindow(receiver);
}

static void test_registration_recovery(void) {
    HWND receiver = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    HWND other = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(receiver && other);
    if (!receiver || !other) {
        if (receiver) DestroyWindow(receiver);
        if (other) DestroyWindow(other);
        return;
    }
    WindowsInputState state = {.window = receiver};
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK, receiver};
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    /* Restore only the missing keyboard; keep the healthy mouse unchanged. */
    CHECK(bongo_cat_windows_input_restore(&state, registered_to(receiver)) == 3);
    CHECK(registered_to(receiver) == 3);
    CHECK(bongo_cat_windows_input_ownership(&state) == 3);
    CHECK(state.mouse_registration_flags == RIDEV_INPUTSINK);
    CHECK(state.keyboard_registration_flags & RIDEV_INPUTSINK);
    CHECK(bongo_cat_windows_input_restore(&state, 3) == 3);

    RAWINPUTDEVICE keyboard = {1, 6, 0, receiver};
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    CHECK(registered_to(receiver) == 1);
    CHECK(bongo_cat_windows_input_restore(&state, 1) == 3);
    CHECK(registered_to(receiver) == 3);

    /* A competing receiver must not be overwritten, even without INPUTSINK. */
    keyboard.hwndTarget = other;
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    CHECK(bongo_cat_windows_input_restore(&state, registered_to(receiver)) == 1);
    keyboard.dwFlags = RIDEV_INPUTSINK;
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    CHECK(bongo_cat_windows_input_restore(&state, 1) == 1);
    CHECK(registered_to(other) == 2);
    keyboard.dwFlags = 0;
    keyboard.hwndTarget = NULL;
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    CHECK(bongo_cat_windows_input_restore(&state, 1) == 1);

    keyboard.dwFlags = RIDEV_REMOVE;
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    CHECK(bongo_cat_windows_input_restore(&state, 1) == 3);
    bongo_cat_windows_input_unregister(&state);
    CHECK(registered_to(receiver) == 0);
    CHECK(bongo_cat_windows_input_restore(&state, 0) == 3);
    CHECK(registered_to(receiver) == 3);
    bongo_cat_windows_input_unregister(&state);
    DestroyWindow(other);
    DestroyWindow(receiver);
}

static void test_worker_recovery(HWND receiver) {
    RAWINPUTDEVICE keyboard = {1, 6, RIDEV_REMOVE, NULL};
    CHECK(RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)));
    ULONGLONG deadline = GetTickCount64() + 3000;
    while (registered_to(receiver) != 3 && GetTickCount64() < deadline)
        SDL_Delay(10);
    CHECK(registered_to(receiver) == 3);
}

void test_windows_raw_receiver(void) {
    test_registration_flags();
    test_registration_recovery();
    BongoCatInputState input;
    bongo_cat_input_init(&input);
    BongoCatPlatform platform = {.input = &input};
    SDL_setenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_FAILURE", "1", 1);
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_FAILURE");
    SDL_setenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS", "5000", 1);
    ULONGLONG started = GetTickCount64();
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(GetTickCount64() - started < 4000);
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS");

    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "0", SDL_HINT_OVERRIDE);
    CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("Raw Input test", 64, 64, SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    CHECK(bongo_cat_windows_input_start(&platform));
    if (platform.native) {
        WindowsInputState *state = platform.native;
        HWND receiver = state->window;
        CHECK(registered_to(receiver) == 3);
        CHECK(!IsWindowVisible(receiver));
        CHECK(GetWindowThreadProcessId(receiver, NULL) != GetCurrentThreadId());
        BongoCatPlatform duplicate = {.input = &input};
        CHECK(!bongo_cat_windows_input_start(&duplicate));
        SDL_Window *preferences = SDL_CreateWindow("Raw Input preferences test",
            64, 64, SDL_WINDOW_HIDDEN);
        CHECK(preferences != NULL);
        SDL_PumpEvents();
        CHECK(registered_to(receiver) == 3);
        test_worker_recovery(receiver);
        SDL_DestroyWindow(preferences);
        bongo_cat_windows_input_stop(&platform);
        CHECK(platform.native == NULL && registered_to(receiver) == 0);
        CHECK(!IsWindow(receiver));
        bongo_cat_windows_input_stop(&platform);
    }
    test_deferred_cleanup(&platform);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    HWND other = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(other != NULL);
    if (!other) return;
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, other};
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(registered_to(other) == 1);
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));

    CHECK(bongo_cat_windows_input_start(&platform));
    if (platform.native) {
        mouse.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        mouse.hwndTarget = other;
        CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
        bongo_cat_windows_input_stop(&platform);
        CHECK(registered_to(other) == 1);
        mouse.dwFlags = RIDEV_REMOVE;
        mouse.hwndTarget = NULL;
        CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    }
    DestroyWindow(other);
}
