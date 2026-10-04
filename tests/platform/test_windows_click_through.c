#include "test.h"
#include "bongo_cat/platform.h"
#include "windows_borderless.h"
#include "windows_layered.h"
#include "windows_layered_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <windows.h>

/* A different thread is essential: HTTRANSPARENT alone can pass a same-thread
   test while still swallowing input intended for a game or another process.
   Query native routing without moving the user's cursor or injecting clicks. */
typedef struct InputBackground {
    RECT bounds;
    HANDLE ready;
    HWND window;
} InputBackground;

static const wchar_t background_class[] = L"BongoCat foreign-thread input target";

static LRESULT CALLBACK background_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) return HTCLIENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

static DWORD WINAPI background_thread(void *userdata) {
    InputBackground *background = userdata;
    RECT r = background->bounds;
    background->window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
        WS_EX_TOPMOST, background_class, L"Native click-through target",
        WS_POPUP | WS_VISIBLE, r.left - 80, r.top - 80,
        r.right - r.left + 160, r.bottom - r.top + 160,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(background->ready);
    if (!background->window) return 1;
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

static void draw_bands(int width, int height, bool reverse) {
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    const unsigned char alpha[] = {0, 8, 9, 255};
    for (int band = 0; band < 4; ++band) {
        float a = alpha[reverse ? 3 - band : band] / 255.0f;
        int left = band * width / 4, right = (band + 1) * width / 4;
        glScissor(left, 0, right - left, height);
        glClearColor(a, a, a, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    /* An interior transparent hole must work as well as the outer margin. */
    int hole_x = (reverse ? 1 : 7) * width / 8;
    glScissor(hole_x - width / 32, height * 11 / 16, width / 16, height / 8);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

static void check_point(BongoCatPlatform *platform, HWND background,
    float x, float y, bool hit) {
    HWND source = SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    RECT client;
    CHECK(GetClientRect(source, &client));
    POINT point = {(LONG)(client.right * x), (LONG)(client.bottom * y)};
    CHECK(ClientToScreen(source, &point));
    HWND expected = hit ? bongo_cat_windows_layered_proxy(source) : background;
    CHECK(expected != NULL);
    CHECK(WindowFromPoint(point) == expected);
}

static void check_bands(BongoCatPlatform *platform, HWND background,
    bool reverse, bool forced) {
    int width = 0, height = 0;
    CHECK(SDL_GetWindowSizeInPixels(platform->window, &width, &height));
    for (int band = 0; band < 4; ++band) {
        bool visible = (reverse ? 3 - band : band) >= 2;
        float x = (band + 0.5f) / 4.0f;
        check_point(platform, background, x, 0.5f, visible && !forced);
        uint8_t alpha = 0;
        CHECK(bongo_cat_platform_frame_alpha(platform, width, height,
            (int)(width * x), height / 2, &alpha));
        CHECK((alpha > 8) == visible);
    }
    check_point(platform, background, reverse ? 0.125f : 0.875f, 0.25f, false);
}

void bongo_cat_test_windows_click_through(BongoCatPlatform *platform) {
    SDL_Window *window = platform->window;
    HWND source = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    HWND proxy = bongo_cat_windows_layered_proxy(source);
    CHECK(proxy != NULL);
    CHECK(bongo_cat_windows_layered_native_hit_test(platform));
    if (!proxy) return;
    WNDCLASSW type = {.lpfnWndProc = background_proc,
        .hInstance = GetModuleHandleW(NULL), .lpszClassName = background_class};
    ATOM registered = RegisterClassW(&type);
    CHECK(registered != 0);
    if (!registered) return;
    InputBackground background = {0};
    CHECK(GetWindowRect(source, &background.bounds));
    background.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE thread = background.ready ?
        CreateThread(NULL, 0, background_thread, &background, 0, NULL) : NULL;
    CHECK(thread != NULL);
    if (!thread) {
        if (background.ready) CloseHandle(background.ready);
        UnregisterClassW(background_class, GetModuleHandleW(NULL));
        return;
    }
    /* The containing interactive test has a process timeout. */
    CHECK(WaitForSingleObject(background.ready, INFINITE) == WAIT_OBJECT_0);
    CHECK(background.window != NULL);
    if (!background.window) goto cleanup;
    CHECK(GetWindowThreadProcessId(background.window, NULL) != GetCurrentThreadId());
    CHECK(SetWindowPos(source, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    CHECK(SetWindowPos(proxy, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    HWND foreground = GetForegroundWindow();
    int original_width, original_height, original_x, original_y;
    CHECK(SDL_GetWindowSize(window, &original_width, &original_height));
    CHECK(SDL_GetWindowPosition(window, &original_x, &original_y));
    for (int hdr = 0; hdr < 2; ++hdr) {
        CHECK(SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", hdr ? "1" : "0"));
        CHECK(SDL_SetWindowSize(window, original_width + hdr * 17,
            original_height + hdr * 23));
        CHECK(SDL_SetWindowPosition(window, original_x + hdr * 11, original_y - hdr * 7));
        CHECK(SDL_SyncWindow(window));
        for (int frame = 0; frame < 2; ++frame) {
            int width, height;
            CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
            draw_bands(width, height, frame != 0);
            CHECK(bongo_cat_platform_present(platform, width, height));
            CHECK(bongo_cat_windows_layered_proxy(source) == proxy);
            check_bands(platform, background.window, frame != 0, false);
            /* A stale runtime sample must never override the native shape. */
            bongo_cat_platform_set_click_through(platform, false, true);
            check_bands(platform, background.window, frame != 0, false);
            bongo_cat_platform_set_click_through(platform, true, false);
            check_bands(platform, background.window, frame != 0, true);
            bongo_cat_platform_set_click_through(platform, false, false);
            CHECK(bongo_cat_platform_set_opacity(platform, 0.5f));
            check_bands(platform, background.window, frame != 0, false);
            CHECK(bongo_cat_platform_set_opacity(platform, 1.0f));
            /* Frame updates must not release an active model drag. */
            SetCapture(source);
            CHECK(GetCapture() == source);
            draw_bands(width, height, frame != 0);
            CHECK(bongo_cat_platform_present(platform, width, height));
            CHECK(GetCapture() == source);
            ReleaseCapture();
            CHECK(GetForegroundWindow() == foreground);
        }
    }
    /* Exercise resampling to a different native size (the resize/DPI path). */
    int width, height;
    CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
    draw_bands(width / 2, height / 2, false);
    CHECK(bongo_cat_platform_present(platform, width / 2, height / 2));
    check_point(platform, background.window, 0.125f, 0.5f, false);
    check_point(platform, background.window, 0.875f, 0.5f, true);
    check_point(platform, background.window, 0.875f, 0.25f, false);
    uint8_t alpha = 255;
    CHECK(bongo_cat_platform_frame_alpha(platform, width / 2, height / 2,
        width / 2 * 7 / 8, height / 2 * 3 / 4, &alpha));
    CHECK(alpha == 0);
    BongoCatWindowsLayered *presenter = platform->presenter;
    CHECK(presenter->readback != NULL);
    size_t previous_capacity = presenter->readback_capacity;
    draw_bands(width / 4, height / 4, false);
    CHECK(bongo_cat_platform_present(platform, width / 4, height / 4));
    CHECK(presenter->readback_capacity < previous_capacity);
    /* Returning to the normal dimensions must retire the temporary full-frame
       copy after the resize grace period, while the native input shape works. */
    SDL_Delay(1001);
    draw_bands(width, height, false);
    CHECK(bongo_cat_platform_present(platform, width, height));
    CHECK(presenter->readback == NULL && presenter->readback_capacity == 0);
    check_bands(platform, background.window, false, false);
    CHECK(SDL_SetWindowSize(window, original_width, original_height));
    CHECK(SDL_SetWindowPosition(window, original_x, original_y));
    CHECK(SDL_SyncWindow(window));
cleanup:
    if (background.window) PostMessageW(background.window, WM_CLOSE, 0, 0);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(background.ready);
    CHECK(UnregisterClassW(background_class, GetModuleHandleW(NULL)));
}
