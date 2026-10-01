#include "test_mver_support.h"

#include "model_import.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

static bool model_displayed(const BongoCatApp *app, const char *name) {
    for (size_t i = 0; app && i < app->models.count; ++i)
        if (strcmp(app->models.entries[i].display_name, name) == 0)
            return true;
    return false;
}

static bool wait_for_model(BongoCatApp *app, const char *name, bool visible) {
    uint64_t deadline = SDL_GetTicksNS() + 5000000000ull;
    while (model_displayed(app, name) != visible &&
        SDL_GetTicksNS() < deadline) {
        bongo_cat_model_refresh_update(app);
        SDL_Delay(2);
    }
    return model_displayed(app, name) == visible;
}

static bool broken_expression_fixture(const char *root) {
    char path[BONGO_CAT_PATH_CAP];
    return mver_fixture(root) &&
        child(path, sizeof(path), root, "config.json", false) &&
        write_text(path, "{\"standard\":{\"l2d\":true,\"keyboard\":[[65]],"
            "\"hand\":[[65]],\"l2d_expression\":[[999999]]}}") &&
        child(path, sizeof(path), root,
            "img/standard/cat_model/cat.model3.json", false) &&
        write_text(path, "{\"Version\":3,\"FileReferences\":{"
            "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"],"
            "\"Expressions\":[{\"Name\":\"bad\",\"File\":\"bad.exp3.json\"}]}}") &&
        child(path, sizeof(path), root,
            "img/standard/cat_model/bad.exp3.json", false) &&
        write_text(path, "{\"Type\":\"Live2D Expression\",\"Parameters\":[]}");
}

static void catalog_failure_isolation(const char *temporary) {
    char root[BONGO_CAT_PATH_CAP], models[BONGO_CAT_PATH_CAP];
    char nearby[BONGO_CAT_PATH_CAP], bad[BONGO_CAT_PATH_CAP];
    char good[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-scan-failure-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(models, sizeof(models), root, "models", true));
    CHECK(child(nearby, sizeof(nearby), root, "nearby", true));
    CHECK(child(bad, sizeof(bad), models, "a-broken", false));
    CHECK(broken_expression_fixture(bad));
    CHECK(child(good, sizeof(good), models, "z-valid", false));
    CHECK(mver_fixture(good));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    snprintf(app->asset_root, sizeof(app->asset_root),
        "%s/resources/assets", BONGO_CAT_NATIVE_SOURCE_DIR);
    snprintf(app->models_root, sizeof(app->models_root), "%s", models);
    CHECK(child(app->cache_root, sizeof(app->cache_root), root, "cache", true));
    snprintf(app->nearby_root, sizeof(app->nearby_root), "%s", nearby);

    /* Verify that this fixture actually exercises the reported failure. */
    BongoCatError error = {0};
    CHECK(bongo_cat_import_nearby_root(app, bad, &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(strstr(error.message, "not a supported input chord") != NULL);
    snprintf(app->session.active_model_id, sizeof(app->session.active_model_id),
        "a-broken");
    app->settings.behavior_shortcut_count = 1;
    snprintf(app->settings.behavior_shortcuts[0].id,
        sizeof(app->settings.behavior_shortcuts[0].id), "a-broken:expression:0");
    snprintf(app->settings.behavior_shortcuts[0].shortcut,
        sizeof(app->settings.behavior_shortcuts[0].shortcut), "Alt+1");
    bongo_cat_app_rescan_models(app);
    CHECK(app->models.count == 4);
    CHECK(bongo_cat_models_find(&app->models, "standard") != NULL);
    CHECK(model_displayed(app, "z-valid"));
    CHECK(!model_displayed(app, "a-broken"));
    CHECK(strcmp(app->session.active_model_id, "standard") == 0);
    CHECK(app->settings.behavior_shortcut_count == 1);
    CHECK(strcmp(app->settings.behavior_shortcuts[0].shortcut, "Alt+1") == 0);

    CHECK(child(bad, sizeof(bad), nearby, "a-broken-nearby", false));
    CHECK(broken_expression_fixture(bad));
    CHECK(child(good, sizeof(good), nearby, "z-valid-nearby", false));
    CHECK(mver_fixture(good));
    CHECK(child(path, sizeof(path), good, "img/standard/cat_model/cat.moc3", false));
    CHECK(write_text(path, "MOC3-distinct-nearby"));
    bongo_cat_app_refresh_nearby_models(app);
    CHECK(app->models.count == 5);
    CHECK(model_displayed(app, "z-valid-nearby"));
    CHECK(!model_displayed(app, "a-broken-nearby"));
    bongo_cat_app_request_nearby_model_refresh(app);
    uint64_t deadline = SDL_GetTicksNS() + 5000000000ull;
    while (bongo_cat_app_model_refresh_busy(app) && SDL_GetTicksNS() < deadline) {
        bongo_cat_model_refresh_update(app);
        SDL_Delay(2);
    }
    CHECK(!bongo_cat_app_model_refresh_busy(app));
    CHECK(app->models.count == 5);
    CHECK(model_displayed(app, "z-valid"));
    CHECK(model_displayed(app, "z-valid-nearby"));
    bongo_cat_model_refresh_shutdown(app);
    CHECK(app->settings.behavior_shortcut_count == 1);

    /* A file at the cache/storage path deterministically simulates an
       unavailable directory without depending on platform ACLs. */
    CHECK(child(path, sizeof(path), root, "blocked", false));
    CHECK(write_text(path, "blocked"));
    snprintf(app->cache_root, sizeof(app->cache_root), "%s", path);
    CHECK(bongo_cat_model_catalog_scan(app, false, nearby) == BONGO_CAT_OK);
    CHECK(app->models.count == 3);
    snprintf(app->models_root, sizeof(app->models_root), "%s", path);
    CHECK(bongo_cat_model_catalog_scan(app, true, nearby) == BONGO_CAT_OK);
    CHECK(app->models.count == 3);
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, "standard");
    CHECK(entry && strstr(entry->directory, "resources/assets/models") != NULL);
    snprintf(app->models_root, sizeof(app->models_root), "%s", models);
    CHECK(bongo_cat_model_catalog_scan(app, false, NULL) == BONGO_CAT_OK);
    CHECK(bongo_cat_model_catalog_add_bundled(app, true));
    entry = bongo_cat_models_find(&app->models, "standard");
    CHECK(entry && strstr(entry->directory, "resources/assets/models") != NULL);
    CHECK(app->models.count == 3);
    free(app);
cleanup:
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}

static void background_installed_refresh(const char *temporary) {
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], models[BONGO_CAT_PATH_CAP];
    char cache[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-owned-source-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongocat-owned-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(data));
    CHECK(mver_fixture(root));
    CHECK(child(source, sizeof(source), root, "config.json", false));
    CHECK(child(models, sizeof(models), data, "models", true));
    CHECK(child(cache, sizeof(cache), data, "cache", true));
    BongoCatImportReceipt receipt = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_install(source, models, &receipt, &error) ==
        BONGO_CAT_OK && receipt.count == 1);

    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        bongo_cat_settings_defaults(&app->settings);
        bongo_cat_session_defaults(&app->session);
        snprintf(app->asset_root, sizeof(app->asset_root),
            "%s/resources/assets", BONGO_CAT_NATIVE_SOURCE_DIR);
        snprintf(app->models_root, sizeof(app->models_root), "%s", models);
        snprintf(app->cache_root, sizeof(app->cache_root), "%s", cache);
        snprintf(app->models.entries[0].id,
            sizeof(app->models.entries[0].id), "unrelated-model");
        app->models.entries[0].managed = true;
        app->models.count = 1;
        bongo_cat_app_request_model_package_refresh(app, receipt.ids[0]);
        CHECK(wait_for_model(app, bongo_cat_path_name(root), true));
        CHECK(bongo_cat_models_find(&app->models, "unrelated-model") != NULL);
        CHECK(bongo_cat_settings_set_model_removed(&app->settings,
            receipt.ids[0], true));
        bongo_cat_app_request_model_package_refresh(app, receipt.ids[0]);
        CHECK(wait_for_model(app, bongo_cat_path_name(root), false));
        CHECK(bongo_cat_models_find(&app->models, "unrelated-model") != NULL);
        bongo_cat_model_refresh_shutdown(app);
        free(app);
    }
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
}

int test_mver_nearby_identity(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], moved[BONGO_CAT_PATH_CAP];
    char again[BONGO_CAT_PATH_CAP], duplicate[BONGO_CAT_PATH_CAP];
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongocat-nearby-move-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongocat-nearby-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(source, sizeof(source), root, "source", false));
    CHECK(child(moved, sizeof(moved), root, "moved", false));
    CHECK(child(again, sizeof(again), root, "again", false));
    CHECK(child(duplicate, sizeof(duplicate), root, "duplicate", false));
    CHECK(mver_fixture(source));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_models_init(&app->models);
    snprintf(app->models_root, sizeof(app->models_root), "%s", data);
    snprintf(app->cache_root, sizeof(app->cache_root), "%s", data);
    BongoCatError error = {0};
    CHECK(bongo_cat_import_nearby_root(app, source, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    char old_id[BONGO_CAT_ID_CAP];
    snprintf(old_id, sizeof(old_id), "%s", app->models.entries[0].id);
    CHECK(bongo_cat_settings_set_model_label(&app->settings, old_id,
        "Nearby custom name"));
    BongoCatBehaviorShortcut *binding =
        &app->settings.behavior_shortcuts[app->settings.behavior_shortcut_count++];
    snprintf(binding->id, sizeof(binding->id), "%s:expression:2", old_id);
    snprintf(binding->shortcut, sizeof(binding->shortcut), "Alt+3");
    snprintf(binding->label, sizeof(binding->label), "Custom expression");
    CHECK(bongo_cat_path_rename(source, moved));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_nearby_root(app, moved, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    const char *new_id = app->models.entries[0].id;
    CHECK(strcmp(old_id, new_id) != 0);
    CHECK(strcmp(app->session.active_model_id, old_id) == 0);
    CHECK(bongo_cat_settings_model_label(&app->settings, new_id) == NULL);
    CHECK(strcmp(bongo_cat_settings_model_label(&app->settings, old_id),
        "Nearby custom name") == 0);
    CHECK(app->settings.behavior_shortcut_count == 1);
    CHECK(strncmp(app->settings.behavior_shortcuts[0].id, old_id,
        strlen(old_id)) == 0);
    CHECK(strcmp(app->settings.behavior_shortcuts[0].shortcut, "Alt+3") == 0);
    char ambiguous_id[BONGO_CAT_ID_CAP];
    snprintf(ambiguous_id, sizeof(ambiguous_id), "%s", new_id);
    CHECK(bongo_cat_settings_set_model_label(&app->settings, ambiguous_id,
        "Must remain here"));
    CHECK(bongo_cat_path_rename(moved, again));
    CHECK(mver_fixture(duplicate));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_nearby_root(app, root, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    CHECK(strcmp(app->session.active_model_id, old_id) == 0);
    CHECK(strcmp(bongo_cat_settings_model_label(&app->settings, ambiguous_id),
        "Must remain here") == 0);
    free(app);
cleanup:
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}

int test_mver_nearby_refresh(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) {
        SDL_free(temporary);
        return failures;
    }
    background_installed_refresh(temporary);
    catalog_failure_isolation(temporary);

    BongoCatApp *parsed = calloc(1, sizeof(*parsed));
    CHECK(parsed != NULL);
    if (parsed) {
        char nearby_argument[BONGO_CAT_PATH_CAP + 20];
        snprintf(nearby_argument, sizeof(nearby_argument),
            "--nearby-root=%s", temporary);
        char *arguments[] = {"BongoCat", nearby_argument};
        BongoCatError argument_error = {0};
        CHECK(bongo_cat_startup_arguments(parsed, 2, arguments,
            &argument_error));
        CHECK(strcmp(parsed->nearby_root, temporary) == 0);
        free(parsed);
    }

    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char startup_source[BONGO_CAT_PATH_CAP], sync_source[BONGO_CAT_PATH_CAP];
    char background_source[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-nearby-root-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongocat-nearby-refresh-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    char name[96];
    snprintf(name, sizeof(name), "startup-source-%llu", nonce);
    CHECK(child(startup_source, sizeof(startup_source), root, name, false));
    snprintf(name, sizeof(name), "sync-source-%llu", nonce);
    CHECK(child(sync_source, sizeof(sync_source), root, name, false));
    snprintf(name, sizeof(name), "background-source-%llu", nonce);
    CHECK(child(background_source, sizeof(background_source), root, name, false));
    CHECK(mver_fixture(startup_source));

    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_models_init(&app->models);
    snprintf(app->asset_root, sizeof(app->asset_root),
        "%s/resources/assets", BONGO_CAT_NATIVE_SOURCE_DIR);
    snprintf(app->models_root, sizeof(app->models_root), "%s", data);
    snprintf(app->cache_root, sizeof(app->cache_root), "%s", data);
    snprintf(app->nearby_root, sizeof(app->nearby_root), "%s", root);

    bongo_cat_app_rescan_models(app);
    CHECK(model_displayed(app, bongo_cat_path_name(startup_source)));
    CHECK(mver_fixture(sync_source));
    bongo_cat_app_refresh_nearby_models(app);
    CHECK(model_displayed(app, bongo_cat_path_name(startup_source)) ||
        model_displayed(app, bongo_cat_path_name(sync_source)));
    CHECK(mver_fixture(background_source));
    bongo_cat_app_request_nearby_model_refresh(app);
    /* A synchronous catalog mutation invalidates the in-flight snapshot. The
       refresh worker must discard it, rerun, and commit only the newer scan. */
    bongo_cat_app_refresh_installed_models(app);
    uint64_t deadline = SDL_GetTicksNS() + 5000000000ull;
    while (!model_displayed(app, bongo_cat_path_name(background_source)) &&
        SDL_GetTicksNS() < deadline) {
        bongo_cat_model_refresh_update(app);
        SDL_Delay(2);
    }
    CHECK(model_displayed(app, bongo_cat_path_name(background_source)));
    bongo_cat_model_refresh_shutdown(app);
    free(app);

cleanup:
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
