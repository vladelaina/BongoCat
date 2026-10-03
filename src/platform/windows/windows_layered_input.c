#include "windows_layered_internal.h"

#include <SDL3/SDL.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

static const wchar_t binding_property[] = L"BongoCat.LayeredBinding";
#define LAYERED_SUBCLASS_ID ((UINT_PTR)0xBC52)

bool bongo_cat_windows_layered_suppressed(HWND source) {
    BongoCatPlatform *platform = source ? GetPropW(source, binding_property) : NULL;
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    return value && value->source_transparent;
}

void bongo_cat_windows_layered_sync_input(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value || !value->proxy) return;
    LONG_PTR style = GetWindowLongPtrW(value->proxy, GWL_EXSTYLE);
    LONG_PTR next = value->forced ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
    if (next != style) SetWindowLongPtrW(value->proxy, GWL_EXSTYLE, next);
}

static LRESULT CALLBACK source_proc(HWND source, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    (void)id;
    BongoCatPlatform *platform = (BongoCatPlatform *)reference;
    BongoCatWindowsLayered *value = platform->presenter;
    if (message == WM_NCDESTROY) {
        if (value) {
            value->source_destroyed = true;
            value->source = NULL;
            value->source_transparent = false;
            value->bound = false;
            value->active = value->has_frame = false;
        }
        RemovePropW(source, binding_property);
        RemoveWindowSubclass(source, source_proc, LAYERED_SUBCLASS_ID);
        return DefSubclassProc(source, message, wparam, lparam);
    }
    if (message == WM_MOUSELEAVE && value && value->proxy) {
        POINT point;
        if (GetCursorPos(&point) && WindowFromPoint(point) == value->proxy) return 0;
    }
    LRESULT result = DefSubclassProc(source, message, wparam, lparam);
    /* A downstream handler may destroy the SDL window/presenter. Reacquire
       the binding only for geometry messages, never dereference a stale ref. */
    if (message != WM_WINDOWPOSCHANGED && message != WM_SHOWWINDOW && message != WM_SIZE)
        return result;
    platform = GetPropW(source, binding_property);
    value = platform ? platform->presenter : NULL;
    if (value && value->proxy && !value->syncing) {
        value->syncing = true;
        if (!value->active || !value->has_frame || !IsWindowVisible(source) || IsIconic(source)) {
            ShowWindow(value->proxy, SW_HIDE);
        } else {
            RECT bounds;
            if (GetWindowRect(source, &bounds)) {
                bool topmost = (GetWindowLongPtrW(source, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
                bool proxy_topmost = (GetWindowLongPtrW(value->proxy, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
                SetWindowPos(value->proxy, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                    bounds.left, bounds.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE |
                    (topmost == proxy_topmost ? SWP_NOZORDER : 0));
                if (value->source_transparent && !IsWindowVisible(value->proxy))
                    ShowWindow(value->proxy, SW_SHOWNOACTIVATE);
            }
        }
        value->syncing = false;
    }
    return result;
}

bool bongo_cat_windows_layered_bind(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform->presenter;
    if (value->bound) return true;
    if (!SetPropW(value->source, binding_property, platform)) return false;
    if (!SetWindowSubclass(value->source, source_proc, LAYERED_SUBCLASS_ID,
        (DWORD_PTR)platform)) {
        RemovePropW(value->source, binding_property);
        return false;
    }
    value->bound = true;
    return true;
}

void bongo_cat_windows_layered_unbind(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform->presenter;
    if (!value || !value->bound) return;
    RemoveWindowSubclass(value->source, source_proc, LAYERED_SUBCLASS_ID);
    RemovePropW(value->source, binding_property);
    value->bound = false;
}

LRESULT CALLBACK bongo_cat_windows_layered_window_proc(HWND window,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
    }
    BongoCatPlatform *platform = (BongoCatPlatform *)GetWindowLongPtrW(window, GWLP_USERDATA);
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = value ? value->source : NULL;
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (value && value->proxy == window) {
            value->proxy = NULL;
            value->has_frame = false;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (!source || !IsWindow(source)) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_NCHITTEST) {
        if (value->forced) return HTTRANSPARENT;
        return SendMessageW(source, message, wparam, lparam);
    }
    if (message == WM_MOUSEACTIVATE) {
        /* SDL's real window must keep keyboard focus and text/IME ownership. */
        SetForegroundWindow(source);
        SetFocus(source);
        return MA_NOACTIVATE;
    }
    if (message == WM_SETCURSOR)
        return SendMessageW(source, message, (WPARAM)source, lparam);
    if (message == WM_DROPFILES) return SendMessageW(source, message, wparam, lparam);
    if (message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK)
        return SendMessageW(source, message, wparam, lparam);
    if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_MOUSELEAVE) {
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
        }
        if (message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL && message != WM_MOUSELEAVE) {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            MapWindowPoints(window, source, &point, 1);
            lparam = MAKELPARAM(point.x, point.y);
        }
        return SendMessageW(source, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
