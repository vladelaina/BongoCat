#include "test_mver_support.h"

#include "model_import.h"
#include "model_import_mver.h"
#include "model_storage.h"
#include "bongo_cat/json.h"
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

static void optional_mode_inputs(const char *temporary) {
    const char *names[] = {"standard", "keyboard", "gamepad"};
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    CHECK(discovery != NULL);
    if (!discovery) return;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char root[BONGO_CAT_PATH_CAP], standard[BONGO_CAT_PATH_CAP];
        char mode[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
        char adapter[BONGO_CAT_PATH_CAP], config[512];
        snprintf(root, sizeof(root), "%s/bongocat-optional-%s-%llu", temporary,
            names[i], (unsigned long long)SDL_GetTicksNS());
        CHECK(mver_fixture(root));
        CHECK(child(standard, sizeof(standard), root, "img/standard", false));
        CHECK(child(mode, sizeof(mode), root, "img", false));
        CHECK(child(path, sizeof(path), mode, names[i], false));
        if (i) CHECK(SDL_RenamePath(standard, path));
        snprintf(mode, sizeof(mode), "%s", path);
        CHECK(child(path, sizeof(path), mode, "hand", false));
        CHECK(bongo_cat_model_remove_tree(path, NULL));
        const char *disabled = i ? "standard" : "keyboard";
        CHECK(child(path, sizeof(path), root, "img", false));
        CHECK(child(standard, sizeof(standard), path, disabled, true));
        snprintf(config, sizeof(config), "{\"%s\":{\"l2d\":true,"
            "\"hand\":null,\"lefthand\":null,\"sounds\":null,"
            "\"l2d_expression\":null,\"l2d_motion\":null},"
            "\"%s\":{\"l2d\":false,\"hand\":42,\"lefthand\":42}}", names[i], disabled);
        CHECK(child(path, sizeof(path), root, "config.json", false));
        CHECK(write_text(path, config));
        memset(discovery, 0, sizeof(*discovery));
        BongoCatError error = {0};
        CHECK(bongo_cat_import_mver_discover_exact(root, discovery, &error) == 1);
        CHECK(discovery->count == 1);
        CHECK(child(adapter, sizeof(adapter), root, "adapter", false));
        CHECK(bongo_cat_import_prepare_adapter(&discovery->candidates[0], adapter, &error));
        char report_path[BONGO_CAT_PATH_CAP];
        CHECK(child(report_path, sizeof(report_path), adapter,
            ".bongo-cat-import-report.json", false));
        yyjson_doc *report = bongo_cat_json_read_file(report_path, 0, NULL);
        yyjson_val *assets = yyjson_obj_get(yyjson_doc_get_root(report), "assets");
        CHECK(report && yyjson_is_obj(assets));
        CHECK(!yyjson_obj_get(assets, "hand") &&
            !yyjson_obj_get(assets, "lefthand") &&
            !yyjson_obj_get(assets, "sounds"));
        yyjson_doc_free(report);
        size_t length = 0;
        char *original = SDL_LoadFile(path, &length);
        CHECK(original && length == strlen(config) && memcmp(original, config, length) == 0);
        SDL_free(original);
        CHECK(bongo_cat_model_remove_tree(root, NULL));
    }
    free(discovery);
}

static void empty_optional_bindings(const char *temporary) {
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    char adapter[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-empty-bindings-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(mver_fixture(root));
    CHECK(child(path, sizeof(path), root, "config.json", false));
    CHECK(write_text(path, "{\"decoration\":{\"emoticonKeep\":true,\"emoticonClear\":[]},"
        "\"standard\":{\"l2d\":true,\"hand\":[[65]],\"keyboard\":[[65]],"
        "\"l2d_expression\":[[],[999999],[65]],"
        "\"l2d_motion\":[null,[999999],[66]],\"face\":[[],null,[67],[999999]],"
        "\"sounds\":[[],null,[68],[999999]]}}"));
    CHECK(child(path, sizeof(path), root, "img/standard/cat_model/cat.model3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{\"Moc\":\"cat.moc3\","
        "\"Textures\":[\"texture.png\"],\"Expressions\":["
        "{\"Name\":\"a\",\"File\":\"a.exp3.json\"},"
        "{\"Name\":\"b\",\"File\":\"b.exp3.json\"},"
        "{\"Name\":\"c\",\"File\":\"c.exp3.json\"}],"
        "\"Motions\":{\"CAT_motion\":[{\"File\":\"a.motion3.json\"},"
        "{\"File\":\"b.motion3.json\"},{\"File\":\"c.motion3.json\"}]}}}"));
    CHECK(child(path, sizeof(path), root, "img/standard/cat_model/c.exp3.json", false));
    CHECK(write_text(path, "{\"Type\":\"Live2D Expression\",\"Parameters\":[]}"));
    CHECK(child(path, sizeof(path), root, "img/standard/cat_model/c.motion3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"Meta\":{\"Duration\":1},\"Curves\":[]}"));
    CHECK(child(path, sizeof(path), root, "img/standard/keyboard", true));
    CHECK(child(path, sizeof(path), root, "img/standard/keyboard/0.png", false));
    CHECK(write_text(path, "unreadable optional image"));
    CHECK(child(path, sizeof(path), root, "img/standard/face", true));
    CHECK(child(path, sizeof(path), root, "img/standard/face/2.png", false));
    CHECK(bongo_cat_path_copy_file(BONGO_CAT_NATIVE_SOURCE_DIR
        "/resources/assets/bongocat.png", path));
    CHECK(child(path, sizeof(path), root, "img/standard/face/3.png", false));
    CHECK(write_text(path, "unreadable optional image"));
    CHECK(child(path, sizeof(path), root, "img/standard/sounds", true));
    CHECK(child(path, sizeof(path), root, "img/standard/sounds/2.flac", false));
    CHECK(bongo_cat_path_copy_file(BONGO_CAT_NATIVE_SOURCE_DIR
        "/resources/assets/models/standard/live2d_motion1.flac", path));
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    CHECK(discovery != NULL);
    if (discovery) {
        BongoCatError error = {0};
        CHECK(bongo_cat_import_mver_discover_exact(root, discovery, &error) == 1);
        CHECK(child(adapter, sizeof(adapter), root, "adapter", false));
        CHECK(bongo_cat_import_prepare_adapter(&discovery->candidates[0], adapter, &error));
        CHECK(child(path, sizeof(path), adapter, BONGO_CAT_MODEL_ADAPTER_FILE, false));
        yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
        yyjson_val *bindings = yyjson_obj_get(yyjson_doc_get_root(document), "bindings");
        CHECK(document && yyjson_arr_size(bindings) == 4);
        CHECK(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(bindings, 0), "index")) == 2);
        CHECK(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(bindings, 1), "index")) == 2);
        yyjson_doc_free(document);
        CHECK(child(path, sizeof(path), adapter, "resources/left-keys/KeyA.png", false));
        CHECK(bongo_cat_path_is_file(path));
        CHECK(child(path, sizeof(path), root, "img/standard/hand/0.png", false));
        CHECK(write_text(path, "unreadable optional hand"));
        CHECK(child(adapter, sizeof(adapter), root, "adapter-without-hand", false));
        CHECK(bongo_cat_import_prepare_adapter(&discovery->candidates[0], adapter, &error));
        CHECK(child(path, sizeof(path), adapter, "resources/left-keys/KeyA.png", false));
        CHECK(!bongo_cat_path_is_file(path));
        free(discovery);
    }
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}

int test_mver_missing_motion_groups(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    optional_mode_inputs(temporary);
    empty_optional_bindings(temporary);
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    char source[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char path[BONGO_CAT_PATH_CAP];
    snprintf(source, sizeof(source), "%s/bongocat-stale-motion-%llu",
        temporary, stamp);
    snprintf(data, sizeof(data), "%s/bongocat-stale-motion-data-%llu",
        temporary, stamp);
    CHECK(mver_fixture(source));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(path, sizeof(path), source, "config.json", false));
    CHECK(write_text(path, "{\"standard\":{\"hand\":[[65]],"
        "\"keyboard\":[[65]],\"l2d_motion\":[[67]],"
        "\"l2d_motion_lockhand\":[[68]]}}"));
    BongoCatImportReceipt receipt = {0};
    BongoCatError error = {0};
    char models_root[BONGO_CAT_PATH_CAP];
    CHECK(child(models_root, sizeof(models_root), data, "models", true));
    CHECK(bongo_cat_import_install(source, models_root, &receipt, &error) ==
        BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1);
    CHECK(bongo_cat_model_remove_tree(source, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
