#include "model_import.h"
#include "model_import_manifest.h"
#include "mver/model_import_mver_manifest.h"
#include "model_storage.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *broken =
    "{\"Version\":3,\"FileReferences\":{\"Moc\":\"cat.moc3\","
    "\"Textures\":[\"texture.png\"]\n]\n}\n},"
    "\"Groups\":[{\"Target\":\"Parameter\",\"Name\":\"EyeBlink\","
    "\"Ids\":[\"ParamEyeLOpen\"]}]}";

void test_mver_manifest(void) {
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char model[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    char models[BONGO_CAT_PATH_CAP], stored[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-manifest-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "source", true));
    CHECK(mver_fixture(package));
    CHECK(child(model, sizeof(model), package, "img/standard/cat_model", false));
    CHECK(child(path, sizeof(path), model, "cat.model3.json", false));
    CHECK(write_text(path, broken));
    bool repaired = false;
    yyjson_doc *document = bongo_cat_import_mver_manifest_read(path, &repaired);
    CHECK(document && repaired);
    CHECK(yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document),
        "Groups")) == 1);
    yyjson_doc_free(document);
    CHECK(!bongo_cat_import_manifest_valid(model, "cat.model3.json", NULL));
    CHECK(bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    CHECK(child(models, sizeof(models), root, "models", true));
    BongoCatError error = {0};
    BongoCatImportReceipt receipt = {0}, duplicate = {0};
    CHECK(bongo_cat_import_install(package, models, &receipt, &error) == BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1);
    CHECK(child(stored, sizeof(stored), models, receipt.ids[0], false));
    CHECK(child(model, sizeof(model), stored, "img/standard/cat_model", false));
    CHECK(!bongo_cat_import_manifest_valid(model, "cat.model3.json", NULL));
    CHECK(bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    char installed_manifest[BONGO_CAT_PATH_CAP];
    CHECK(child(installed_manifest, sizeof(installed_manifest), model,
        "cat.model3.json", false));
    size_t installed_length = 0;
    char *installed_json = SDL_LoadFile(installed_manifest, &installed_length);
    CHECK(installed_json && installed_length == strlen(broken) &&
        memcmp(installed_json, broken, installed_length) == 0);
    SDL_free(installed_json);
    BongoCatBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    BongoCatModelEntry *entry = calloc(1, sizeof(*entry));
    CHECK(behaviors && entry);
    if (behaviors && entry) {
        snprintf(entry->directory, sizeof(entry->directory), "%s", model);
        snprintf(entry->setting_file, sizeof(entry->setting_file), "cat.model3.json");
        CHECK(bongo_cat_behaviors_load(behaviors, entry, &error) == BONGO_CAT_OK);
    }
    free(behaviors);
    free(entry);
    CHECK(bongo_cat_import_install(package, models, &duplicate, &error) == BONGO_CAT_OK);
    CHECK(duplicate.count == 1 && duplicate.installed_count == 0 &&
        strcmp(receipt.ids[0], duplicate.ids[0]) == 0);
    size_t length = 0;
    char *original = SDL_LoadFile(path, &length);
    CHECK(original && length == strlen(broken) && strcmp(original, broken) == 0);
    SDL_free(original);

    CHECK(write_text(path, "{/* comment */\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\",],},}"));
    document = bongo_cat_import_mver_manifest_read(path, &repaired);
    CHECK(document && repaired);
    yyjson_doc_free(document);
    static const char bom_manifest[] = "\xEF\xBB\xBF"
        "{\"Version\":3,\"FileReferences\":{\"Moc\":\"cat.moc3\","
        "\"Textures\":[\"texture.png\"]}}";
    document = bongo_cat_model_json_parse(bom_manifest,
        sizeof(bom_manifest) - 1, &repaired);
    CHECK(document && repaired);
    yyjson_doc_free(document);
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"missing.png\"]]}}}"));
    CHECK(child(model, sizeof(model), package, "img/standard/cat_model", false));
    CHECK(!bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    static const char *const rejected[] = {
        "{\"Version\":3,\"FileReferences\":{",
        "{\"Version\":3 \"FileReferences\":{}}",
        "{\"Version\":3,\"FileReferences\":{}} trailing garbage"
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        CHECK(write_text(path, rejected[i]));
        document = bongo_cat_import_mver_manifest_read(path, &repaired);
        CHECK(!document && !repaired);
        yyjson_doc_free(document);
    }
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"]}}"));
    document = bongo_cat_import_mver_manifest_read(path, &repaired);
    CHECK(document && !repaired);
    yyjson_doc_free(document);
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"],"
        "\"Physics\":\"missing.physics3.json\",\"Pose\":\"\","
        "\"DisplayInfo\":\"missing.cdi3.json\",\"UserData\":\"missing.userdata3.json\","
        "\"Expressions\":[{\"Name\":\"missing\",\"File\":\"missing.exp3.json\"}],"
        "\"Motions\":{\"Idle\":[{\"File\":\"missing.motion3.json\",\"Sound\":\"\"}]}}}"));
    CHECK(!bongo_cat_import_manifest_valid(model, "cat.model3.json", NULL));
    CHECK(bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"],"
        "\"UserData\":\"../outside.json\"}}"));
    CHECK(!bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"missing.moc3\",\"Textures\":[\"texture.png\"]}}"));
    CHECK(!bongo_cat_import_mver_manifest_valid(model, "cat.model3.json"));
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}
