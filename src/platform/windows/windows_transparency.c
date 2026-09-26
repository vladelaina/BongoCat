#include "windows_capture.h"
#include "windows_layered.h"
#include "bongo_cat/log.h"
#include "bongo_cat/platform.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <dwmapi.h>

#ifndef DWMWA_CLOAK
/* 老版本 SDK 的 dwmapi.h 里还没有这个名字 (Windows 10 2004+ 才有)。 */
#define DWMWA_CLOAK 13
#endif

static const wchar_t capture_only_property[] = L"BongoCat.CaptureOnly";

static HWND platform_native_window(BongoCatPlatform *platform) {
    if (!platform || !platform->window) return NULL;
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

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
    if (bongo_cat_windows_layered_suppressed(window)) return true;
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
        /* Ordinary moves/resizes preserve transparency. Repairing on every
           WM_WINDOWPOSCHANGED repeatedly invalidates the animated surface. */
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

bool bongo_cat_platform_capture_only_supported(void) { return true; }

/*
 * "只在录屏软件里显示": 用 DWM 把窗口从桌面合成里摘掉 (DWMWA_CLOAK)。
 *
 * 被隐藏的窗口本身照常渲染、照常呈现, 只是 DWM 不再把它画到屏幕上, 因此
 * 采集侧仍然拿得到画面:
 *   - 游戏采集 (Graphics Hook) 直接读交换链/GL 呈现, 与窗口是否可见无关;
 *   - 窗口采集读的是窗口自己的内容, 也与桌面是否显示无关。
 * 桌面这边则看不到任何东西 —— 主窗口在桌面上完全消失。
 *
 * 因为窗口在桌面上不可见, 点击穿透必须整体生效 (见 windows_pointer.c):
 * 否则用户会在看不见的窗口上点到一只隐形的猫。
 */
bool bongo_cat_platform_set_capture_only(BongoCatPlatform *platform, bool enabled) {
    HWND window = platform_native_window(platform);
    if (!window) return false;
    /* 属性已经是我们想要的状态时不再打扰 DWM: 这个函数会被反复调用。 */
    HANDLE applied = GetPropW(window, capture_only_property);
    if (applied && (applied == (HANDLE)(INT_PTR)2) == enabled) {
        platform->capture_only = enabled;
        return true;
    }
    BOOL cloak = enabled ? TRUE : FALSE;
    HRESULT result = DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak,
        sizeof(cloak));
    if (FAILED(result)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Cannot %s the capture-only window hiding (0x%08lx)",
            enabled ? "enable" : "disable", (unsigned long)result);
        return false;
    }
    SetPropW(window, capture_only_property, (HANDLE)(INT_PTR)(enabled ? 2 : 1));
    platform->capture_only = enabled;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[runtime] capture-only=%d (仅在录屏软件中显示: %s, 桌面%s显示, 采集仍可捕获)",
        enabled ? 1 : 0, enabled ? "已开启" : "已关闭", enabled ? "不再" : "恢复");
    return true;
}
#endif
