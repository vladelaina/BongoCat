#include "model_import_tauri_internal.h"

#include <SDL3/SDL.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define TAURI_MODEL_COPY_DEPTH_CAP 32

typedef struct TauriModelCopy {
    const char *target_root;
    BongoCatError *error;
    unsigned depth;
} TauriModelCopy;

static bool copy_resource_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);

static bool interface_resource(const char *name) {
    static const char *const files[] = {
        "cover.png", "background.png", "cat.png", "bg.png",
        "mousebg.png", "tabletbg.png", BONGO_CAT_TAURI_SOURCE_FILE
    };
    if (SDL_strcasecmp(name, "left-keys") == 0 ||
        SDL_strcasecmp(name, "right-keys") == 0) return true;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        if (SDL_strcasecmp(name, files[i]) == 0) return true;
    return false;
}

static BongoCatPathVisit copy_resource_child(void *userdata,
    const char *dirname, const char *name) {
    TauriModelCopy *context = userdata;
    if (!name || name[0] == '.' || interface_resource(name))
        return BONGO_CAT_PATH_CONTINUE;
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name) ||
        !bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_dir(source))
        return bongo_cat_tauri_copy_model_tree(source, target,
            context->depth + 1, context->error)
            ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(source)) return BONGO_CAT_PATH_CONTINUE;
    if (!bongo_cat_path_create_directory(context->target_root) ||
        !bongo_cat_path_copy_file(source, target)) {
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Cannot preserve Tauri Live2D resource: %s", source);
        return BONGO_CAT_PATH_FAILURE;
    }
    return BONGO_CAT_PATH_CONTINUE;
}

static BongoCatPathVisit copy_model_child(void *userdata,
    const char *dirname, const char *name) {
    TauriModelCopy *context = userdata;
    if (!name || name[0] == '.' || (context->depth == 0 &&
            SDL_strcasecmp(name, BONGO_CAT_TAURI_SOURCE_FILE) == 0))
        return BONGO_CAT_PATH_CONTINUE;
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name) ||
        !bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    if (context->depth == 0 && bongo_cat_path_is_dir(source) &&
        SDL_strcasecmp(name, "resources") == 0)
        return copy_resource_tree(source, target, context->depth + 1,
            context->error) ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_dir(source))
        return bongo_cat_tauri_copy_model_tree(source, target,
            context->depth + 1, context->error)
            ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(source) ||
        !bongo_cat_path_copy_file(source, target)) {
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Cannot copy Tauri Live2D asset: %s", source);
        return BONGO_CAT_PATH_FAILURE;
    }
    return BONGO_CAT_PATH_CONTINUE;
}

bool bongo_cat_tauri_copy_model_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > TAURI_MODEL_COPY_DEPTH_CAP ||
        !bongo_cat_path_create_directory(target) ||
        !bongo_cat_path_enumerate(source, copy_model_child,
            &(TauriModelCopy){target, error, depth})) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot copy Tauri Live2D directory: %s",
            source);
        return false;
    }
    return true;
}

static bool copy_resource_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > TAURI_MODEL_COPY_DEPTH_CAP ||
        !bongo_cat_path_enumerate(source, copy_resource_child,
            &(TauriModelCopy){target, error, depth})) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot inspect Tauri resource directory: %s",
            source);
        return false;
    }
    return true;
}
