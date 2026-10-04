#include "preferences_gl.h"
#include "preferences_state.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>

static bool fail(BongoCatPreferences *value, const char *message) {
    char detail[256];
    snprintf(detail, sizeof(detail), "%s",
        message && message[0] ? message : "unknown OpenGL error");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
        "Preferences dedicated OpenGL context failed: %s "
        "main_window=%p settings_window=%p main_context=%p",
        detail, (void *)value->app->window, (void *)value->window,
        (void *)value->app->gl_context);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    SDL_SetError("%s", detail);
    return false;
}

bool bongo_cat_preferences_gl_create(BongoCatPreferences *value) {
    if (!value || !value->app || !value->window || !value->app->window ||
        !value->app->gl_context)
        return false;
    if (!SDL_GL_MakeCurrent(value->app->window, value->app->gl_context))
        return fail(value, SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1))
        return fail(value, SDL_GetError());
    SDL_GLContext context = SDL_GL_CreateContext(value->window);
    char creation_error[256] = {0};
    if (!context) snprintf(creation_error, sizeof(creation_error), "%s",
        SDL_GetError());
    bool reset = SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (!context)
        return fail(value, creation_error[0] ? creation_error :
            "SDL_GL_CreateContext returned no context");
    if (!reset) {
        snprintf(creation_error, sizeof(creation_error), "%s", SDL_GetError());
        SDL_GL_DestroyContext(context);
        return fail(value, creation_error);
    }
    if (!SDL_GL_MakeCurrent(value->window, context)) {
        snprintf(creation_error, sizeof(creation_error), "%s", SDL_GetError());
        SDL_GL_DestroyContext(context);
        return fail(value, creation_error);
    }
    value->gl_context = context;
    value->owns_gl_context = true;
    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    SDL_Log("[runtime] Preferences OpenGL context: shared=1 dedicated=1 "
        "main_window=%p settings_window=%p main_context=%p settings_context=%p "
        "vendor=%s renderer=%s version=%s",
        (void *)value->app->window, (void *)value->window,
        (void *)value->app->gl_context, (void *)value->gl_context,
        vendor ? (const char *)vendor : "unknown",
        renderer ? (const char *)renderer : "unknown",
        version ? (const char *)version : "unknown");
    return true;
}

bool bongo_cat_preferences_gl_destroy(BongoCatPreferences *value) {
    if (!value || !value->gl_context) return true;
    SDL_Log("[runtime] Preferences OpenGL context release: "
        "settings_window=%p settings_context=%p current_window=%p "
        "current_context=%p", (void *)value->window,
        (void *)value->gl_context, (void *)SDL_GL_GetCurrentWindow(),
        (void *)SDL_GL_GetCurrentContext());
    if (SDL_GL_GetCurrentContext() == value->gl_context)
        SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    bool destroyed = !value->owns_gl_context || SDL_GL_DestroyContext(value->gl_context);
    value->gl_context = NULL;
    value->owns_gl_context = false;
    return destroyed;
}
