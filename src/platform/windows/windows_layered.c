#include "windows_layered.h"
#include "windows_layered_internal.h"
#include "windows_capture.h"
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_properties.h>
#include <stb_image_resize2.h>
#include <stdlib.h>
#include <windows.h>
static const wchar_t proxy_class[] = L"BongoCat.LayeredPresenter";
static const wchar_t proxy_property[] = L"BongoCat.LayeredProxy";
/* SDL's OpenGL window owns a DC, so a separate layered window presents frames. */
static HWND native_window(BongoCatPlatform *platform) {
    return platform && platform->window ? (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
}
void *bongo_cat_windows_layered_create(void) {
    BongoCatWindowsLayered *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    value->memory_dc = CreateCompatibleDC(NULL);
    if (value->memory_dc) return value;
    free(value);
    return NULL;
}

static void release_bitmap(BongoCatWindowsLayered *value) {
    if (!value || !value->bitmap) return;
    SelectObject(value->memory_dc, value->original_bitmap);
    DeleteObject(value->bitmap);
    value->bitmap = NULL;
    value->original_bitmap = NULL;
    value->pixels = NULL;
    value->width = value->height = 0;
}
static void restore_source(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source || !value->source_transparent) return;
    LONG_PTR style = GetWindowLongPtrW(source, GWL_EXSTYLE);
    SetWindowLongPtrW(source, GWL_EXSTYLE,
        style & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
    SetWindowPos(source, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    value->source_transparent = false;
    SDL_SetWindowOpacity(platform->window, platform->window_opacity);
    bongo_cat_windows_capture_restore_transparency(source);
    bongo_cat_windows_capture_log(source, "layered-source-restored");
}
void bongo_cat_windows_layered_destroy(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value) return;
    HWND source = native_window(platform);
    restore_source(platform);
    if (source) RemovePropW(source, proxy_property);
    if (value->proxy) DestroyWindow(value->proxy);
    release_bitmap(value);
    free(value->readback);
    if (value->memory_dc) DeleteDC(value->memory_dc);
    free(value);
    platform->presenter = NULL;
}
static bool register_proxy_class(void) {
    WNDCLASSEXW existing = {.cbSize = sizeof(existing)};
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (GetClassInfoExW(instance, proxy_class, &existing)) return true;
    WNDCLASSEXW type = {0};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    type.lpszClassName = proxy_class;
    return RegisterClassExW(&type) != 0;
}
static bool ensure_proxy(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source) return false;
    if (value->proxy) return true;
    if (!register_proxy_class()) return false;
    wchar_t title[128] = L"BongoCat - Pet";
    GetWindowTextW(source, title, (int)(sizeof(title) / sizeof(title[0])));
    DWORD style = WS_EX_LAYERED | WS_EX_TRANSPARENT |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (value->topmost) style |= WS_EX_TOPMOST;
    value->proxy = CreateWindowExW(style, proxy_class, title, WS_POPUP,
        0, 0, 1, 1, source, NULL, GetModuleHandleW(NULL), NULL);
    if (!value->proxy) return false;
    HICON large = (HICON)SendMessageW(source, WM_GETICON, ICON_BIG, 0);
    HICON small_icon = (HICON)SendMessageW(source, WM_GETICON, ICON_SMALL, 0);
    if (large) SendMessageW(value->proxy, WM_SETICON, ICON_BIG, (LPARAM)large);
    if (small_icon)
        SendMessageW(value->proxy, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    SetPropW(source, proxy_property, value->proxy);
    bongo_cat_windows_capture_log(value->proxy, "layered-proxy-created");
    return true;
}
bool bongo_cat_windows_layered_update_proxy(BongoCatPlatform *platform,
    BongoCatWindowsLayered *value) {
    HWND source_window = native_window(platform);
    RECT bounds;
    if (!source_window || !value || !value->proxy || !value->bitmap ||
        !GetWindowRect(source_window, &bounds))
        return SDL_SetError("Cannot locate the Windows layered window");
    POINT destination = {bounds.left, bounds.top};
    POINT source = {0, 0};
    SIZE size = {value->width, value->height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0,
        (BYTE)(platform->window_opacity * 255.0f + 0.5f), AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    bool presented = screen && UpdateLayeredWindow(value->proxy, screen,
        &destination, &size, value->memory_dc, &source, 0, &blend,
        ULW_ALPHA) != FALSE;
    DWORD failure = presented ? ERROR_SUCCESS : GetLastError();
    if (screen) ReleaseDC(NULL, screen);
    if (!presented) return SDL_SetError(
        "UpdateLayeredWindow failed (%lu)", (unsigned long)failure);
    return true;
}

static bool make_source_transparent(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source) return false;
    if (value->source_transparent) return true;
    LONG_PTR style = GetWindowLongPtrW(source, GWL_EXSTYLE);
    SetWindowLongPtrW(source, GWL_EXSTYLE,
        style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    if (!SetLayeredWindowAttributes(source, 0, 0, LWA_ALPHA)) {
        SetWindowLongPtrW(source, GWL_EXSTYLE, style);
        return false;
    }
    SetWindowPos(source, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    value->source_transparent = true;
    bongo_cat_windows_capture_log(source, "layered-source-suppressed");
    return true;
}

void bongo_cat_windows_layered_set_click_through(
    BongoCatPlatform *platform, bool enabled) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value || (value->mode_logged && value->active == enabled)) return;
    value->mode_logged = true;
    value->active = enabled;
    SDL_Log("Windows presentation path: forced_click_through=%d", enabled);
    if (enabled) {
        value->has_frame = false;
        ensure_proxy(platform);
        if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        bongo_cat_windows_capture_log(native_window(platform),
            "layered-presenter-ready");
    } else {
        if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        restore_source(platform);
        bongo_cat_windows_capture_log(native_window(platform),
            "direct-opengl-presenter");
    }
}

void bongo_cat_windows_layered_set_always_on_top(
    BongoCatPlatform *platform, bool enabled) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value) return;
    value->topmost = enabled;
    if (!value->proxy) return;
    SetWindowPos(value->proxy, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

HWND bongo_cat_windows_layered_proxy(HWND source) {
    return source ? (HWND)GetPropW(source, proxy_property) : NULL;
}

static bool resize_bitmap(BongoCatWindowsLayered *value, int width, int height) {
    if (value->bitmap && value->width == width && value->height == height) return true;
    release_bitmap(value);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = NULL;
    value->bitmap = CreateDIBSection(value->memory_dc, &info,
        DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!value->bitmap || !pixels) {
        if (value->bitmap) DeleteObject(value->bitmap);
        value->bitmap = NULL;
        return false;
    }
    value->original_bitmap = SelectObject(value->memory_dc, value->bitmap);
    value->pixels = pixels;
    value->width = width;
    value->height = height;
    return true;
}

static bool read_frame(BongoCatWindowsLayered *value, int width, int height,
    int display_width, int display_height) {
    value->source_width = width; value->source_height = height;
    value->readback_valid = width != display_width || height != display_height;
    if (!value->readback_valid) {
        glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, value->pixels);
        return true;
    }
    size_t bytes = (size_t)width * (size_t)height * 4;
    if (width <= 0 || height <= 0 ||
        bytes / 4 != (size_t)width * (size_t)height) return false;
    if (value->readback_capacity < bytes) {
        unsigned char *next = realloc(value->readback, bytes);
        if (!next) return false;
        value->readback = next;
        value->readback_capacity = bytes;
    }
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, value->readback);
    return stbir_resize_uint8_linear(value->readback, width, height, width * 4,
        value->pixels, display_width, display_height, display_width * 4,
        STBIR_BGRA_PM) != NULL;
}

bool bongo_cat_platform_present(BongoCatPlatform *platform, int width, int height) {
    if (!platform || !platform->window) return false;
    BongoCatWindowsLayered *value = platform->presenter;
    if (!value || !value->active) return SDL_GL_SwapWindow(platform->window);
    HWND source = native_window(platform);
    if (!source || !value->visible || !IsWindowVisible(source) || IsIconic(source)) {
        if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        /* A hidden source has no frame to expose through the proxy. Let the
           startup path show it before retrying the first presentation. */
        return false;
    }
    RECT bounds;
    if (!GetWindowRect(source, &bounds)) return false;
    int display_width = bounds.right - bounds.left,
        display_height = bounds.bottom - bounds.top;
    if (!ensure_proxy(platform) || width <= 0 || height <= 0 ||
        display_width <= 0 || display_height <= 0 ||
        !resize_bitmap(value, display_width, display_height) ||
        !read_frame(value, width, height, display_width, display_height)) {
        value->has_frame = false;
        return SDL_SetError("Cannot allocate the Windows layered frame");
    }
    if (!bongo_cat_windows_layered_update_proxy(platform, value)) {
        value->has_frame = false;
        return false;
    }
    value->has_frame = true;
    if (!make_source_transparent(platform))
        return SDL_SetError("Cannot suppress the OpenGL source window");
    ShowWindow(value->proxy, SW_SHOWNOACTIVATE);
    return true;
}
#endif
