#include "windows_capture.h"
#include "windows_borderless.h"
#include "windows_diagnostics.h"
#include "windows_direct_input.h"
#include "windows_input.h"
#include "windows_package.h"
#include "windows_popup.h"
#include "windows_tray.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static int failures;
static int window_position_messages;
static int nonclient_size_messages;

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", \
        __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

static void test_package_storage_root(void) {
    char path[BONGO_CAT_PATH_CAP];
    CHECK(bongo_cat_windows_package_storage_root_for(
        L"C:\\Users\\Alice", L"publisher.BongoCat_abcd1234",
        path, sizeof(path)));
    CHECK(strcmp(path, "C:\\Users\\Alice\\AppData\\Local\\Packages\\"
        "publisher.BongoCat_abcd1234\\LocalCache\\Local\\BongoCat") == 0);
    CHECK(!bongo_cat_windows_package_storage_root_for(
        L"C:\\Users\\Alice", L"publisher.BongoCat_abcd1234", path, 8));
    CHECK(path[0] == '\0');
    CHECK(!bongo_cat_windows_package_storage_root_for(
        L"C:\\Users\\Alice", L"", path, sizeof(path)));
}

static LRESULT CALLBACK click_through_test_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGED) window_position_messages++;
    if (message == WM_NCCALCSIZE) nonclient_size_messages++;
    return DefWindowProcW(window, message, wparam, lparam);
}

static void test_click_through_does_not_refresh_frame(void) {
    CHECK(!bongo_cat_windows_borderless_hit_transparent(false, false, false));
    CHECK(bongo_cat_windows_borderless_hit_transparent(false, true, false));
    CHECK(!bongo_cat_windows_borderless_hit_transparent(false, true, true));
    CHECK(bongo_cat_windows_borderless_hit_transparent(true, false, true));
    const wchar_t class_name[] = L"BongoCat click-through test";
    WNDCLASSW type = {.lpfnWndProc = click_through_test_proc,
        .hInstance = GetModuleHandleW(NULL), .lpszClassName = class_name};
    ATOM registered = RegisterClassW(&type);
    CHECK(registered != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    HWND window = CreateWindowExW(WS_EX_LAYERED, class_name,
        L"BongoCat click-through test", WS_POPUP, 0, 0, 32, 32,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (!window) return;
    bongo_cat_windows_borderless_install(window);
    window_position_messages = 0;
    nonclient_size_messages = 0;
    bongo_cat_windows_borderless_set_click_through(window, false, true);
    CHECK((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0);
    CHECK(SendMessageW(window, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
    SendMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    CHECK(GetCapture() == window);
    SendMessageW(window, WM_RBUTTONUP, 0, 0);
    CHECK(GetCapture() != window);
    bongo_cat_windows_borderless_set_click_through(window, true, false);
    CHECK((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0);
    CHECK(SendMessageW(window, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
    bongo_cat_windows_borderless_set_click_through(window, false, false);
    CHECK((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0);
    CHECK(window_position_messages == 0);
    CHECK(nonclient_size_messages == 0);
    bongo_cat_windows_borderless_uninstall(window);
    DestroyWindow(window);
    if (registered) UnregisterClassW(class_name, GetModuleHandleW(NULL));
}

static void test_popup_completion(void) {
    HWND window = CreateWindowExW(0, L"STATIC", L"BongoCat popup test",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (!window) return;
    bongo_cat_windows_popup_complete(window);
    MSG message = {0};
    bool found = false;
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
        if (message.message == WM_NULL) found = true;
    CHECK(found);
    DestroyWindow(window);
}

static void test_input_startup(void) {
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_HOOK_FAILURE");
    SDL_setenv_unsafe("BONGO_CAT_TEST_HOOK_DELAY_MS", "2000", 1);
    BongoCatInputState input;
    bongo_cat_input_init(&input);
    BongoCatPlatform platform = {.input = &input};
    ULONGLONG started = GetTickCount64();
    CHECK(bongo_cat_windows_input_start(&platform));
    CHECK(GetTickCount64() - started < 500);
    bongo_cat_windows_input_stop(&platform);
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_HOOK_DELAY_MS");
    SDL_setenv_unsafe("BONGO_CAT_TEST_HOOK_FAILURE", "1", 1);
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_HOOK_FAILURE");
}

static void test_direct_input_rebase(void) {
    HWND window = CreateWindowExW(0, L"STATIC", L"BongoCat input test",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (!window) return;
    BongoCatPlatform platform = {0};
    if (bongo_cat_windows_direct_input_create(&platform, window)) {
        double x = 0.0, y = 0.0;
        CHECK(!bongo_cat_platform_relative_pointer(&platform, &x, &y));
        bongo_cat_windows_direct_input_reset(&platform);
        CHECK(!bongo_cat_platform_relative_pointer(&platform, &x, &y));
        bongo_cat_windows_direct_input_destroy(&platform);
        CHECK(platform.relative_pointer == NULL);
    }
    DestroyWindow(window);
}

static void count_tray_restore(void *userdata) {
    int *count = userdata;
    (*count)++;
}

static void test_tray_restart_notification(void) {
    int restores = 0;
    void *tray = &restores;
    bongo_cat_platform_set_tray_callbacks(
        tray, NULL, NULL, count_tray_restore, &restores);
    bongo_cat_windows_tray_handle_message(WM_NULL);
    CHECK(restores == 0);
    UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    CHECK(taskbar_created != 0);
    bongo_cat_windows_tray_handle_message(taskbar_created);
    CHECK(restores == 1);
    bongo_cat_platform_set_tray_callbacks(tray, NULL, NULL, NULL, NULL);
    bongo_cat_windows_tray_handle_message(taskbar_created);
    CHECK(restores == 1);
}

static void test_capture_styles(void) {
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED |
        WS_EX_TRANSPARENT, L"STATIC",
        L"BongoCat capture test", WS_POPUP, 0, 0, 32, 32,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (window) {
        CHECK(bongo_cat_windows_diagnostics_probe_capture(window, "unit-test"));
        CHECK(bongo_cat_windows_capture_configure(window));
        LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        CHECK((style & WS_EX_APPWINDOW) == 0);
        CHECK((style & WS_EX_TOOLWINDOW) == 0);
        CHECK((style & WS_EX_NOREDIRECTIONBITMAP) == 0);
        CHECK((style & WS_EX_LAYERED) != 0);
        CHECK((style & WS_EX_TRANSPARENT) != 0);
        CHECK(bongo_cat_windows_capture_configure(window));
        CHECK(GetWindowLongPtrW(window, GWL_EXSTYLE) == style);
        CHECK(!bongo_cat_windows_capture_handle_message(window, WM_NULL, 0));
        CHECK(!bongo_cat_windows_capture_handle_message(window, WM_TIMER, 1));
        DestroyWindow(window);
    }
    window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC",
        L"BongoCat redirection test", WS_POPUP,
        0, 0, 32, 32, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (window) {
        CHECK(!bongo_cat_windows_capture_configure(window));
        LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        CHECK((style & WS_EX_TOOLWINDOW) == 0);
        CHECK((style & WS_EX_NOREDIRECTIONBITMAP) != 0);
        DestroyWindow(window);
    }
    window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, L"STATIC",
        L"BongoCat capture app-window test", WS_POPUP, 0, 0, 32, 32,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (window) {
        LONG_PTR before = GetWindowLongPtrW(window, GWL_EXSTYLE);
        CHECK(bongo_cat_windows_capture_configure(window));
        LONG_PTR after = GetWindowLongPtrW(window, GWL_EXSTYLE);
        CHECK((after & WS_EX_APPWINDOW) != 0);
        CHECK((after & WS_EX_LAYERED) != 0);
        CHECK(after == before);
        DestroyWindow(window);
    }
}

static void test_transparency_lifecycle(void) {
    HWND window = CreateWindowExW(0, L"STATIC", L"BongoCat transparency test",
        WS_POPUP, 0, 0, 32, 32, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (!window) return;
    CHECK(!bongo_cat_windows_capture_is_configured(window));
    bongo_cat_windows_capture_mark_transparent(window, true);
    CHECK(bongo_cat_windows_capture_handle_transparency_message(
        window, WM_ERASEBKGND, 0));
    bongo_cat_windows_capture_install_transparency_handler(window);
    CHECK(SendMessageW(window, WM_ERASEBKGND, 0, 0) == 0);
    CHECK(bongo_cat_windows_capture_configure(window));
    CHECK(bongo_cat_windows_capture_is_configured(window));
    bongo_cat_windows_capture_mark_transparent(window, false);
    CHECK(!bongo_cat_windows_capture_handle_transparency_message(
        window, WM_ERASEBKGND, 0));
    DestroyWindow(window);
}

int main(void) {
    test_package_storage_root();
    test_click_through_does_not_refresh_frame();
    test_capture_styles();
    test_transparency_lifecycle();
    test_tray_restart_notification();
    test_popup_completion();
    test_input_startup();
    test_direct_input_rebase();
    return failures ? 1 : 0;
}
