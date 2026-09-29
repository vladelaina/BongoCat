#include "windows_snapshot_internal.h"

#include <windowsx.h>

static const wchar_t snapshot_property[] = L"BongoCat.InteractionSnapshot";

bool bongo_cat_windows_snapshot_hit(BongoCatWindowsSnapshot *s, float x, float y) {
    RECT rect;
    if (!s || !GetWindowRect(s->source_handle, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top ||
        x < 0 || y < 0 || x >= rect.right - rect.left || y >= rect.bottom - rect.top)
        return false;
    SDL_FRect destination = s->custom_destination ? s->destination :
        (SDL_FRect){(float)rect.left, (float)rect.top,
            (float)(rect.right - rect.left), (float)(rect.bottom - rect.top)};
    x += rect.left - destination.x;
    y += rect.top - destination.y;
    if (x < 0 || y < 0 || x >= destination.w || y >= destination.h) return false;
    int px = SDL_clamp((int)(x * s->width / destination.w), 0, s->width - 1);
    int py = s->height - 1 - SDL_clamp(
        (int)(y * s->height / destination.h), 0, s->height - 1);
    return s->pixels[(size_t)py * s->width + px] > 8;
}

void bongo_cat_windows_snapshot_pointer(BongoCatWindowsSnapshot *s) {
    POINT point;
    if (!s || !GetCursorPos(&point) || !ScreenToClient(s->source_handle, &point)) return;
    bool hit = bongo_cat_windows_snapshot_hit(s, (float)point.x, (float)point.y);
    LONG_PTR style = GetWindowLongPtrW(s->handle, GWL_EXSTYLE);
    LONG_PTR next = hit ? style & ~WS_EX_TRANSPARENT : style | WS_EX_TRANSPARENT;
    if (next != style) SetWindowLongPtrW(s->handle, GWL_EXSTYLE, next);
}

static LRESULT CALLBACK snapshot_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    BongoCatWindowsSnapshot *s = GetPropW(window, snapshot_property);
    if (!s) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_NCHITTEST) {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(s->source_handle, &point);
        return bongo_cat_windows_snapshot_hit(s, (float)point.x, (float)point.y)
            ? HTCLIENT : HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        /* Wheel coordinates are already screen-relative. Other mouse messages
           must look as though SDL received them on the real pet window. */
        if (message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL) {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            MapWindowPoints(window, s->source_handle, &point, 1);
            lparam = MAKELPARAM(point.x, point.y);
        }
        return SendMessageW(s->source_handle, message, wparam, lparam);
    }
    return CallWindowProcW(s->original, window, message, wparam, lparam);
}

bool bongo_cat_windows_snapshot_bind_input(BongoCatWindowsSnapshot *s) {
    if (!SetPropW(s->handle, snapshot_property, s)) return false;
    s->original = (WNDPROC)GetWindowLongPtrW(s->handle, GWLP_WNDPROC);
    return SetWindowLongPtrW(s->handle, GWLP_WNDPROC, (LONG_PTR)snapshot_proc) != 0;
}

void bongo_cat_windows_snapshot_unbind_input(BongoCatWindowsSnapshot *s) {
    if (s->original)
        SetWindowLongPtrW(s->handle, GWLP_WNDPROC, (LONG_PTR)s->original);
    RemovePropW(s->handle, snapshot_property);
}
