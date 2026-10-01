#include "model_import.h"
#include "model_import_path.h"
#include "model_import_probe.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static bool image_package_root(const char *source, char *directory,
    size_t capacity) {
    char current[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_import_parent_path(source, current, sizeof(current)))
        return false;
    for (int depth = 0; depth < 12; ++depth) {
        if (SDL_strcasecmp(bongo_cat_path_name(current), "img") == 0)
            return bongo_cat_import_parent_path(current, directory, capacity);
        char parent[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_import_parent_path(current, parent, sizeof(parent)))
            return false;
        snprintf(current, sizeof(current), "%s", parent);
    }
    return false;
}

BongoCatResult bongo_cat_import_source_directory(const char *source,
    char *directory, size_t capacity, BongoCatError *error) {
    if (!source || !source[0] || !directory || !capacity) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Missing model import source");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (bongo_cat_path_is_dir(source)) {
        int length = snprintf(directory, capacity, "%s", source);
        if (length >= 0 && (size_t)length < capacity) return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Model import path is too long");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (!bongo_cat_path_is_file(source)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Selected model source does not exist: %s", source);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    const char *name = bongo_cat_path_name(source);
    if (name && bongo_cat_import_has_suffix_ci(name, ".moc3"))
        return bongo_cat_import_probe_live2d_owner(source, directory,
            capacity, error);
    if (name && bongo_cat_import_has_suffix_ci(name, ".png") &&
        image_package_root(source, directory, capacity)) return BONGO_CAT_OK;
    if (!name || (SDL_strcasecmp(name, "config.json") != 0 &&
        SDL_strcasecmp(name, BONGO_CAT_SKIN_CONFIG_FILE) != 0 &&
        !bongo_cat_import_has_suffix_ci(name, ".model3.json"))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Select a BongoCat skin file, Mver config.json, image-patch PNG, "
            "or Live2D .model3.json/.moc3 file");
        return BONGO_CAT_ERROR_FORMAT;
    }
    if (bongo_cat_import_parent_path(source, directory, capacity))
        return BONGO_CAT_OK;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
        "Cannot determine the selected model file directory: %s", source);
    return BONGO_CAT_ERROR_ARGUMENT;
}
