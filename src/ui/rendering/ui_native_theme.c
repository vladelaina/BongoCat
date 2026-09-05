#include "ui_native_theme.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <dwmapi.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
typedef int (WINAPI *SetPreferredAppModeFn)(int);
typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
typedef void (WINAPI *FlushMenuThemesFn)(void);
typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);

static SetWindowThemeFn set_window_theme;
static SetPreferredAppModeFn set_preferred_mode;
static AllowDarkModeForWindowFn allow_dark_window;
static FlushMenuThemesFn flush_menu_themes;
static bool initialized;
static int applied_mode = -1;

static void load_named(HMODULE module, const char *name,
    void *target, size_t size) {
    FARPROC address = module ? GetProcAddress(module, name) : NULL;
    if (target && size == sizeof(address)) memcpy(target, &address, size);
}

static void load_ordinal(HMODULE module, WORD ordinal,
    void *target, size_t size) {
    FARPROC address = module ? GetProcAddress(module,
        MAKEINTRESOURCEA(ordinal)) : NULL;
    if (target && size == sizeof(address)) memcpy(target, &address, size);
}

static DWORD windows_build(void) {
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn version_fn = NULL;
    load_named(module, "RtlGetVersion", &version_fn, sizeof(version_fn));
    OSVERSIONINFOW version = {0}; version.dwOSVersionInfoSize = sizeof(version);
    return version_fn && version_fn(&version) == 0 && version.dwMajorVersion == 10
        ? version.dwBuildNumber : 0;
}

static bool high_contrast(void) {
    HIGHCONTRASTW contrast = {0}; contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
        &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

static HWND native_window(SDL_Window *window) {
    return window ? (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
}

static void initialize(void) {
    if (initialized) return;
    initialized = true;
    HMODULE theme = LoadLibraryW(L"uxtheme.dll");
    load_named(theme, "SetWindowTheme", &set_window_theme,
        sizeof(set_window_theme));
    if (windows_build() < 18362) return;
    load_ordinal(theme, 133, &allow_dark_window, sizeof(allow_dark_window));
    load_ordinal(theme, 135, &set_preferred_mode, sizeof(set_preferred_mode));
    load_ordinal(theme, 136, &flush_menu_themes, sizeof(flush_menu_themes));
}

void bongo_cat_ui_native_menu_prepare(SDL_Window *window, bool dark) {
    initialize();
    HWND handle = native_window(window);
    bongo_cat_ui_native_menu_prepare_native(handle, dark);
}

void bongo_cat_ui_native_menu_prepare_native(void *native_handle, bool dark) {
    initialize();
    HWND handle = (HWND)native_handle;
    bool contrast = high_contrast();
    int mode = contrast ? 0 : (dark ? 2 : 3);
    if (set_preferred_mode && mode != applied_mode) {
        set_preferred_mode(mode);
        applied_mode = mode;
        if (flush_menu_themes) flush_menu_themes();
    }
    if (handle && allow_dark_window) allow_dark_window(handle, !contrast);
    if (handle && set_window_theme)
        set_window_theme(handle, dark && !contrast ? L"DarkMode_Explorer" : NULL,
            NULL);
}

void bongo_cat_ui_native_theme_apply(SDL_Window *window, bool dark) {
    HWND handle = native_window(window);
    if (!handle) return;
    initialize();
    bool enabled = dark && !high_contrast();
    HRESULT result = DwmSetWindowAttribute(handle, 20, &enabled,
        sizeof(enabled));
    if (FAILED(result)) DwmSetWindowAttribute(handle, 19, &enabled,
        sizeof(enabled));
    bool transparent = (SDL_GetWindowFlags(window) &
        SDL_WINDOW_TRANSPARENT) != 0;
    int corner = transparent ? 1 : 2;
    DwmSetWindowAttribute(handle, 33, &corner, sizeof(corner));
    if (transparent) {
        DWORD border = 0xfffffffeu;
        DwmSetWindowAttribute(handle, 34, &border, sizeof(border));
    }
    if (set_window_theme) set_window_theme(handle,
        enabled ? L"DarkMode_Explorer" : NULL, NULL);
    bongo_cat_ui_native_menu_prepare(window, dark);
}
#else
void bongo_cat_ui_native_theme_apply(SDL_Window *window, bool dark) {
    (void)window; (void)dark;
}

void bongo_cat_ui_native_menu_prepare(SDL_Window *window, bool dark) {
    (void)window; (void)dark;
}

void bongo_cat_ui_native_menu_prepare_native(void *handle, bool dark) {
    (void)handle; (void)dark;
}
#endif
