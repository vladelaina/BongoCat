#include "test.h"
#include "bongo_cat/platform.h"
#include "bongo_cat/gl_api.h"
#include "windows_borderless.h"
#include "windows_layered.h"
#include "windows_hdr.h"
#include "ui_present.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <dwmapi.h>
#include <commctrl.h>

int bongo_cat_test_failures;
static LPARAM forwarded_mouse, forwarded_wheel;
static HWND native_window(SDL_Window *window);

static LRESULT CALLBACK input_probe(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    (void)id; (void)reference;
    if (message == WM_NCHITTEST) return HTBOTTOMRIGHT;
    if (message == WM_MOUSEMOVE) { forwarded_mouse = lparam; return 0; }
    if (message == WM_MOUSEWHEEL) { forwarded_wheel = lparam; return 0; }
    return DefSubclassProc(window, message, wparam, lparam);
}

static void check_proxy_input(SDL_Window *window, HWND proxy) {
    HWND source = native_window(window);
    CHECK(SetWindowSubclass(source, input_probe, 123, 0));
    CHECK(SendMessageW(proxy, WM_NCHITTEST, 0, 0) == HTBOTTOMRIGHT);
    forwarded_mouse = forwarded_wheel = 0;
    SendMessageW(proxy, WM_MOUSEMOVE, 0, MAKELPARAM(20, 30));
    CHECK(forwarded_mouse == MAKELPARAM(20, 30));
    SendMessageW(proxy, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(200, 300));
    CHECK(forwarded_wheel == MAKELPARAM(200, 300));
    CHECK(RemoveWindowSubclass(source, input_probe, 123));
}

static HWND native_window(SDL_Window *window) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

static void draw_marker(SDL_Window *window, bool green) {
    int width = 0, height = 0;
    CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Exercise the shader/texture/alpha path used by logos and covers.
       Clearing a colored rectangle cannot detect missing textured draws. */
    BongoCatGL gl;
    BongoCatError error = {0};
    if (!bongo_cat_gl_load(&gl, &error)) { CHECK(false); return; }
    const char *vertex = "#version 330 core\n"
        "out vec2 uv; void main(){vec2 p[4]=vec2[4](vec2(-.5,-.5),"
        "vec2(.5,-.5),vec2(-.5,.5),vec2(.5,.5));"
        "uv=p[gl_VertexID]+.5;gl_Position=vec4(p[gl_VertexID],0,1);}";
    const char *fragment = "#version 330 core\n"
        "in vec2 uv; uniform sampler2D Texture; out vec4 color;"
        "void main(){color=texture(Texture,uv);}";
    GLuint program = bongo_cat_gl_program(&gl, vertex, fragment, &error);
    CHECK(program != 0);
    if (!program) return;
    GLuint texture = 0, vao = 0;
    const unsigned char pixel[] = {green ? 0 : 255, green ? 255 : 0, 0, 192};
    gl.active_texture(GL_TEXTURE0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    gl.gen_vertex_arrays(1, &vao);
    gl.bind_vertex_array(vao);
    gl.use_program(program);
    gl.uniform_1i(gl.uniform_location(program, "Texture"), 0);
    glEnable(GL_BLEND);
    gl.blend_equation(GL_FUNC_ADD);
    gl.blend_func_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    unsigned char actual[4] = {0};
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    CHECK(actual[green ? 1 : 0] >= 188 && actual[3] >= 188 && actual[3] <= 196);
    CHECK(glGetError() == GL_NO_ERROR);
    gl.use_program(0);
    gl.delete_vertex_arrays(1, &vao);
    glDeleteTextures(1, &texture);
    gl.delete_program(program);
}

/* Inspect the desktop result, not just the framebuffer/API return value.
   Alternating colors prevents an invisible window matching the wallpaper. */
static void check_desktop_marker(SDL_Window *window, bool green) {
    RECT client;
    HWND handle = native_window(window);
    CHECK(GetClientRect(handle, &client));
    POINT point = {client.right / 2, client.bottom / 2};
    CHECK(ClientToScreen(handle, &point));
    bool visible = false;
    COLORREF pixel = CLR_INVALID;
    Uint64 deadline = SDL_GetTicks() + 1500;
    do {
        SDL_PumpEvents();
        DwmFlush();
        HDC desktop = GetDC(NULL);
        if (desktop) {
            pixel = GetPixel(desktop, point.x, point.y);
            ReleaseDC(NULL, desktop);
        }
        if (pixel != CLR_INVALID) {
            int dominant = green ? GetGValue(pixel) : GetRValue(pixel);
            int other = green ? GetRValue(pixel) : GetGValue(pixel);
            visible = dominant > other + 40 && dominant > GetBValue(pixel) + 40;
        }
        if (!visible) SDL_Delay(16);
    } while (!visible && SDL_GetTicks() < deadline);
    if (!visible) fprintf(stderr, "Desktop marker missing: title=%s green=%d pixel=0x%08lx\n",
        SDL_GetWindowTitle(window), green, (unsigned long)pixel);
    CHECK(visible);
    CHECK(!(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP));
}

static bool present_pet(BongoCatPlatform *platform) {
    int width, height;
    return SDL_GetWindowSizeInPixels(platform->window, &width, &height) &&
        bongo_cat_platform_present(platform, width, height);
}

static void check_transparent_corner(SDL_Window *window) {
    POINT corner = {8, 8};
    CHECK(ClientToScreen(native_window(window), &corner));
    DwmFlush();
    HDC desktop = GetDC(NULL);
    COLORREF pixel = desktop ? GetPixel(desktop, corner.x, corner.y) : CLR_INVALID;
    if (desktop) ReleaseDC(NULL, desktop);
    CHECK(pixel != CLR_INVALID && GetRValue(pixel) > 180 &&
        GetGValue(pixel) > 180 && GetBValue(pixel) > 180);
}

static void test_window(bool pet, bool transparent, bool menu) {
    /* A white backing window makes black/opaque corners observable. */
    HWND background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE, L"STATIC", L"Transparency test background",
        WS_POPUP | SS_WHITERECT, 0, 0, 420, 420, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(background != NULL);
    SDL_Window *window = SDL_CreateWindow(pet ? "Pet visibility regression" :
        "Settings/menu visibility regression", 160, 160,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_ALWAYS_ON_TOP | (menu ? SDL_WINDOW_UTILITY : 0) |
        (transparent ? SDL_WINDOW_TRANSPARENT : 0));
    CHECK(window != NULL);
    if (!window) { if (background) DestroyWindow(background); return; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    CHECK(context != NULL);
    if (!context) { SDL_DestroyWindow(window); if (background) DestroyWindow(background); return; }
    CHECK(SDL_GL_MakeCurrent(window, context));
    SDL_GL_SetSwapInterval(0);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(window);
    if (!pet) bongo_cat_windows_prepare_transparent_ui(window);
    RECT rect;
    GetWindowRect(native_window(window), &rect);
    if (background) {
        SetWindowPos(background, HWND_TOPMOST, rect.left - 60, rect.top - 60,
            420, 420, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(background);
    }
    BongoCatPlatform platform = {.window = window, .window_opacity = 1.0f};
    if (pet) {
        platform.presenter = bongo_cat_windows_layered_create();
        CHECK(platform.presenter != NULL);
        bongo_cat_windows_borderless_install(native_window(window));
    }
    /* The first successful frame must survive the hidden-to-shown transition. */
    draw_marker(window, false);
    bool first = pet ? present_pet(&platform) : bongo_cat_ui_present(window);
    CHECK(first || (pet && bongo_cat_windows_hdr_enabled(window)));
    if (pet) bongo_cat_platform_set_visible(&platform, true);
    else CHECK(SDL_ShowWindow(window));
    if (!first) CHECK(present_pet(&platform));
    /* UI initially paints hidden; the shown event supplies its HDR frame. */
    if (!pet) {
        draw_marker(window, false);
        CHECK(bongo_cat_ui_present(window));
    }
    check_desktop_marker(window, false);
    if (transparent) check_transparent_corner(window);
    for (int cycle = 0; cycle < 2; ++cycle) {
        if (pet) bongo_cat_platform_set_click_through(&platform, true, false);
        draw_marker(window, true);
        CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
        check_desktop_marker(window, true);
        if (pet) {
            CHECK(bongo_cat_platform_set_opacity(&platform, 0.5f));
            bongo_cat_platform_set_click_through(&platform, false, false);
            CHECK(bongo_cat_platform_set_opacity(&platform, 1.0f));
        }
        CHECK(SDL_SetWindowSize(window, 180 + cycle * 20, 180 + cycle * 20));
        SDL_SyncWindow(window);
        draw_marker(window, false);
        CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
        check_desktop_marker(window, false);
        if (transparent) check_transparent_corner(window);
        if (pet) {
            bongo_cat_platform_set_visible(&platform, false);
            bongo_cat_platform_set_visible(&platform, true);
        } else {
            CHECK(SDL_HideWindow(window));
            CHECK(SDL_ShowWindow(window));
        }
        draw_marker(window, true);
        CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
        check_desktop_marker(window, true);
    }
    /* Return from HDR to SDR and back without reopening the window. */
    for (int hdr = 0; hdr < 2; ++hdr) {
        CHECK(SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", hdr ? "1" : "0"));
        draw_marker(window, hdr != 0);
        CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
        check_desktop_marker(window, hdr != 0);
        HWND proxy = bongo_cat_windows_layered_proxy(native_window(window));
        CHECK((proxy && IsWindowVisible(proxy)) == (hdr && transparent));
        if (hdr && transparent) {
            CHECK(!(GetWindowLongPtrW(proxy, GWL_EXSTYLE) & WS_EX_TRANSPARENT));
            CHECK(bongo_cat_windows_layered_suppressed(native_window(window)));
            check_transparent_corner(window);
            check_proxy_input(window, proxy);
        }
    }
    /* A failed layered upload must unsuppress the source immediately. */
    CHECK(SDL_SetHint("BONGO_CAT_TEST_LAYERED_FAILURE", "1"));
    draw_marker(window, false);
    CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
    check_desktop_marker(window, false);
    CHECK(!bongo_cat_windows_layered_suppressed(native_window(window)));
    SDL_ResetHint("BONGO_CAT_TEST_LAYERED_FAILURE");
    draw_marker(window, true);
    CHECK(pet ? present_pet(&platform) : bongo_cat_ui_present(window));
    check_desktop_marker(window, true);
    if (pet) {
        bongo_cat_windows_layered_destroy(&platform);
        bongo_cat_windows_borderless_uninstall(native_window(window));
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    if (background) DestroyWindow(background);
}

static void test_active_ui_destroy(void) {
    CHECK(SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", "1"));
    for (int cycle = 0; cycle < 3; ++cycle) {
        SDL_Window *window = SDL_CreateWindow("HDR UI teardown regression", 160, 160,
            SDL_WINDOW_OPENGL | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS |
            SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN);
        CHECK(window != NULL);
        if (!window) return;
        SDL_GLContext context = SDL_GL_CreateContext(window);
        CHECK(context != NULL);
        if (!context) { SDL_DestroyWindow(window); return; }
        CHECK(SDL_GL_MakeCurrent(window, context));
        bongo_cat_windows_prepare_transparent_ui(window);
        CHECK(SDL_ShowWindow(window));
        draw_marker(window, true);
        CHECK(bongo_cat_ui_present(window));
        HWND proxy = bongo_cat_windows_layered_proxy(native_window(window));
        CHECK(proxy != NULL && IsWindowVisible(proxy));
        /* Match settings/menu cleanup: the GL context goes away before SDL
           releases its presenter property. No explicit presenter destroy. */
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        CHECK(!IsWindow(proxy));
        SDL_PumpEvents();
    }
}

int main(void) {
    /* Requires an unlocked, unobscured desktop. Run on both HDR and SDR
       desktops; no display setting is changed by the test. */
    if (!SDL_GetHintBoolean("BONGO_CAT_TEST_VISIBLE_PRESENTATION", false)) return 77;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Video initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    for (int hdr = 0; hdr < 2; ++hdr) {
        SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", hdr ? "1" : "0");
        test_window(true, true, false);
        SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", hdr ? "1" : "0");
        test_window(false, true, false);
        SDL_SetHint("BONGO_CAT_TEST_HDR_PRESENTATION", hdr ? "1" : "0");
        test_window(false, true, true);
        test_window(false, false, false);
    }
    test_active_ui_destroy();
    SDL_ResetHint("BONGO_CAT_TEST_HDR_PRESENTATION");
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
