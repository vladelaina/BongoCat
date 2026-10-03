#include "model_storage.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

static bool mark_builtin(const char *directory, BongoCatError *error) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), directory,
        BONGO_CAT_MODEL_BUILTIN_MARKER)) return false;
    if (bongo_cat_path_is_file(path)) return true;
    FILE *file = bongo_cat_file_open(path, "wb");
    bool ok = file && fputs("BongoCat built-in model\n", file) >= 0;
    if (file && fclose(file) != 0) ok = false;
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot mark built-in model: %s", directory);
    return ok;
}

BongoCatResult bongo_cat_model_install_builtins(const char *asset_root,
    const char *models_root, bool first_run, BongoCatError *error) {
    static const char *const names[] = {"standard", "keyboard", "gamepad"};
    char source_root[BONGO_CAT_PATH_CAP];
    if (!asset_root || !models_root ||
        !bongo_cat_path_join(source_root, sizeof(source_root), asset_root,
            "models") || !bongo_cat_path_create_directory(models_root))
        return BONGO_CAT_ERROR_ARGUMENT;
    char initialized[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(initialized, sizeof(initialized), models_root,
            ".bongo-cat-builtins-initialized")) return BONGO_CAT_ERROR_ARGUMENT;
    if (bongo_cat_path_is_file(initialized)) return BONGO_CAT_OK;
    /* Migrate existing installations without restoring missing defaults. */
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char directory[BONGO_CAT_PATH_CAP], marker[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(directory, sizeof(directory), models_root, names[i]) &&
            bongo_cat_path_join(marker, sizeof(marker), directory,
                BONGO_CAT_MODEL_BUILTIN_MARKER) && bongo_cat_path_is_file(marker))
            first_run = false;
    }
    /* Record initialization before copying: a later scan must never fill in
       a missing model, including after an interrupted first installation. */
    FILE *file = bongo_cat_file_open(initialized, "wb");
    bool recorded = file && fputs("Built-in models initialized\n", file) >= 0;
    if (file && fclose(file) != 0) recorded = false;
    if (!recorded) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot record built-in model initialization: %s", models_root);
        return BONGO_CAT_ERROR_IO;
    }
    if (!first_run) return BONGO_CAT_OK;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(source, sizeof(source), source_root, names[i]) ||
            !bongo_cat_path_join(target, sizeof(target), models_root, names[i]) ||
            !bongo_cat_path_is_dir(source)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Built-in model assets are missing: %s", names[i]);
            return BONGO_CAT_ERROR_IO;
        }
        if (!bongo_cat_path_is_dir(target)) {
            if (bongo_cat_path_is_file(target) ||
                bongo_cat_model_copy_directory(source, target, error) != BONGO_CAT_OK)
                return error && error->code ? error->code : BONGO_CAT_ERROR_IO;
        }
        if (!mark_builtin(target, error))
            return error && error->code ? error->code : BONGO_CAT_ERROR_IO;
    }
    return BONGO_CAT_OK;
}
