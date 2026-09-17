#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProgressiveImportLog {
    size_t callbacks;
    size_t resolved;
    size_t installed;
} ProgressiveImportLog;

static void progressive_receipt(void *userdata,
    const BongoCatImportReceipt *receipt) {
    ProgressiveImportLog *log = userdata;
    log->callbacks++;
    log->resolved += receipt->count;
    log->installed += receipt->installed_count;
}

static void import_without_hand_images(void) {
    char *temporary = SDL_GetCurrentDirectory();
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    CHECK(temporary && discovery);
    if (!temporary || !discovery) {
        SDL_free(temporary);
        free(discovery);
        return;
    }
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char models[BONGO_CAT_PATH_CAP], stored[BONGO_CAT_PATH_CAP];
    char path[BONGO_CAT_PATH_CAP], copied[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-no-hand-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "source", true));
    CHECK(mver_fixture(package));
    BongoCatError error = {0};
    CHECK(child(path, sizeof(path), package, "img/standard/hand/0.png", false));
    CHECK(SDL_RemovePath(path));
    CHECK(bongo_cat_import_mver_discover_exact(package, discovery, &error) == 1);
    CHECK(discovery->count == 1);
    CHECK(child(path, sizeof(path), package, "img/standard/hand", false));
    CHECK(bongo_cat_model_remove_tree(path, NULL));
    CHECK(child(models, sizeof(models), root, "models", true));
    BongoCatImportReceipt receipt = {0};
    CHECK(bongo_cat_import_install(package, models, &receipt, &error) == BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1);
    CHECK(child(stored, sizeof(stored), models, receipt.ids[0], false));
    memset(discovery, 0, sizeof(*discovery));
    CHECK(bongo_cat_import_mver_discover_exact(stored, discovery, &error) == 1);
    CHECK(discovery->count == 1);
    CHECK(child(path, sizeof(path), root, "adapter", false));
    CHECK(bongo_cat_import_prepare_adapter(&discovery->candidates[0], path, &error));
    CHECK(child(path, sizeof(path), root, "adapter/resources/left-keys", false));
    CHECK(!bongo_cat_path_find_suffix(path, ".png", copied, sizeof(copied)));
    CHECK(child(path, sizeof(path), package, "img/standard/hand", false));
    CHECK(!bongo_cat_path_is_dir(path));
    CHECK(child(path, sizeof(path), stored, "img/standard/hand", false));
    CHECK(!bongo_cat_path_is_dir(path));
    static const char *const preserved[] = {
        "config.json", "img/standard/cat_model/cat.model3.json",
        "img/standard/cat_model/cat.moc3", "img/standard/cat_model/texture.png"
    };
    for (size_t i = 0; i < sizeof(preserved) / sizeof(preserved[0]); ++i) {
        CHECK(child(path, sizeof(path), package, preserved[i], false));
        CHECK(child(copied, sizeof(copied), stored, preserved[i], false));
        size_t source_size = 0, copied_size = 0;
        void *source_data = SDL_LoadFile(path, &source_size);
        void *copied_data = SDL_LoadFile(copied, &copied_size);
        CHECK(source_data && copied_data && source_size == copied_size &&
            memcmp(source_data, copied_data, source_size) == 0);
        SDL_free(source_data);
        SDL_free(copied_data);
    }
    CHECK(child(path, sizeof(path), package, "img/standard/cat_model/texture.png", false));
    CHECK(SDL_RemovePath(path));
    memset(discovery, 0, sizeof(*discovery));
    CHECK(bongo_cat_import_mver_discover_exact(package, discovery, &error) == -1);
    CHECK(error.code == BONGO_CAT_ERROR_FORMAT);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
    free(discovery);
}

void test_mver_container_discovery(void) {
    import_without_hand_images();
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char backup[BONGO_CAT_PATH_CAP];
    char mode[BONGO_CAT_PATH_CAP];
    char bundle[BONGO_CAT_PATH_CAP], nested_package[BONGO_CAT_PATH_CAP];
    char variant[BONGO_CAT_PATH_CAP], variant_model[BONGO_CAT_PATH_CAP];
    char variant_img[BONGO_CAT_PATH_CAP], variant_standard[BONGO_CAT_PATH_CAP];
    char variant_hand[BONGO_CAT_PATH_CAP], source_hand[BONGO_CAT_PATH_CAP];
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongocat-container-%llu", temporary,
        stamp);
    bongo_cat_model_remove_tree(root, NULL);
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "app", true));
    CHECK(mver_fixture(package));
    CHECK(child(backup, sizeof(backup), package,
        "config.json.before_console_bak", false));
    CHECK(write_text(backup, "backup"));
    char runtime_resources[BONGO_CAT_PATH_CAP];
    CHECK(child(runtime_resources, sizeof(runtime_resources), package,
        "Resources", true));
    CHECK(child(backup, sizeof(backup), runtime_resources, "cat.ttf", false));
    CHECK(write_text(backup, "runtime font"));
    CHECK(child(backup, sizeof(backup), runtime_resources,
        "l2dlogo.png", false));
    CHECK(write_text(backup, "runtime logo"));
    char authored_resources[BONGO_CAT_PATH_CAP];
    CHECK(child(authored_resources, sizeof(authored_resources), package,
        "img/standard/cat_model/resources", true));
    CHECK(child(backup, sizeof(backup), authored_resources, "keep.bin", false));
    CHECK(write_text(backup, "model resource"));
    CHECK(child(bundle, sizeof(bundle), root, "bundle", true));
    CHECK(child(nested_package, sizeof(nested_package), bundle, "model", true));
    CHECK(mver_fixture(nested_package));
    char nested_config[BONGO_CAT_PATH_CAP], slim_config[BONGO_CAT_PATH_CAP];
    CHECK(child(nested_config, sizeof(nested_config), nested_package,
        "config.json", false));
    CHECK(child(slim_config, sizeof(slim_config), nested_package,
        BONGO_CAT_SKIN_CONFIG_FILE, false));
    CHECK(SDL_RenamePath(nested_config, slim_config));
    CHECK(child(variant, sizeof(variant), bundle, "variant", true));
    CHECK(child(variant_model, sizeof(variant_model), variant, "model", true));
    CHECK(child(variant_img, sizeof(variant_img), variant_model, "img", true));
    CHECK(child(variant_standard, sizeof(variant_standard), variant_img,
        "standard", true));
    CHECK(child(variant_hand, sizeof(variant_hand), variant_standard,
        "hand", true));
    CHECK(child(source_hand, sizeof(source_hand), nested_package,
        "img/standard/hand/0.png", false));
    CHECK(child(mode, sizeof(mode), variant_hand, "0.png", false) &&
        SDL_CopyFile(source_hand, mode));
    CHECK(child(backup, sizeof(backup), variant_standard,
        "override_bak", false));
    CHECK(write_text(backup, "backup"));
    char normalized[BONGO_CAT_PATH_CAP];
    BongoCatError source_error = {0};
    CHECK(bongo_cat_import_source_directory(mode, normalized,
        sizeof(normalized), &source_error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, variant_model) == 0);
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_mver_discover_exact(package, &discovery, &error) == 1);
    CHECK(discovery.count == 1);
    CHECK(child(mode, sizeof(mode), package, "img/standard", false));
    BongoCatImportDiscovery nested = {0};
    CHECK(bongo_cat_import_mver_discover_exact(mode, &nested, &error) == 0);
    CHECK(bongo_cat_import_mver_discover(mode, &nested, &error) == 1);
    BongoCatImportDiscovery selected_container = {0};
    CHECK(bongo_cat_import_discover(root, &selected_container, &error));
    CHECK(selected_container.count == 3);
    size_t patch_count = 0;
    const BongoCatImportCandidate *contained_patch = NULL;
    for (size_t i = 0; i < selected_container.count; ++i) {
        if (selected_container.candidates[i].format ==
            BONGO_CAT_IMPORT_TAURI) continue;
        /* Each recursive candidate owns its package tree. The selected
           container is only a discovery scope, never a copy source. */
        CHECK(strcmp(selected_container.candidates[i].package_root,
            root) != 0);
    }
    for (size_t i = 0; i < selected_container.count; ++i)
        if (selected_container.candidates[i].format ==
            BONGO_CAT_IMPORT_MVER_PATCH) {
            patch_count++;
            contained_patch = &selected_container.candidates[i];
        }
    CHECK(patch_count == 1);
    BongoCatImportDiscovery filtered = selected_container;
    BongoCatPackageMetadata filtered_metadata[
        BONGO_CAT_IMPORT_CANDIDATE_CAP] = {0};
    CHECK(bongo_cat_import_prepare_package_metadata(&filtered,
        filtered_metadata, &error));
    CHECK(filtered.count == 2);
    for (size_t i = 0; i < filtered.count; ++i)
        CHECK(filtered.candidates[i].format == BONGO_CAT_IMPORT_MVER);
    char installed_root[BONGO_CAT_PATH_CAP];
    snprintf(installed_root, sizeof(installed_root),
        "%s/bongocat-container-installed-%llu", temporary, stamp);
    BongoCatImportCandidate installed = {0};
    CHECK(contained_patch && bongo_cat_import_prepare_package(contained_patch,
        installed_root, &installed, &error));
    CHECK(strncmp(installed.overrides, installed_root,
        strlen(installed_root)) == 0);
    CHECK(child(mode, sizeof(mode), installed.overrides, "hand/0.png", false) &&
        bongo_cat_path_is_file(mode));
    CHECK(child(backup, sizeof(backup), installed_root,
        "app/config.json.before_console_bak", false) &&
        !bongo_cat_path_is_file(backup));

    BongoCatImportDiscovery external_patch = {0};
    CHECK(bongo_cat_import_mver_patch_discover(variant, &external_patch,
        &error) == 1);
    BongoCatImportDiscovery standalone_patch = external_patch;
    BongoCatPackageMetadata standalone_metadata[
        BONGO_CAT_IMPORT_CANDIDATE_CAP] = {0};
    CHECK(bongo_cat_import_prepare_package_metadata(&standalone_patch,
        standalone_metadata, &error));
    CHECK(standalone_patch.count == 1 &&
        standalone_patch.candidates[0].format == BONGO_CAT_IMPORT_MVER_PATCH);
    char external_root[BONGO_CAT_PATH_CAP];
    snprintf(external_root, sizeof(external_root),
        "%s/bongocat-container-external-%llu", temporary, stamp);
    installed = (BongoCatImportCandidate){0};
    CHECK(external_patch.count == 1 &&
        bongo_cat_import_prepare_package(&external_patch.candidates[0],
            external_root, &installed, &error));
    CHECK(strncmp(installed.overrides, external_root,
        strlen(external_root)) == 0);
    CHECK(child(mode, sizeof(mode), installed.overrides, "hand/0.png", false) &&
        bongo_cat_path_is_file(mode));
    CHECK(child(backup, sizeof(backup), installed.overrides,
        "override_bak", false) && !bongo_cat_path_is_file(backup));

    char progressive_root[BONGO_CAT_PATH_CAP];
    snprintf(progressive_root, sizeof(progressive_root),
        "%s/bongocat-container-progressive-%llu", temporary, stamp);
    CHECK(SDL_CreateDirectory(progressive_root));
    ProgressiveImportLog progressive = {0};
    BongoCatImportSession *session = bongo_cat_import_session_create(
        progressive_root, &error);
    BongoCatImportBatchStats stats = {0};
    CHECK(session != NULL);
    CHECK(session && bongo_cat_import_session_install_progressive(session,
        root, progressive_receipt, &progressive, &stats, &error) ==
        BONGO_CAT_OK);
    CHECK(progressive.callbacks == 2 && progressive.resolved == 2 &&
        progressive.installed == 2);
    CHECK(stats.succeeded_count == 2 && stats.failed_count == 0);
    CHECK(child(mode, sizeof(mode), package,
        "img/standard/cat_model/cat.moc3", false));
    progressive = (ProgressiveImportLog){0};
    CHECK(session && bongo_cat_import_session_install_progressive(session,
        mode, progressive_receipt, &progressive, &stats, &error) ==
        BONGO_CAT_OK);
    CHECK(progressive.callbacks == 1 && progressive.resolved == 1 &&
        progressive.installed == 0);
    CHECK(stats.succeeded_count == 1 && stats.failed_count == 0);
    char missing_source[BONGO_CAT_PATH_CAP];
    CHECK(child(missing_source, sizeof(missing_source), progressive_root,
        "missing", false));
    CHECK(bongo_cat_import_session_install_progressive(session,
        missing_source, progressive_receipt, &progressive, &stats, &error) ==
        BONGO_CAT_ERROR_ARGUMENT);
    CHECK(stats.succeeded_count == 0 && stats.failed_count == 1 &&
        stats.failure_name_count == 1 && strcmp(stats.failure_names[0],
            "missing") == 0);
    bongo_cat_import_session_destroy(session);

    char models_root[BONGO_CAT_PATH_CAP], cache_root[BONGO_CAT_PATH_CAP];
    snprintf(models_root, sizeof(models_root),
        "%s/bongocat-container-models-%llu", temporary, stamp);
    snprintf(cache_root, sizeof(cache_root),
        "%s/bongocat-container-cache-%llu", temporary, stamp);
    CHECK(SDL_CreateDirectory(models_root));
    CHECK(SDL_CreateDirectory(cache_root));
    BongoCatImportReceipt receipt = {0};
    CHECK(bongo_cat_import_install(root, models_root, &receipt, &error) ==
        BONGO_CAT_OK);
    CHECK(receipt.count == 2 && receipt.installed_count == 2);
    BongoCatImportReceipt subset_receipt = {0};
    CHECK(bongo_cat_import_install(package, models_root, &subset_receipt,
        &error) == BONGO_CAT_OK);
    CHECK(subset_receipt.count == 1 && subset_receipt.installed_count == 0 &&
        strcmp(subset_receipt.ids[0], receipt.ids[0]) == 0);
    CHECK(child(mode, sizeof(mode), package,
        "img/standard/cat_model/cat.moc3", false));
    BongoCatImportReceipt moc_receipt = {0};
    CHECK(bongo_cat_import_install(mode, models_root, &moc_receipt,
        &error) == BONGO_CAT_OK);
    CHECK(moc_receipt.count == subset_receipt.count &&
        moc_receipt.installed_count == 0 &&
        strcmp(moc_receipt.ids[0], subset_receipt.ids[0]) == 0);
    char stored[BONGO_CAT_PATH_CAP], duplicate_directory[BONGO_CAT_PATH_CAP];
    CHECK(child(stored, sizeof(stored), models_root, receipt.ids[0], false) &&
        bongo_cat_path_is_dir(stored));
    CHECK(child(duplicate_directory, sizeof(duplicate_directory), models_root,
        receipt.ids[1], false) && !bongo_cat_path_is_dir(duplicate_directory));
    CHECK(child(backup, sizeof(backup), stored,
        ".bongo-cat-package.json", false) && !bongo_cat_path_is_file(backup));
    CHECK(child(backup, sizeof(backup), stored,
        ".bongo-cat-adapter", false) && !bongo_cat_path_is_dir(backup));
    CHECK(child(backup, sizeof(backup), stored,
        BONGO_CAT_MODEL_ADAPTER_FILE, false) &&
        !bongo_cat_path_is_file(backup));
    CHECK(child(backup, sizeof(backup), stored, "app/Resources", false) &&
        !bongo_cat_path_is_dir(backup));
    CHECK(child(backup, sizeof(backup), stored,
        "app/img/standard/cat_model/resources/keep.bin", false) &&
        bongo_cat_path_is_file(backup));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        snprintf(app->models_root, sizeof(app->models_root), "%s", models_root);
        snprintf(app->cache_root, sizeof(app->cache_root), "%s", cache_root);
        bongo_cat_models_init(&app->models);
        CHECK(bongo_cat_import_installed_models(app, models_root, &error) ==
            BONGO_CAT_OK);
        CHECK(app->models.count == receipt.count);
        for (size_t i = 0; i < app->models.count; ++i)
            CHECK(strcmp(app->models.entries[i].storage_directory, stored) == 0);
        CHECK(bongo_cat_settings_set_model_removed(&app->settings,
            receipt.ids[0], true));
        bongo_cat_models_init(&app->models);
        CHECK(bongo_cat_import_installed_models(app, models_root, &error) ==
            BONGO_CAT_OK);
        CHECK(app->models.count + 1 == receipt.count);
        CHECK(!bongo_cat_models_find(&app->models, receipt.ids[0]));
        CHECK(bongo_cat_models_find(&app->models, receipt.ids[1]));
        CHECK(bongo_cat_path_is_dir(stored));
        CHECK(bongo_cat_settings_restore_model_package(&app->settings,
            receipt.ids[0]));
        bongo_cat_models_init(&app->models);
        CHECK(bongo_cat_import_installed_models(app, models_root, &error) ==
            BONGO_CAT_OK);
        CHECK(app->models.count == receipt.count &&
            bongo_cat_models_find(&app->models, receipt.ids[0]));
        free(app);
    }
    CHECK(bongo_cat_model_remove_tree(models_root, NULL));
    CHECK(bongo_cat_model_remove_tree(cache_root, NULL));
    CHECK(bongo_cat_model_remove_tree(progressive_root, NULL));
    CHECK(bongo_cat_model_remove_tree(installed_root, NULL));
    CHECK(bongo_cat_model_remove_tree(external_root, NULL));
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}
