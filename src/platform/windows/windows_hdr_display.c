#include "windows_hdr.h"
#include "bongo_cat/runtime_diagnostics.h"
#include <windows.h>

/* Read-only display query. SDL's cached per-window HDR state can refer to
   the monitor on which a short-lived menu was originally created. */
static int monitor_hdr(HMONITOR monitor) {
    MONITORINFOEXW info = {0};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) return -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 path_count = 0, mode_count = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
                &path_count, &mode_count) != ERROR_SUCCESS) return -1;
        DISPLAYCONFIG_PATH_INFO *paths = SDL_calloc(path_count, sizeof(*paths));
        DISPLAYCONFIG_MODE_INFO *modes = SDL_calloc(mode_count, sizeof(*modes));
        if (!paths || !modes) { SDL_free(paths); SDL_free(modes); return -1; }
        LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count,
            paths, &mode_count, modes, NULL);
        int result = -1;
        if (status == ERROR_SUCCESS) for (UINT32 i = 0; i < path_count; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {0};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = paths[i].sourceInfo.adapterId;
            source.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                SDL_wcscmp(source.viewGdiDeviceName, info.szDevice)) continue;
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color = {0};
            color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            color.header.size = sizeof(color);
            color.header.adapterId = paths[i].targetInfo.adapterId;
            color.header.id = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
                /* Advanced-color SDR also benefits from explicit alpha. For
                   cloned outputs use alpha if any matching target needs it. */
                if (color.advancedColorEnabled) { result = 1; break; }
                result = 0;
            }
        }
        SDL_free(paths);
        SDL_free(modes);
        if (status != ERROR_INSUFFICIENT_BUFFER) return result;
    }
    return -1;
}

int bongo_cat_windows_display_hdr(SDL_Window *window) {
    HWND handle = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    HMONITOR monitor = handle ? MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST) : NULL;
    if (!monitor) return -1;
    /* Shared by the pet and its UI on the main thread. Avoid querying display
       topology for every animated frame; moving monitors bypasses the cache. */
    static HMONITOR cached_monitor;
    static Uint64 expires;
    static int cached = -1;
    Uint64 now = SDL_GetTicks();
    if (monitor != cached_monitor || now >= expires) {
        const char *previous = bongo_cat_diagnostics_phase("hdr-monitor-query");
        cached = monitor_hdr(monitor);
        bongo_cat_diagnostics_phase(previous);
        cached_monitor = monitor;
        expires = now + 250;
    }
    return cached;
}
