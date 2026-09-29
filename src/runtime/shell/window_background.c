#include "runtime.h"

#include <SDL3/SDL_opengl.h>

void bongo_cat_window_clear_background(BongoCatApp *app) {
    BongoCatWindowPreferences *window = &app->settings.window;
    uint32_t rgb = bongo_cat_obs_background_color_rgb(
        window->obs_background_color);
    float alpha = window->obs_background ? 1.0f : 0.0f;
    glClearColor(window->obs_background ? ((rgb >> 16) & 255) / 255.0f : 0.0f,
        window->obs_background ? ((rgb >> 8) & 255) / 255.0f : 0.0f,
        window->obs_background ? (rgb & 255) / 255.0f : 0.0f, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    static int last_enabled = -1;
    static BongoCatObsBackgroundColor last_color =
        BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT;
    if (last_enabled != (window->obs_background ? 1 : 0) ||
        last_color != window->obs_background_color) {
        last_enabled = window->obs_background;
        last_color = window->obs_background_color;
        SDL_Log("OBS background mode: enabled=%d color=%s",
            window->obs_background,
            bongo_cat_obs_background_color_name(window->obs_background_color));
    }
}
