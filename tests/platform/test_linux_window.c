#include "runtime.h"
#include "test.h"
#include <stdlib.h>

int bongo_cat_test_failures;
void bongo_cat_test_linux_click_through(BongoCatApp *app);

int main(int argc, char **argv) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    BongoCatError error = {0};
    if (!app) return 1;
    if (!bongo_cat_app_initialize(app, argc, argv, &error)) {
        fprintf(stderr, "Initialization failed: %s\n", error.message);
        bongo_cat_app_shutdown(app, "test:failed", 1);
        free(app);
        return 1;
    }
    app->startup_visibility_pending = false;
    app->settings.window.always_on_top = true;
    bongo_cat_platform_set_always_on_top(&app->platform, true);
    bongo_cat_window_set_visible(app, true);
    SDL_SyncWindow(app->window);
    int x, y;
    CHECK(SDL_GetWindowPosition(app->window, &x, &y));
    bongo_cat_window_set_visible(app, false);
    CHECK(app->session.window.position_known);
    CHECK(app->session.window.x == x && app->session.window.y == y);
    CHECK(SDL_GetWindowFlags(app->window) & SDL_WINDOW_HIDDEN);
    SDL_Rect bounds = {0};
    CHECK(SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(app->window), &bounds));
    app->session.window.x = bounds.x - 10000;
    app->session.window.y = bounds.y - 10000;
    bongo_cat_window_set_visible(app, true);
    SDL_SyncWindow(app->window);
    CHECK(SDL_GetWindowPosition(app->window, &x, &y));
    CHECK(x == bounds.x - 10000 && y == bounds.y - 10000);
    CHECK(!(SDL_GetWindowFlags(app->window) & SDL_WINDOW_HIDDEN));
    CHECK(SDL_GetWindowFlags(app->window) & SDL_WINDOW_ALWAYS_ON_TOP);
    SDL_Event exposed = {.type = SDL_EVENT_WINDOW_EXPOSED};
    exposed.window.windowID = SDL_GetWindowID(app->window);
    app->window_minimized = true;
    CHECK(bongo_cat_window_event(app, &exposed));
    CHECK(app->window_minimized ==
        ((SDL_GetWindowFlags(app->window) & SDL_WINDOW_MINIMIZED) != 0));
    for (int i = 0; i < 3; ++i) {
        bongo_cat_window_set_visible(app, false);
        bongo_cat_window_set_visible(app, true);
    }
    CHECK(!app->startup_visibility_pending && app->session.window.visible);
    bongo_cat_test_linux_click_through(app);
    bongo_cat_app_shutdown(app, "test:complete", bongo_cat_test_failures ? 1 : 0);
    free(app);
    return bongo_cat_test_failures ? 1 : 0;
}
