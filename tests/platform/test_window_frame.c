#include "runtime.h"
#include "../../src/live2d/model_frame_policy.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static int window_x, window_y, window_w, window_h, dpi, resize_calls;
static bool fail_resize, flipped;
static uint64_t ticks;
static BongoCatLive2DFrame allocated, requested;
static BongoCatFrameViewport viewport;

static bool position(SDL_Window *window, int *x, int *y) {
    (void)window; *x = window_x; *y = window_y; return true;
}
static bool size(SDL_Window *window, int *w, int *h) {
    (void)window; *w = window_w; *h = window_h; return true;
}
static bool pixels(SDL_Window *window, int *w, int *h) {
    (void)window; *w = window_w * dpi; *h = window_h * dpi; return true;
}
static SDL_DisplayID frame_test_display(SDL_Window *window) { (void)window; return 1; }
static bool sync_window(SDL_Window *window) { (void)window; return true; }
static bool frame_test_bounds(SDL_DisplayID id, SDL_Rect *rect) {
    (void)id; *rect = (SDL_Rect){0, 0, 1920, 1080}; return true;
}
static uint64_t time_ns(void) { return ticks; }

static void update_viewport(void) {
    viewport = bongo_cat_frame_viewport(allocated, requested,
        window_w * dpi, window_h * dpi, flipped);
}
bool bongo_cat_live2d_frame(const BongoCatLive2D *model, BongoCatLive2DFrame *frame) {
    (void)model; *frame = allocated; return true;
}
bool bongo_cat_live2d_measure_frame(BongoCatLive2D *model, BongoCatLive2DFrame *frame) {
    (void)model; *frame = requested; update_viewport(); return true;
}
void bongo_cat_live2d_set_frame(BongoCatLive2D *model, const BongoCatLive2DFrame *frame) {
    (void)model; allocated = *frame; update_viewport();
}
bool bongo_cat_live2d_viewport(const BongoCatLive2D *model,
    int *x, int *y, int *w, int *h) {
    (void)model; *x = viewport.x; *y = viewport.y;
    *w = viewport.width; *h = viewport.height; return true;
}
bool bongo_cat_window_content_size(BongoCatApp *app,
    int w, int h, int *cw, int *ch) {
    (void)app; *cw = (int)lround(w / (1.0 + allocated.left + allocated.right));
    *ch = (int)lround(h / (1.0 + allocated.top + allocated.bottom)); return true;
}
bool bongo_cat_window_frame_size(BongoCatApp *app, int cw, int ch,
    int *w, int *h, int *left, int *top) {
    *w = (int)lround(cw * (1.0 + allocated.left + allocated.right));
    *h = (int)lround(ch * (1.0 + allocated.top + allocated.bottom));
    *left = (int)lround(cw * allocated.left);
    *top = (int)lround(ch * (app->settings.model.vertical_flip ? allocated.bottom : allocated.top));
    return true;
}
bool bongo_cat_window_apply_geometry(BongoCatApp *app, int x, int y,
    float scale, int w, int h) {
    ++resize_calls;
    if (fail_resize) return false;
    window_x = x; window_y = y; window_w = w; window_h = h;
    app->session.window.x = x; app->session.window.y = y;
    app->session.window.width = w; app->session.window.height = h;
    app->session.window.scale_percent = scale;
    app->resize_pending = true;
    return true;
}
void bongo_cat_window_apply_pending_resize(BongoCatApp *app) {
    if (!app->resize_pending) return;
    app->resize_pending = false;
    app->model_pointer_anchor_ready = false;
    update_viewport();
}

#define SDL_GetWindowPosition position
#define SDL_SyncWindow sync_window
#define SDL_GetWindowSize size
#define SDL_GetWindowSizeInPixels pixels
#define SDL_GetDisplayForWindow frame_test_display
#define SDL_GetDisplayUsableBounds frame_test_bounds
#define SDL_GetTicksNS time_ns
#include "../../src/runtime/shell/window_frame.c"

static void reset(BongoCatApp *app) {
    memset(app, 0, sizeof(*app));
    app->window = (SDL_Window *)app;
    app->loaded_model[0] = 'm';
    window_x = 500; window_y = 400; window_w = 640; window_h = 320;
    dpi = 1; resize_calls = 0; fail_resize = flipped = false;
    ticks = 1000000000ull;
    allocated = requested = (BongoCatLive2DFrame){0};
    app->session.window.scale_percent = 100;
    app->session.window.content_width = 640;
    app->session.window.content_height = 320;
    app->model_pointer_anchor_ready = true;
    app->model_pointer_anchor_x = app->model_pointer_anchor_y = .5f;
    update_viewport();
}

int main(void) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    reset(app);
    bongo_cat_window_update_model_frame(app);
    CHECK(!resize_calls);
    requested = (BongoCatLive2DFrame){.25f, .125f, 0, 0};
    bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 1 && window_w == 800 && window_h == 360);
    CHECK(window_x == 340 && window_y == 360);
    CHECK(viewport.width == 640 && viewport.height == 320 && viewport.scale == 1.0f);
    CHECK(fabs(window_x + app->model_pointer_anchor_x * window_w - 820) < .01);
    CHECK(fabs(window_y + app->model_pointer_anchor_y * window_h - 560) < .01);
    CHECK(app->session.window.content_width == 640 && app->session.window.content_height == 320);
    bongo_cat_window_store_content_origin(app);
    CHECK(app->session.window.content_left == 160 && app->session.window.content_top == 40);
    for (int i = 0; i < 1000; ++i) bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 1);

    reset(app);
    app->settings.model.vertical_flip = flipped = true;
    requested = (BongoCatLive2DFrame){0, .125f, 0, 0};
    bongo_cat_window_update_model_frame(app);
    CHECK(window_y == 400 && viewport.y == 40);
    CHECK(fabs(window_y + app->model_pointer_anchor_y * window_h - 560) < .01);

    reset(app);
    requested.left = .25f;
    app->resize_gesture = true;
    bongo_cat_window_update_model_frame(app);
    CHECK(!resize_calls && viewport.scale < 1.0f);
    app->resize_gesture = false;
    bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 1 && viewport.scale == 1.0f);

    reset(app);
    window_x = 0; requested.left = .25f;
    bongo_cat_window_update_model_frame(app);
    CHECK(!resize_calls && window_x == 0 && viewport.scale < 1.0f);
    CHECK(viewport.x - requested.left * viewport.width < 0);

    reset(app);
    dpi = 4; update_viewport();
    requested = (BongoCatLive2DFrame){4, 4, 4, 4};
    bongo_cat_window_update_model_frame(app);
    CHECK((double)window_w * window_h * dpi * dpi <= 4194304.0);
    CHECK(viewport.scale < 1.0f);
    int allocated_calls = resize_calls;
    for (int i = 0; i < 100; ++i) bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == allocated_calls);

    reset(app);
    fail_resize = true; requested.left = .25f;
    bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 1 && allocated.left == 0 && viewport.scale < 1.0f);
    for (int i = 0; i < 100; ++i) bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 1);
    fail_resize = false; ticks += 1000000000ull;
    bongo_cat_window_update_model_frame(app);
    CHECK(resize_calls == 2 && allocated.left == .25f && viewport.scale == 1.0f);
    free(app);
    return bongo_cat_test_failures ? 1 : 0;
}
