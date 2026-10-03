#include "runtime.h"
#include "test.h"
#include <SDL3/SDL_opengl.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

/* Inspect the input region installed in the X server. This works under XWayland
   without observing or moving the cursor, and does not need evdev permissions. */
static bool input_contains(Display *display, Window window, int x, int y) {
    int count = 0, ordering = 0;
    XRectangle *rectangles = XShapeGetRectangles(display, window,
        ShapeInput, &count, &ordering);
    bool hit = false;
    for (int i = 0; i < count; ++i) {
        XRectangle r = rectangles[i];
        if (x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height) hit = true;
    }
    if (rectangles) XFree(rectangles);
    return hit;
}

void bongo_cat_test_linux_click_through(BongoCatApp *app) {
    if (!(SDL_GetWindowFlags(app->window) & SDL_WINDOW_TRANSPARENT)) return;
    SDL_PropertiesID properties = SDL_GetWindowProperties(app->window);
    Display *display = SDL_GetPointerProperty(properties,
        SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    Window window = (Window)SDL_GetNumberProperty(properties,
        SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!display || !window) return; /* A native Wayland compositor owns its input region. */
    int event, error;
    if (!XFixesQueryExtension(display, &event, &error)) return;
    CHECK(SDL_GL_MakeCurrent(app->window, app->gl_context));
    int width, height, pixels_w, pixels_h;
    CHECK(SDL_GetWindowSize(app->window, &width, &height));
    CHECK(SDL_GetWindowSizeInPixels(app->window, &pixels_w, &pixels_h));
    Window focus;
    int revert;
    XGetInputFocus(display, &focus, &revert);
    for (int frame = 0; frame < 2; ++frame) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(frame ? pixels_w / 2 : 0, 0, pixels_w / 2, pixels_h);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        CHECK(bongo_cat_platform_present(&app->platform, pixels_w, pixels_h));
        XSync(display, False);
        CHECK(bongo_cat_platform_native_hit_test(&app->platform));
        CHECK(input_contains(display, window, width / 4, height / 2) == !frame);
        CHECK(input_contains(display, window, width * 3 / 4, height / 2) == !!frame);
        bongo_cat_platform_set_click_through(&app->platform, false, true);
        CHECK(input_contains(display, window, width / 4, height / 2) == !frame);
        bongo_cat_platform_set_click_through(&app->platform, true, false);
        CHECK(!input_contains(display, window, width / 4, height / 2));
        CHECK(!input_contains(display, window, width * 3 / 4, height / 2));
        bongo_cat_platform_set_click_through(&app->platform, false, false);
        CHECK(input_contains(display, window, width / 4, height / 2) == !frame);
        CHECK(input_contains(display, window, width * 3 / 4, height / 2) == !!frame);
    }
    Window after;
    XGetInputFocus(display, &after, &revert);
    CHECK(after == focus);
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}
