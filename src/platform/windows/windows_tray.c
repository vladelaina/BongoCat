#include "bongo_cat/platform.h"
#include "windows_popup.h"
#include "windows_tray.h"
#include "bongo_cat/tray.h"

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>

#define BONGO_CAT_TRAY_MODAL_TIMER ((UINT_PTR)0xBC4F)
static const wchar_t tray_binding_property[] = L"BongoCat.TrayBinding";
static UINT taskbar_created_message;
static void *restore_tray;
static BongoCatTrayRestore restore_callback;
static void *restore_userdata;

typedef struct WindowsTrayBinding {
    WNDPROC original;
    BongoCatTrayClick left_click;
    BongoCatModalTick modal_tick;
    void *userdata;
} WindowsTrayBinding;

static LRESULT CALLBACK tray_window_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    WindowsTrayBinding *binding = GetPropW(window, tray_binding_property);
    WNDPROC original = binding ? binding->original : NULL;
    if (message == WM_USER + 1 && LOWORD(lparam) == WM_LBUTTONUP &&
        binding && binding->left_click) {
        binding->left_click(binding->userdata);
        return 0;
    }
    if (message == WM_USER + 1 && LOWORD(lparam) == WM_CONTEXTMENU && binding) {
        bongo_cat_tray_prepare_menu(binding->userdata, window);
        if (binding->modal_tick) {
            binding->modal_tick(binding->userdata);
            SetTimer(window, BONGO_CAT_TRAY_MODAL_TIMER, 16, NULL);
        }
        LRESULT result = CallWindowProcW(original ? original : DefWindowProcW,
            window, message, wparam, lparam);
        KillTimer(window, BONGO_CAT_TRAY_MODAL_TIMER);
        bongo_cat_windows_popup_complete(window);
        return result;
    }
    if (message == WM_TIMER && wparam == BONGO_CAT_TRAY_MODAL_TIMER &&
        binding && binding->modal_tick) {
        binding->modal_tick(binding->userdata);
        return 0;
    }
    return CallWindowProcW(original ? original : DefWindowProcW,
        window, message, wparam, lparam);
}

static void unbind_tray_window(HWND window) {
    WindowsTrayBinding *binding = GetPropW(window, tray_binding_property);
    if (!binding) return;
    KillTimer(window, BONGO_CAT_TRAY_MODAL_TIMER);
    if ((WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) == tray_window_proc)
        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)binding->original);
    RemovePropW(window, tray_binding_property);
    free(binding);
}

static void bind_tray_window(HWND window, void *tray,
    BongoCatTrayClick left_click, BongoCatModalTick modal_tick,
    void *userdata) {
    wchar_t name[32]; DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() ||
        !GetClassNameW(window, name, (int)(sizeof(name) / sizeof(name[0]))) ||
        wcscmp(name, L"Message") != 0 ||
        (void *)GetWindowLongPtrW(window, GWLP_USERDATA) != tray) return;
    unbind_tray_window(window);
    if (!left_click && !modal_tick) return;
    WindowsTrayBinding *binding = calloc(1, sizeof(*binding));
    if (!binding) return;
    binding->original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    binding->left_click = left_click;
    binding->modal_tick = modal_tick;
    binding->userdata = userdata;
    if (!binding->original || binding->original == tray_window_proc ||
        !SetPropW(window, tray_binding_property, binding)) {
        free(binding);
        return;
    }
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(window, GWLP_WNDPROC,
        (LONG_PTR)tray_window_proc) && GetLastError() != ERROR_SUCCESS) {
        RemovePropW(window, tray_binding_property);
        free(binding);
    }
}

void bongo_cat_platform_set_tray_callbacks(void *tray,
    BongoCatTrayClick left_click, BongoCatModalTick modal_tick,
    BongoCatTrayRestore restore, void *userdata) {
    if (!taskbar_created_message)
        taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    if (tray && restore) {
        restore_tray = tray;
        restore_callback = restore;
        restore_userdata = userdata;
    } else if (!tray || tray == restore_tray) {
        restore_tray = NULL;
        restore_callback = NULL;
        restore_userdata = NULL;
    }
    /* Pinned SDL stores SDL_Tray* in its private message window userdata. */
    HWND window = NULL;
    while ((window = FindWindowExW(HWND_MESSAGE, window, L"Message", NULL)) != NULL)
        bind_tray_window(window, tray, left_click, modal_tick, userdata);
}

void bongo_cat_windows_tray_handle_message(UINT message) {
    if (!taskbar_created_message)
        taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbar_created_message && message == taskbar_created_message &&
        restore_callback)
        restore_callback(restore_userdata);
}
#endif
