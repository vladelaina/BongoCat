#include "model_import_tauri_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BongoCatPathVisit collect_keys(void *userdata,
    const char *dirname, const char *name) {
    TauriKeyFiles *files = userdata;
    if (files->count >= TAURI_KEY_CAP ||
        !bongo_cat_import_has_suffix_ci(name, ".png"))
        return BONGO_CAT_PATH_CONTINUE;
    int code = bongo_cat_tauri_key_code(name);
    if (code < 0) return BONGO_CAT_PATH_CONTINUE;
    TauriKeyFile *item = &files->values[files->count];
    if (!bongo_cat_path_join(item->path, sizeof(item->path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(item->path)) return BONGO_CAT_PATH_CONTINUE;
    snprintf(item->name, sizeof(item->name), "%s", name);
    item->code = code;
    files->count++;
    return BONGO_CAT_PATH_CONTINUE;
}

static int compare_keys(const void *left, const void *right) {
    const TauriKeyFile *a = left, *b = right;
    return SDL_strcasecmp(a->name, b->name);
}

static bool copy_keys(const BongoCatImportCandidate *candidate,
    const char *mode_root, const char *source_group, const char *target_group,
    TauriKeyFiles *files, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_tauri_find_resource_directory(candidate, source_group,
            source, sizeof(source))) return true;
    if (!bongo_cat_path_enumerate(source, collect_keys, files)) return false;
    qsort(files->values, files->count, sizeof(files->values[0]), compare_keys);
    char destination[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(destination, sizeof(destination), mode_root,
            target_group) || !bongo_cat_path_create_directory(destination))
        return false;
    for (size_t i = 0; i < files->count; ++i) {
        char filename[32], path[BONGO_CAT_PATH_CAP];
        snprintf(filename, sizeof(filename), "%zu.png", i);
        if (!bongo_cat_path_join(path, sizeof(path), destination, filename) ||
            !bongo_cat_path_copy_file(files->values[i].path, path)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot copy Tauri input image: %s", files->values[i].path);
            return false;
        }
    }
    return true;
}

static bool ensure_key(const char *mode_root, const char *target_group,
    int fallback_code, TauriKeyFiles *files, BongoCatError *error) {
    if (files->count) return true;
    char group[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(group, sizeof(group), mode_root, target_group) ||
        !bongo_cat_path_create_directory(group) ||
        !bongo_cat_path_join(target, sizeof(target), group, "0.png") ||
        !bongo_cat_tauri_copy_image_or_placeholder(NULL, target, error))
        return false;
    files->values[0].code = fallback_code;
    files->count = 1;
    return true;
}

bool bongo_cat_tauri_copy_input_images(
    const BongoCatImportCandidate *candidate,
    const char *mode_root, TauriKeyFiles *left, TauriKeyFiles *right,
    BongoCatError *error) {
    *left = (TauriKeyFiles){0};
    *right = (TauriKeyFiles){0};
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        return copy_keys(candidate, mode_root, "left-keys", "hand", left,
            error) &&
            ensure_key(mode_root, "hand", 65, left, error);
    }
    return copy_keys(candidate, mode_root,
        "left-keys", "lefthand", left, error) &&
        copy_keys(candidate, mode_root,
            "right-keys", "righthand", right, error) &&
        ensure_key(mode_root, "lefthand",
            candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 0 : 65, left, error) &&
        ensure_key(mode_root, "righthand",
            candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 1 : 65, right, error);
}
