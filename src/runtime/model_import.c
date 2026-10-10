#include "model_import.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImportInstall {
    char id[BONGO_CAT_ID_CAP];
    char temporary[BONGO_CAT_PATH_CAP];
    char target[BONGO_CAT_PATH_CAP];
    bool committed;
} ImportInstall;

static bool custom_root(const char *data_root, char *path, size_t capacity) {
    return data_root && data_root[0] &&
        bongo_cat_path_join(path, capacity, data_root, "custom-models") &&
        bongo_cat_path_create_directory(path);
}

static bool copy_optional(const char *source_dir, const char *source_name,
    const char *target_dir, const char *target_name) {
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(source, sizeof(source), source_dir, source_name) &&
        bongo_cat_path_join(target, sizeof(target), target_dir, target_name) &&
        bongo_cat_path_copy_file(source, target);
}

static bool copy_first(const char *source_dir, const char *const *names, size_t count,
    const char *target_dir, const char *target_name) {
    for (size_t i = 0; i < count; ++i) {
        char source[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(source, sizeof(source), source_dir, names[i]) ||
            !bongo_cat_path_is_file(source)) continue;
        return copy_optional(source_dir, names[i], target_dir, target_name);
    }
    return true;
}

static bool copy_preview_file(const char *source_resources, const char *source_root,
    const char *const *names, size_t count, const char *target_resources,
    const char *target_name) {
    for (size_t i = 0; i < count; ++i) {
        char source[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(source, sizeof(source), source_resources, names[i]) &&
            bongo_cat_path_is_file(source))
            return copy_optional(source_resources, names[i], target_resources, target_name);
    }
    return copy_first(source_root, names, count, target_resources, target_name);
}

static bool preview_file_exists(const char *directory, const char *name) {
    char path[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(path, sizeof(path), directory, name) &&
        bongo_cat_path_is_file(path);
}

static void mark_preview_fallback(const char *resources) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), resources,
        ".bongo-cat-cover-fallback")) return;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (file) fclose(file);
}

static bool copy_preview(const BongoCatImportCandidate *candidate, const char *target,
    BongoCatError *error) {
    char source_resources[BONGO_CAT_PATH_CAP], target_resources[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source_resources, sizeof(source_resources),
        candidate->assets, "resources") ||
        !bongo_cat_path_join(target_resources, sizeof(target_resources), target, "resources"))
        return false;
    bool target_exists = bongo_cat_path_is_dir(target_resources);
    if (bongo_cat_path_is_dir(source_resources) && !target_exists) {
        if (bongo_cat_copy_directory(source_resources, target_resources, error) != BONGO_CAT_OK)
            return false;
        target_exists = true;
    }
    if (!target_exists && !bongo_cat_path_create_directory(target_resources)) return false;
    const char *covers[] = {"cover.png", "cat.png", "bg.png", "mousebg.png",
        "tabletbg.png"};
    const char *backgrounds[] = {"background.png", "bg.png", "mousebg.png",
        "tabletbg.png"};
    bool authored_cover = preview_file_exists(source_resources, "cover.png") ||
        preview_file_exists(candidate->assets, "cover.png");
    bool ok = (preview_file_exists(target_resources, "cover.png") ||
        copy_preview_file(source_resources, candidate->assets, covers, 5,
            target_resources, "cover.png")) &&
        (preview_file_exists(target_resources, "background.png") ||
        copy_preview_file(source_resources, candidate->assets, backgrounds, 4,
            target_resources, "background.png"));
    if (ok && !authored_cover && preview_file_exists(target_resources, "cover.png"))
        mark_preview_fallback(target_resources);
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot copy model preview assets: %s", SDL_GetError());
    return ok;
}

static bool write_mode(const char *target, BongoCatModelMode mode, BongoCatError *error) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), target, ".bongo-cat-mode")) return false;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    const char *name = mode == BONGO_CAT_MODE_KEYBOARD ? "keyboard" :
        mode == BONGO_CAT_MODE_GAMEPAD ? "gamepad" : "standard";
    bool ok = fputs(name, file) >= 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot write imported model metadata");
    return ok;
}

bool bongo_cat_import_prepare_adapter(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (!candidate || !target ||
        (!bongo_cat_path_is_dir(target) && !bongo_cat_path_create_directory(target))) return false;
    return copy_preview(candidate, target, error) &&
        bongo_cat_import_mver_assets(candidate, target, error) &&
        bongo_cat_import_mver_metadata(candidate, target, error) &&
        bongo_cat_import_write_report(candidate, target, error) &&
        write_mode(target, candidate->mode, error);
}

static void cleanup(ImportInstall *installs, size_t count, bool committed) {
    for (size_t i = 0; i < count; ++i)
        bongo_cat_model_remove_tree(committed && installs[i].committed ? installs[i].target :
            installs[i].temporary, NULL);
}

static bool prepare_install(const BongoCatImportCandidate *candidate,
    ImportInstall *install, const char *root, unsigned long long stamp,
    size_t index, size_t count, BongoCatError *error) {
    const char *mode = candidate->mode == BONGO_CAT_MODE_KEYBOARD ? "keyboard" :
        candidate->mode == BONGO_CAT_MODE_GAMEPAD ? "gamepad" : "standard";
    if (count == 1) snprintf(install->id, sizeof(install->id), "model-%llx", stamp);
    else snprintf(install->id, sizeof(install->id), "model-%llx-%s-%zu",
        stamp, mode, index + 1);
    char temporary_name[BONGO_CAT_ID_CAP + 8];
    snprintf(temporary_name, sizeof(temporary_name), ".import-%llx-%zu.tmp",
        stamp, index + 1);
    BongoCatImportCandidate installed;
    if (!bongo_cat_path_join(install->temporary, sizeof(install->temporary),
        root, temporary_name) ||
        !bongo_cat_path_join(install->target, sizeof(install->target), root, install->id) ||
        !bongo_cat_import_prepare_package(candidate, install->temporary,
            &installed, error)) return false;
    char adapter[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(adapter, sizeof(adapter), install->temporary, "adapter") ||
        !bongo_cat_import_prepare_adapter(&installed, adapter, error) ||
        !bongo_cat_import_write_package(&installed, install->temporary, error)) return false;
    return bongo_cat_import_manifest_valid(installed.directory,
        installed.setting, error);
}

BongoCatResult bongo_cat_import_install(const char *source, const char *data_root,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!source || !data_root || !bongo_cat_path_is_dir(source))
        return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    ImportInstall *installs = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP, sizeof(*installs));
    if (!discovery || !installs) {
        free(discovery); free(installs);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    if (!bongo_cat_import_discover(source, discovery, error)) {
        free(discovery); free(installs); return BONGO_CAT_ERROR_FORMAT;
    }
    char root[BONGO_CAT_PATH_CAP];
    if (!custom_root(data_root, root, sizeof(root)) ||
        !bongo_cat_model_cleanup_imports(root, error)) {
        free(discovery); free(installs); return BONGO_CAT_ERROR_IO;
    }
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    for (size_t i = 0; i < discovery->count; ++i) {
        if (prepare_install(&discovery->candidates[i], &installs[i], root,
            stamp, i, discovery->count, error)) continue;
        cleanup(installs, i + 1, false);
        BongoCatResult result = error && error->code == BONGO_CAT_ERROR_FORMAT
            ? BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
        free(discovery); free(installs); return result;
    }
    for (size_t i = 0; i < discovery->count; ++i) {
        if (!bongo_cat_path_rename(installs[i].temporary, installs[i].target)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot finish model import: %s", SDL_GetError());
            cleanup(installs, discovery->count, true);
            free(discovery); free(installs);
            return BONGO_CAT_ERROR_IO;
        }
        installs[i].committed = true;
    }
    if (receipt) {
        receipt->count = discovery->count;
        for (size_t i = 0; i < discovery->count; ++i)
            snprintf(receipt->ids[i], sizeof(receipt->ids[i]), "%s", installs[i].id);
    }
    free(discovery); free(installs);
    return BONGO_CAT_OK;
}

#ifdef BONGO_CAT_HAS_CUBISM
static void remove_receipt(const char *data_root,
    const BongoCatImportReceipt *receipt) {
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    if (!receipt || !custom_root(data_root, root, sizeof(root))) return;
    for (size_t i = 0; i < receipt->count; ++i)
        if (bongo_cat_path_join(path, sizeof(path), root, receipt->ids[i]))
            bongo_cat_model_remove_tree(path, NULL);
}
#endif

BongoCatResult bongo_cat_app_import_model(BongoCatApp *app, const char *source,
    BongoCatError *error) {
    if (!app || !source) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportReceipt receipt;
    BongoCatResult result = bongo_cat_import_install(source, app->data_root,
        &receipt, error);
    if (result != BONGO_CAT_OK) {
        if (error && !error->message[0]) bongo_cat_error_set(error, result,
            "Model import failed while installing: %s", source);
        return result;
    }
    char previous[BONGO_CAT_PATH_CAP];
    snprintf(previous, sizeof(previous), "%s", app->config.current_model);
    bongo_cat_app_rescan_models(app);
    if (receipt.count && bongo_cat_app_select_model(app, receipt.ids[0]))
        return BONGO_CAT_OK;
#ifndef BONGO_CAT_HAS_CUBISM
    const BongoCatModelEntry *entry = receipt.count ?
        bongo_cat_models_find(&app->models, receipt.ids[0]) : NULL;
    if (entry) {
        snprintf(app->config.current_model, sizeof(app->config.current_model),
            "%s", receipt.ids[0]);
        app->config.current_mode = entry->mode;
    }
    return BONGO_CAT_OK;
#else
    remove_receipt(app->data_root, &receipt);
    bongo_cat_app_rescan_models(app);
    if (previous[0]) bongo_cat_app_select_model(app, previous);
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "Model import was rolled back because the Live2D model could not be loaded");
    return BONGO_CAT_ERROR_CUBISM;
#endif
}
