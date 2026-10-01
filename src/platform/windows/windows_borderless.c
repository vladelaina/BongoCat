#include "windows_borderless.h"
#include "windows_capture.h"
#include "windows_keys.h"
#include "windows_tray.h"

#ifdef _WIN32
#include <stdlib.h>

static const wchar_t original_proc_property[] = L"BongoCat.BorderlessWindowProc";
static const wchar_t click_through_property[] = L"BongoCat.ClickThrough";
static const wchar_t menu_binding_property[] = L"BongoCat.MenuBinding";
static const wchar_t drag_binding_property[] = L"BongoCat.DragBinding";
static const wchar_t preserve_screen_property[] = L"BongoCat.PreserveScreenPixels";
#define BONGO_CAT_MENU_PREVIEW_TIMER ((UINT_PTR)0xBC4E)
#define BONGO_CAT_DRAG_MODAL_TIMER ((UINT_PTR)0xBC50)
#define BONGO_CAT_DRAG_FRAME_INTERVAL_MS 16
#define BONGO_CAT_CLICK_THROUGH_FORCED ((INT_PTR)1)
#define BONGO_CAT_CLICK_THROUGH_TRANSPARENT_PIXEL ((INT_PTR)2)

bool bongo_cat_windows_borderless_hit_transparent(bool forced,
    bool pointer_transparent, bool right_button_down) {
    return forced || (pointer_transparent && !right_button_down);
}

typedef struct WindowsMenuBinding {
    BongoCatMenuPreview preview;
    void (*tick)(void *userdata);
    void *userdata;
} WindowsMenuBinding;

typedef struct WindowsDragBinding {
    BongoCatModalTick tick;
    void *userdata;
    ULONGLONG last_tick_ms;
    bool ticking;
} WindowsDragBinding;

static void drag_tick(WindowsDragBinding *drag) {
    if (!drag || !drag->tick || drag->ticking) return;
    ULONGLONG now = GetTickCount64();
    if (drag->last_tick_ms && now - drag->last_tick_ms <
        BONGO_CAT_DRAG_FRAME_INTERVAL_MS) return;
    drag->last_tick_ms = now;
    drag->ticking = true;
    drag->tick(drag->userdata);
    drag->ticking = false;
}

static LONG_PTR borderless_style(LONG_PTR style) {
    return (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
}

static LRESULT CALLBACK borderless_window_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    bongo_cat_windows_tray_handle_message(message);
    WNDPROC original = (WNDPROC)GetPropW(window, original_proc_property);
    WindowsMenuBinding *menu = GetPropW(window, menu_binding_property);
    WindowsDragBinding *drag = GetPropW(window, drag_binding_property);
    if (message == WM_NCCALCSIZE && wparam && lparam &&
        GetPropW(window, preserve_screen_property)) {
        NCCALCSIZE_PARAMS *sizes = (NCCALCSIZE_PARAMS *)lparam;
        RECT previous_client = sizes->rgrc[2];
        /* SDL must still calculate the new client rectangle and update its
           border bookkeeping. Its redraw/alignment flags are replaced only
           for our synchronous move-and-resize transaction. */
        CallWindowProcW(original ? original : DefWindowProcW,
            window, message, wparam, lparam);
        RECT overlap;
        if (IntersectRect(&overlap, &previous_client, &sizes->rgrc[0])) {
            /* Both rectangles use screen coordinates. Equal source and
               destination preserve the old frame in place, rather than
               shifting it with the new window origin before the GL swap. */
            sizes->rgrc[1] = overlap;
            sizes->rgrc[2] = overlap;
            return WVR_VALIDRECTS;
        }
        return WVR_REDRAW;
    }
    if (bongo_cat_windows_capture_handle_message(window, message, wparam)) {
        return 0;
    } else if (message == WM_NCHITTEST) {
        INT_PTR click_through = (INT_PTR)GetPropW(
            window, click_through_property);
        if (bongo_cat_windows_borderless_hit_transparent(
                click_through == BONGO_CAT_CLICK_THROUGH_FORCED,
                click_through == BONGO_CAT_CLICK_THROUGH_TRANSPARENT_PIXEL,
                bongo_cat_windows_right_button_down() ||
                    GetCapture() == window)) return HTTRANSPARENT;
    } else if (message == WM_RBUTTONDOWN &&
        (INT_PTR)GetPropW(window, click_through_property) ==
            BONGO_CAT_CLICK_THROUGH_TRANSPARENT_PIXEL) {
        SetCapture(window);
    } else if (message == WM_SYSCOMMAND &&
        (wparam & 0xFFF0u) == SC_MINIMIZE) {
        return 0;
    } else if (message == WM_STYLECHANGING && wparam == (WPARAM)GWL_STYLE && lparam) {
        STYLESTRUCT *styles = (STYLESTRUCT *)lparam;
        styles->styleNew = (DWORD)borderless_style(styles->styleNew);
    } else if (message == WM_STYLECHANGED && wparam == (WPARAM)GWL_STYLE) {
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        LONG_PTR fixed = borderless_style(style);
        if (fixed != style) SetWindowLongPtrW(window, GWL_STYLE, fixed);
    } else if (message == WM_NCPAINT) {
        return 0;
    } else if (message == WM_NCACTIVATE) {
        return TRUE;
    } else if (message == WM_MOUSEACTIVATE) {
        return MA_ACTIVATE;
    } else if (message == WM_MENUSELECT && menu && menu->preview) {
        UINT id = LOWORD(wparam), flags = HIWORD(wparam);
        if (flags == 0xffff && !lparam) return CallWindowProcW(
            original ? original : DefWindowProcW, window, message, wparam, lparam);
        if ((flags & (MF_POPUP | MF_SEPARATOR)) || !id) {
            menu->preview(menu->userdata, BONGO_CAT_MENU_NONE);
        } else {
            BongoCatMenuAction action = (BongoCatMenuAction)id;
            menu->preview(menu->userdata, action);
        }
    } else if (message == WM_TIMER &&
        wparam == BONGO_CAT_MENU_PREVIEW_TIMER && menu && menu->tick) {
        menu->tick(menu->userdata);
        return 0;
    } else if (message == WM_TIMER &&
        wparam == BONGO_CAT_DRAG_MODAL_TIMER && drag && drag->tick) {
        drag_tick(drag);
        return 0;
    }
    LRESULT result = CallWindowProcW(original ? original : DefWindowProcW,
        window, message, wparam, lparam);
    if (message == WM_RBUTTONUP && GetCapture() == window)
        ReleaseCapture();
    /* Move messages keep frames flowing when the low-priority timer is starved. */
    if (drag && (message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE))
        drag_tick(drag);
    return result;
}

void bongo_cat_windows_begin_drag(HWND window, BongoCatModalTick modal_tick,
    void *userdata) {
    if (!window) return;
    WindowsDragBinding binding = {modal_tick, userdata};
    bool bound = modal_tick && SetPropW(window, drag_binding_property, &binding);
    if (bound) {
        drag_tick(&binding);
        SetTimer(window, BONGO_CAT_DRAG_MODAL_TIMER,
            BONGO_CAT_DRAG_FRAME_INTERVAL_MS, NULL);
    }
    ReleaseCapture();
    SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    if (bound) {
        KillTimer(window, BONGO_CAT_DRAG_MODAL_TIMER);
        RemovePropW(window, drag_binding_property);
        modal_tick(userdata);
    }
}

static void clear_menu_binding(HWND window) {
    WindowsMenuBinding *menu = window ?
        GetPropW(window, menu_binding_property) : NULL;
    if (!menu) return;
    KillTimer(window, BONGO_CAT_MENU_PREVIEW_TIMER);
    RemovePropW(window, menu_binding_property);
    free(menu);
}

void bongo_cat_windows_menu_preview(HWND window, BongoCatMenuPreview preview,
    void (*tick)(void *userdata), void *userdata) {
    if (!window) return;
    clear_menu_binding(window);
    if (!preview && !tick) return;
    WindowsMenuBinding *menu = calloc(1, sizeof(*menu));
    if (!menu) return;
    menu->preview = preview;
    menu->tick = tick;
    menu->userdata = userdata;
    if (!SetPropW(window, menu_binding_property, menu)) {
        free(menu);
        return;
    }
    if (tick) SetTimer(window, BONGO_CAT_MENU_PREVIEW_TIMER, 16, NULL);
}

void bongo_cat_windows_borderless_install(HWND window) {
    if (!window || GetPropW(window, original_proc_property)) return;
    LONG_PTR style = borderless_style(GetWindowLongPtrW(window, GWL_STYLE));
    SetWindowLongPtrW(window, GWL_STYLE, style);
    WNDPROC original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (!original || !SetPropW(window, original_proc_property, (HANDLE)original)) return;
    SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)borderless_window_proc);
}

void bongo_cat_windows_borderless_uninstall(HWND window) {
    if (!window) return;
    clear_menu_binding(window);
    KillTimer(window, BONGO_CAT_DRAG_MODAL_TIMER);
    RemovePropW(window, drag_binding_property);
    WNDPROC original = (WNDPROC)GetPropW(window, original_proc_property);
    if (!original) return;
    SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)original);
    RemovePropW(window, original_proc_property);
    RemovePropW(window, click_through_property);
    RemovePropW(window, preserve_screen_property);
}

bool bongo_cat_windows_borderless_preserve_screen(HWND window, bool enabled) {
    if (!window) return false;
    if (!enabled) {
        RemovePropW(window, preserve_screen_property);
        return true;
    }
    return GetPropW(window, original_proc_property) &&
        SetPropW(window, preserve_screen_property, (HANDLE)1);
}

void bongo_cat_windows_borderless_set_click_through(HWND window,
    bool forced, bool pointer_transparent) {
    if (!window) return;
    INT_PTR mode = forced ? BONGO_CAT_CLICK_THROUGH_FORCED :
        pointer_transparent ? BONGO_CAT_CLICK_THROUGH_TRANSPARENT_PIXEL : 0;
    if (mode) SetPropW(window, click_through_property, (HANDLE)mode);
    else RemovePropW(window, click_through_property);
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    LONG_PTR next = forced ? style | WS_EX_TRANSPARENT :
        style & ~WS_EX_TRANSPARENT;
    /* The hit-test property is immediate; a frame refresh flickers OpenGL windows. */
    if (next != style) SetWindowLongPtrW(window, GWL_EXSTYLE, next);
}
#endif
