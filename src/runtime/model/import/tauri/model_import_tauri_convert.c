#include "model_import_tauri_internal.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct TauriConversionWorkspace {
    TauriKeyFiles left;
    TauriKeyFiles right;
} TauriConversionWorkspace;

static bool create_target_tree(const char *target, BongoCatModelMode mode,
    char *mode_root, size_t mode_capacity, char *model_root,
    size_t model_capacity) {
    char image_root[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_create_directory(target) &&
        bongo_cat_path_join(image_root, sizeof(image_root), target, "img") &&
        bongo_cat_path_join(mode_root, mode_capacity, image_root,
            bongo_cat_mode_name(mode)) &&
        bongo_cat_path_join(model_root, model_capacity, mode_root,
            "cat_model") && bongo_cat_path_create_directory(mode_root) &&
        bongo_cat_path_create_directory(model_root);
}

bool bongo_cat_import_tauri_convert_to_mver(
    const BongoCatImportCandidate *candidate, const char *target,
    BongoCatImportCandidate *installed, BongoCatError *error) {
    if (!candidate || !target || !installed ||
        candidate->format != BONGO_CAT_IMPORT_TAURI) return false;
    char mode_root[BONGO_CAT_PATH_CAP], model_root[BONGO_CAT_PATH_CAP];
    if (!create_target_tree(target, candidate->mode, mode_root,
            sizeof(mode_root), model_root, sizeof(model_root)) ||
        !bongo_cat_tauri_copy_model_tree(candidate->directory, model_root, 0,
            error)) return false;
    TauriConversionWorkspace *work = calloc(1, sizeof(*work));
    if (!work) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate Tauri conversion workspace");
        return false;
    }
    if (!bongo_cat_tauri_copy_input_images(candidate,
            mode_root, &work->left, &work->right, error)) {
        free(work);
        return false;
    }
    TauriMverCalibration calibration;
    char config[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_tauri_read_calibration(candidate, &calibration) ||
        !bongo_cat_path_join(config, sizeof(config), target, "config.json") ||
        !bongo_cat_tauri_write_config(config, candidate->mode, &work->left,
            &work->right, &calibration, error) ||
        !bongo_cat_tauri_copy_preview(candidate, mode_root, error) ||
        !bongo_cat_tauri_copy_runtime_images(candidate, mode_root, error)) {
        free(work);
        return false;
    }
    free(work);
    *installed = *candidate;
    snprintf(installed->directory, sizeof(installed->directory), "%s",
        model_root);
    snprintf(installed->assets, sizeof(installed->assets), "%s", mode_root);
    snprintf(installed->package_root, sizeof(installed->package_root), "%s",
        target);
    snprintf(installed->config, sizeof(installed->config), "%s", config);
    installed->overrides[0] = '\0';
    installed->patch_root[0] = '\0';
    installed->format = BONGO_CAT_IMPORT_MVER;
    installed->gamepad_buttons = candidate->mode == BONGO_CAT_MODE_GAMEPAD;
    return true;
}
