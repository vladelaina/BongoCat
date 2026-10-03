#include "windows_layered.h"
#include "windows_layered_internal.h"
#include "windows_capture.h"
#include "windows_gl_readback.h"
#include "windows_hdr.h"
#include "bongo_cat/runtime_diagnostics.h"
#include "bongo_cat/log.h"
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_properties.h>
#include <stb_image_resize2.h>
#include <stdlib.h>
#include <limits.h>
#include <windows.h>
#include <shellapi.h>
static const wchar_t proxy_class[] = L"BongoCat.LayeredPresenter";
static const wchar_t proxy_property[] = L"BongoCat.LayeredProxy";
/* SDL's OpenGL window owns a DC, so a separate layered window presents frames. */
static HWND native_window(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (value && value->source_destroyed) return NULL;
    if (value && value->source) return value->source;
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
static bool restore_source(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source || !value->source_transparent) return true;
    value->source_transparent = false;
    LONG_PTR style = GetWindowLongPtrW(source, GWL_EXSTYLE);
    SetWindowLongPtrW(source, GWL_EXSTYLE,
        (style & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT)) |
        (value->source_style & (WS_EX_LAYERED | WS_EX_TRANSPARENT)));
    SetWindowPos(source, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    bool restored = SDL_SetWindowOpacity(platform->window, platform->window_opacity);
    if (!restored && (GetWindowLongPtrW(source, GWL_EXSTYLE) & WS_EX_LAYERED))
        restored = SetLayeredWindowAttributes(source, 0,
            (BYTE)(platform->window_opacity * 255.0f + 0.5f), LWA_ALPHA) != FALSE;
    if (!restored) {
        value->source_transparent = true;
        return false;
    }
    bongo_cat_windows_capture_restore_transparency(source);
    bongo_cat_windows_capture_log(source, "layered-source-restored");
    return true;
}
void bongo_cat_windows_layered_destroy(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value) return;
    HWND source = native_window(platform);
    restore_source(platform);
    bongo_cat_windows_layered_unbind(platform);
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
    type.lpfnWndProc = bongo_cat_windows_layered_window_proc;
    type.style = CS_DBLCLKS;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    type.lpszClassName = proxy_class;
    return RegisterClassExW(&type) != 0;
}
static bool ensure_proxy(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source) return false;
    value->source = source;
    if (value->proxy) return true;
    if (!register_proxy_class()) return false;
    wchar_t title[128] = L"BongoCat - Pet";
    GetWindowTextW(source, title, (int)(sizeof(title) / sizeof(title[0])));
    DWORD style = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (value->forced) style |= WS_EX_TRANSPARENT;
    value->topmost = (GetWindowLongPtrW(source, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (value->topmost) style |= WS_EX_TOPMOST;
    value->proxy = CreateWindowExW(style, proxy_class, title, WS_POPUP,
        0, 0, 1, 1, source, NULL, GetModuleHandleW(NULL), platform);
    if (!value->proxy) return false;
    if (!bongo_cat_windows_layered_bind(platform)) {
        DestroyWindow(value->proxy);
        value->proxy = NULL;
        return false;
    }
    DragAcceptFiles(value->proxy, TRUE);
    /* Match the source's legacy shell-drop support for elevated launches. */
    ChangeWindowMessageFilterEx(value->proxy, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(value->proxy, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(value->proxy, 0x0049, MSGFLT_ALLOW, NULL);
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
    const char *previous = bongo_cat_diagnostics_phase("layered-window-upload");
    bool presented = screen && UpdateLayeredWindow(value->proxy, screen,
        &destination, &size, value->memory_dc, &source, 0, &blend,
        ULW_ALPHA) != FALSE;
    DWORD failure = presented ? ERROR_SUCCESS : GetLastError();
    bongo_cat_diagnostics_phase(previous);
    if (screen) ReleaseDC(NULL, screen);
    if (!presented) return SDL_SetError(
        "UpdateLayeredWindow failed (%lu)", (unsigned long)failure);
    value->applied_alpha = blend.SourceConstantAlpha;
    value->applied_alpha_valid = true;
    return true;
}

static bool make_source_transparent(BongoCatPlatform *platform) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    HWND source = native_window(platform);
    if (!value || !source) return false;
    if (value->source_transparent) return true;
    LONG_PTR style = GetWindowLongPtrW(source, GWL_EXSTYLE);
    value->source_style = style;
    SetWindowLongPtrW(source, GWL_EXSTYLE,
        style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    if (!SetLayeredWindowAttributes(source, 0, 0, LWA_ALPHA)) {
        SetWindowLongPtrW(source, GWL_EXSTYLE, style);
        return false;
    }
    value->source_transparent = true;
    SetWindowPos(source, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    bongo_cat_windows_capture_log(source, "layered-source-suppressed");
    return true;
}

void bongo_cat_windows_layered_set_click_through(
    BongoCatPlatform *platform, bool enabled) {
    BongoCatWindowsLayered *value = platform ? platform->presenter : NULL;
    if (!value || (value->mode_logged && value->forced == enabled)) return;
    value->mode_logged = true;
    value->forced = enabled;
    value->source_style = enabled ? value->source_style | WS_EX_TRANSPARENT :
        value->source_style & ~WS_EX_TRANSPARENT;
    bool active = !value->hdr_failed && (enabled || bongo_cat_windows_hdr_enabled(platform->window));
    bongo_cat_windows_layered_sync_input(platform);
    if (value->active == active) return;
    value->active = active;
    SDL_Log("Windows presentation path: forced_click_through=%d", enabled);
    if (active) {
        value->has_frame = false;
        ensure_proxy(platform);
        if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        bongo_cat_windows_capture_log(native_window(platform),
            "layered-presenter-ready");
    } else {
        if (restore_source(platform) && value->proxy) ShowWindow(value->proxy, SW_HIDE);
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
    if (!value->original_bitmap || value->original_bitmap == HGDI_ERROR) {
        DeleteObject(value->bitmap);
        value->bitmap = NULL;
        value->original_bitmap = NULL;
        return SDL_SetError("Cannot select the Windows layered bitmap");
    }
    value->pixels = pixels;
    value->width = width;
    value->height = height;
    return true;
}

static bool read_frame(BongoCatWindowsLayered *value, int width, int height,
    int display_width, int display_height) {
    value->source_width = width; value->source_height = height;
    value->readback_valid = width != display_width || height != display_height;
    if (!value->readback_valid)
        return bongo_cat_windows_gl_readback(width, height, value->pixels);
    if (width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4))
        return SDL_SetError("Windows layered frame is too large");
    size_t bytes = (size_t)width * (size_t)height * 4;
    if (value->readback_capacity < bytes) {
        unsigned char *next = realloc(value->readback, bytes);
        if (!next) return false;
        value->readback = next;
        value->readback_capacity = bytes;
    }
    if (!bongo_cat_windows_gl_readback(width, height, value->readback)) return false;
    bool resized = stbir_resize_uint8_linear(value->readback, width, height, width * 4,
        value->pixels, display_width, display_height, display_width * 4,
        STBIR_BGRA_PM) != NULL;
    return resized || SDL_SetError("Cannot resize the Windows layered frame");
}

static bool present_layered(BongoCatPlatform *platform, int width, int height,
    bool *swapped) {
    BongoCatWindowsLayered *value = platform->presenter;
    HWND source = native_window(platform);
    if (!source || !IsWindowVisible(source) || IsIconic(source)) {
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
        display_width > INT_MAX / 4 ||
        (size_t)display_height > SIZE_MAX / ((size_t)display_width * 4) ||
        !resize_bitmap(value, display_width, display_height)) {
        value->has_frame = false;
        return SDL_SetError("Cannot allocate the Windows layered frame");
    }
    const char *previous = bongo_cat_diagnostics_phase("layered-gl-readback");
    bool read = read_frame(value, width, height, display_width, display_height);
    bongo_cat_diagnostics_phase(previous);
    if (!read) {
        value->has_frame = false;
        return false;
    }
    /* Keep WGL presentation alive, including GL_FRONT consumers. Never
       change the original window to WS_EX_NOREDIRECTIONBITMAP. */
    previous = bongo_cat_diagnostics_phase("layered-gl-swap");
    *swapped = SDL_GL_SwapWindow(platform->window);
    bongo_cat_diagnostics_phase(previous);
    if (!*swapped) return false;
    if (SDL_GetHintBoolean("BONGO_CAT_TEST_LAYERED_FAILURE", false))
        return SDL_SetError("Injected layered presentation failure");
    if (!bongo_cat_windows_layered_update_proxy(platform, value)) {
        value->has_frame = false;
        return false;
    }
    value->has_frame = true;
    if (!IsWindowVisible(value->proxy)) ShowWindow(value->proxy, SW_SHOWNOACTIVATE);
    if (!IsWindowVisible(value->proxy)) return SDL_SetError("Layered proxy is not visible");
    previous = bongo_cat_diagnostics_phase("layered-source-suppression");
    bool suppressed = make_source_transparent(platform);
    bongo_cat_diagnostics_phase(previous);
    if (!suppressed)
        return SDL_SetError("Cannot suppress the OpenGL source window");
    return true;
}

bool bongo_cat_platform_present(BongoCatPlatform *platform, int width, int height) {
    if (!platform || !platform->window) return false;
    BongoCatWindowsLayered *value = platform->presenter;
    bool hdr = bongo_cat_windows_hdr_enabled(platform->window);
    bool active = value && !value->hdr_failed && (value->forced || hdr);
    SDL_PropertiesID properties = SDL_GetWindowProperties(platform->window);
    Sint64 state = (hdr ? 1 : 0) | (active ? 2 : 0) |
        (value && value->forced ? 4 : 0) | (value && value->hdr_failed ? 8 : 0);
    if (SDL_GetNumberProperty(properties, "BongoCat.DiagnosticPresentation", -1) != state) {
        SDL_SetNumberProperty(properties, "BongoCat.DiagnosticPresentation", state);
        Sint64 count = SDL_GetNumberProperty(properties, "BongoCat.DiagnosticPresentationCount", 0);
        if (count < 16) {
            SDL_SetNumberProperty(properties, "BongoCat.DiagnosticPresentationCount", count + 1);
            SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[render] window=%u hdr=%d layered=%d forced=%d failed=%d size=%dx%d gl=%s",
            (unsigned)SDL_GetWindowID(platform->window), hdr, active,
            value && value->forced, value && value->hdr_failed, width, height,
            glGetString(GL_RENDERER) ? (const char *)glGetString(GL_RENDERER) : "unknown");
        }
    }
    if (!active) {
        if (value) {
            value->active = false;
            if (!restore_source(platform))
                return SDL_SetError("Cannot restore WGL window opacity");
            if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        }
        const char *previous = bongo_cat_diagnostics_phase("direct-gl-swap");
        bool swapped = SDL_GL_SwapWindow(platform->window);
        bongo_cat_diagnostics_phase(previous);
        return swapped;
    }
    if (!value->active) SDL_Log("Windows HDR/transparent presenter: layered alpha, forced=%d hdr=%d",
        value->forced, hdr);
    value->active = true;
    HWND source = native_window(platform);
    if (!source || !IsWindowVisible(source) || IsIconic(source)) {
        if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
        /* The caller reveals a hidden window and retries the same back
           buffer. Do not swap here: that would consume the first HDR frame. */
        return false;
    }
    bool swapped = false;
    if (present_layered(platform, width, height, &swapped)) return true;
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Layered presentation failed; restoring WGL: %s", SDL_GetError());
    value->active = false;
    value->has_frame = false;
    value->hdr_failed = true;
    if (!restore_source(platform))
        return SDL_SetError("Cannot restore WGL window opacity after layered failure");
    if (value->proxy) ShowWindow(value->proxy, SW_HIDE);
    /* The old source is restored even if allocation/upload/input setup fails. */
    return swapped || SDL_GL_SwapWindow(platform->window);
}
#endif
