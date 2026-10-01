#include "runtime.h"
#include "model_catalog_selection.h"
#include "model_import.h"
#include "model_import_lock.h"
#include "model_storage.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static size_t models_using_storage(const BongoCatModelCatalog *models,
    const char *directory) {
    size_t count = 0;
    if (!models || !directory || !directory[0]) return 0;
    for (size_t i = 0; i < models->count; ++i)
        if (!strcmp(models->entries[i].storage_directory, directory)) count++;
    return count;
}

static bool storage_package_id(const char *directory,
    char output[BONGO_CAT_ID_CAP]) {
    const char *name = directory ? bongo_cat_path_name(directory) : NULL;
    return name && name[0] && bongo_cat_import_package_id(output,
        BONGO_CAT_ID_CAP, name);
}

BongoCatResult bongo_cat_app_remove_model(BongoCatApp *app, const char *id,
    BongoCatError *error) {
    if (!app || !id) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Missing model id");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Model is not installed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (entry->preset || entry->managed) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, entry->managed
            ? "Nearby models are managed by their source directory: %s"
            : "Built-in models cannot be removed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    bool primary = !strcmp(id, app->session.active_model_id) ||
        !strcmp(id, app->loaded_model);
    bool additional = !primary && bongo_cat_app_model_active(app, id);
    BongoCatModelSelection previous_selection =
        bongo_cat_model_selection_capture(app);
    char directory[BONGO_CAT_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s", entry->storage_directory);
    char package_id[BONGO_CAT_ID_CAP];
    if (!directory[0] || !storage_package_id(directory, package_id)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model storage directory is invalid: %s", id);
        return BONGO_CAT_ERROR_FORMAT;
    }
    bool shared_storage = models_using_storage(&app->models, directory) > 1;
    bongo_cat_settings_validate(&app->settings);
    bool already_removed = bongo_cat_settings_model_removed(
        &app->settings, id);
    if (shared_storage && !already_removed &&
        app->settings.removed_model_count >= BONGO_CAT_MODEL_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Too many removed model versions are recorded");
        return BONGO_CAT_ERROR_FORMAT;
    }
    if (additional) {
        BongoCatError deactivate_error = {0};
        if (!bongo_cat_app_set_model_active(app, id, false,
                &deactivate_error)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                "Cannot stop the active model: %s",
                deactivate_error.message);
            return BONGO_CAT_ERROR_PLATFORM;
        }
    } else if (primary) {
        BongoCatError load_error = {0};
        bool replacement = false;
        for (size_t i = 0; i < app->models.count; ++i)
            if (strcmp(app->models.entries[i].id, id) &&
                bongo_cat_app_select_model_with_error(app,
                    app->models.entries[i].id, &load_error)) {
                replacement = true;
                break;
            }
        if (!replacement) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
                "Cannot delete the active model because no replacement could be displayed: %s",
                load_error.message[0] ? load_error.message :
                "no installed models");
            return BONGO_CAT_ERROR_CUBISM;
        }
    }
    if (shared_storage && !already_removed &&
        !bongo_cat_settings_set_model_removed(&app->settings, id, true)) {
        if (primary) {
            BongoCatError restore_error = {0};
            if (!bongo_cat_app_select_model_with_error(app, id,
                    &restore_error))
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to restore model after recording deletion failed: %s",
                    restore_error.message[0] ? restore_error.message :
                    "unknown error");
        }
        if (additional)
            bongo_cat_model_selection_restore(app, &previous_selection);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot record removed model version: %s", id);
        return BONGO_CAT_ERROR_FORMAT;
    }
    bool removed = true;
    if (!shared_storage) {
        bongo_cat_import_storage_lock();
        removed = bongo_cat_model_remove_tree(directory, error);
        bongo_cat_import_storage_unlock();
    }
    if (!removed) {
        bongo_cat_app_rescan_models(app);
        bool restore_selection = additional;
        if (primary && bongo_cat_models_find(&app->models, id)) {
            BongoCatError restore_error = {0};
            restore_selection = bongo_cat_app_select_model_with_error(app, id,
                &restore_error);
            if (!restore_selection)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to restore model after deletion failed: %s",
                    restore_error.message[0] ? restore_error.message :
                    "unknown error");
        }
        if (restore_selection)
            bongo_cat_model_selection_restore(app, &previous_selection);
        return BONGO_CAT_ERROR_IO;
    }
    if (!shared_storage)
        bongo_cat_settings_restore_model_package(&app->settings, package_id);
    bongo_cat_app_forget_behavior_state(app, id);
    bongo_cat_settings_set_model_label(&app->settings, id, "");
    size_t output = 0;
    size_t id_length = strlen(id);
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *shortcut = &app->settings.behavior_shortcuts[i];
        if (!strncmp(shortcut->id, id, id_length) &&
            shortcut->id[id_length] == ':') continue;
        app->settings.behavior_shortcuts[output++] = *shortcut;
    }
    app->settings.behavior_shortcut_count = output;
    bongo_cat_app_rescan_models(app);
    return BONGO_CAT_OK;
}
