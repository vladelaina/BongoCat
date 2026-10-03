#include "windows_input_internal.h"

#ifdef _WIN32
#include <SDL3/SDL_log.h>
static const wchar_t receiver_class[] = L"BongoCat Raw Input Receiver";

static LRESULT CALLBACK receiver_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    WindowsInputState *state = (WindowsInputState *)GetWindowLongPtrW(window,
        GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        state = create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
    }
    if (state && message == WM_INPUT) {
        RAWINPUT packet = {0};
        UINT bytes = sizeof(packet);
        UINT received = GetRawInputData((HRAWINPUT)lparam, RID_INPUT, &packet,
            &bytes, sizeof(RAWINPUTHEADER));
        if (received == (UINT)-1) {
            DWORD error = GetLastError();
            state->diagnostic_read_failures++;
            if (error != state->read_error) SDL_LogError(SDL_LOG_CATEGORY_INPUT,
                "Raw Input read failed: error=%lu", (unsigned long)error);
            state->read_error = error;
        } else bongo_cat_windows_input_packet(state, &packet, received);
        /* DefWindowProc performs the required foreground WM_INPUT cleanup. */
    } else if (state && message == WM_INPUT_DEVICE_CHANGE) {
        if (wparam == GIDC_REMOVAL || wparam == GIDC_ARRIVAL) {
            state->diagnostic_device_changes++;
            bongo_cat_windows_input_remove_device(state, (HANDLE)lparam);
            bongo_cat_windows_input_clear_motion(state);
        }
    } else if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool bongo_cat_windows_input_receiver_create(WindowsInputState *state) {
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW type = {.lpfnWndProc = receiver_proc,
        .hInstance = instance, .lpszClassName = receiver_class};
    state->window_class = RegisterClassW(&type);
    if (!state->window_class && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        state->startup_error = GetLastError();
        return false;
    }
    state->window = CreateWindowExW(0, receiver_class, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, instance, state);
    if (!state->window) { state->startup_error = GetLastError(); return false; }
    return bongo_cat_windows_input_register(state);
}

void bongo_cat_windows_input_receiver_destroy(WindowsInputState *state) {
    if (state->window) {
        bongo_cat_windows_input_unregister(state);
        DestroyWindow(state->window);
        state->window = NULL;
    }
    if (state->window_class) {
        UnregisterClassW(receiver_class, GetModuleHandleW(NULL));
        state->window_class = 0;
    }
}

bool bongo_cat_windows_input_dispatch(WindowsInputState *state) {
    MSG message;
    /* Bound each batch so high-rate mice cannot starve shutdown. */
    for (unsigned i = 0; i < 256 && PeekMessageW(&message,
        NULL, 0, 0, PM_REMOVE); ++i) {
        if (message.message == WM_QUIT ||
            WaitForSingleObject(state->stop, 0) == WAIT_OBJECT_0) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}
#endif
