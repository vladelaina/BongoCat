#include "windows_capture.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <dwmapi.h>

static UINT transparency_repair_message;
static const wchar_t transparent_property[] = L"BongoCat.TransparentWindow";
static const wchar_t transparency_proc_property[] =
    L"BongoCat.TransparentWindowProc";
static const wchar_t transparency_repair_property[] =
    L"BongoCat.TransparentWindowRepairPending";
static bool transparency_warning_emitted;

static bool is_transparent(HWND window) {
    return window && GetPropW(window, transparent_property) != NULL;
}

void bongo_cat_windows_capture_mark_transparent(HWND window, bool enabled) {
    if (!window) return;
    if (enabled) SetPropW(window, transparent_property, (HANDLE)1);
    else {
        RemovePropW(window, transparent_property);
        RemovePropW(window, transparency_repair_property);
    }
}

static void register_message(void) {
    if (!transparency_repair_message)
        transparency_repair_message = RegisterWindowMessageW(
            L"BongoCat.TransparentWindow.Repair");
}

static LRESULT CALLBACK transparency_window_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (bongo_cat_windows_capture_handle_message(window, message, wparam))
        return 0;
    WNDPROC original = (WNDPROC)GetPropW(window, transparency_proc_property);
    return CallWindowProcW(original ? original : DefWindowProcW, window,
        message, wparam, lparam);
}

void bongo_cat_windows_capture_install_transparency_handler(HWND window) {
    if (!window || GetPropW(window, transparency_proc_property)) return;
    WNDPROC original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (!original || !SetPropW(window, transparency_proc_property,
        (HANDLE)original)) return;
    if (!SetWindowLongPtrW(window, GWLP_WNDPROC,
        (LONG_PTR)transparency_window_proc))
        RemovePropW(window, transparency_proc_property);
}

bool bongo_cat_windows_capture_restore_transparency(HWND window) {
    if (!window) return false;
    HRGN region = CreateRectRgn(-1, -1, 0, 0);
    if (!region) return false;
    DWM_BLURBEHIND blur = {0};
    blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    blur.fEnable = TRUE;
    blur.hRgnBlur = region;
    HRESULT result = DwmEnableBlurBehindWindow(window, &blur);
    DeleteObject(region);
    if (SUCCEEDED(result)) {
        /* DWM may restore a border or rounded corner after a shell/DPI
           transition. Keep transparent popup windows borderless as well. */
        const int no_rounding = 1; /* DWMWCP_DONOTROUND */
        const DWORD no_border = 0xfffffffeu; /* DWMWA_COLOR_NONE */
        DwmSetWindowAttribute(window, 33, &no_rounding, sizeof(no_rounding));
        DwmSetWindowAttribute(window, 34, &no_border, sizeof(no_border));
    }
    if (FAILED(result) && !transparency_warning_emitted) {
        transparency_warning_emitted = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Cannot restore Windows transparent composition (0x%08lx)",
            (unsigned long)result);
    }
    return SUCCEEDED(result);
}

void bongo_cat_windows_capture_repair_transparency(HWND window) {
    if (!is_transparent(window)) return;
    if (bongo_cat_windows_capture_restore_transparency(window)) {
        /* A composition reset need not produce a paint message. */
        InvalidateRect(window, NULL, FALSE);
    }
}

static void schedule_repair(HWND window) {
    if (!is_transparent(window)) return;
    register_message();
    if (!transparency_repair_message ||
        GetPropW(window, transparency_repair_property)) return;
    if (SetPropW(window, transparency_repair_property, (HANDLE)1) &&
        !PostMessageW(window, transparency_repair_message, 0, 0))
        RemovePropW(window, transparency_repair_property);
}

bool bongo_cat_windows_capture_handle_transparency_message(
    HWND window, UINT message, WPARAM wparam) {
    (void)wparam;
    register_message();
    if (is_transparent(window)) {
        /* SDL's default erase handler fills the client area with black. */
        if (message == WM_ERASEBKGND) return true;
        switch (message) {
        case WM_DWMCOMPOSITIONCHANGED:
        case WM_DWMNCRENDERINGCHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
        case WM_THEMECHANGED:
        case WM_NCACTIVATE:
        case WM_SHOWWINDOW:
        case WM_STYLECHANGED:
        case WM_WINDOWPOSCHANGED:
            schedule_repair(window);
            break;
        default:
            break;
        }
    }
    if (transparency_repair_message && message == transparency_repair_message) {
        RemovePropW(window, transparency_repair_property);
        bongo_cat_windows_capture_repair_transparency(window);
        return true;
    }
    return false;
}
#endif
