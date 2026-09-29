#include "model_import.h"
#include "model_import_manifest.h"
#include "mver/model_import_mver.h"
#include "mver/model_import_mver_manifest.h"
#include "model_storage.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "bongo_cat/json.h"
#include "bongo_cat/file.h"
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

static void multiple_mver_manifests(void) {
    char *temporary = SDL_GetCurrentDirectory();
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    CHECK(temporary && discovery);
    if (!temporary || !discovery) {
        SDL_free(temporary);
        free(discovery);
        return;
    }
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char model[BONGO_CAT_PATH_CAP], canonical[BONGO_CAT_PATH_CAP];
    char exported[BONGO_CAT_PATH_CAP], extra[BONGO_CAT_PATH_CAP];
    char models[BONGO_CAT_PATH_CAP], stored[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-multiple-manifests-%llu",
        temporary, (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "source", true));
    CHECK(mver_fixture(package));
    CHECK(child(model, sizeof(model), package, "img/standard/cat_model", false));
    CHECK(child(canonical, sizeof(canonical), model, "cat.model3.json", false));
    CHECK(child(exported, sizeof(exported), model, "ys.model3.json", false));
    /* The export can carry different behavior; Mver still uses cat.model3.json. */
    static const char export_json[] =
        "{\"Version\":3,\"FileReferences\":{\"Moc\":\"cat.moc3\","
        "\"Textures\":[\"texture.png\"],\"Expressions\":["
        "{\"Name\":\"unused\",\"File\":\"unused.exp3.json\"}]}}";
    CHECK(write_text(exported, export_json));
    BongoCatError error = {0};
    CHECK(bongo_cat_import_discover(root, discovery, &error));
    CHECK(discovery->count == 1);
    CHECK(strcmp(discovery->candidates[0].setting, "cat.model3.json") == 0);
    CHECK(child(models, sizeof(models), root, "models", true));
    BongoCatImportReceipt receipt = {0};
    CHECK(bongo_cat_import_install(package, models, &receipt, &error) == BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1);
    CHECK(child(stored, sizeof(stored), models, receipt.ids[0], false));
    CHECK(bongo_cat_import_discover(stored, discovery, &error));
    CHECK(discovery->count == 1);
    CHECK(strcmp(discovery->candidates[0].setting, "cat.model3.json") == 0);
    CHECK(child(extra, sizeof(extra), stored,
        "img/standard/cat_model/ys.model3.json", false));
    size_t length = 0;
    char *copied = SDL_LoadFile(extra, &length);
    CHECK(copied && length == strlen(export_json) &&
        memcmp(copied, export_json, length) == 0);
    SDL_free(copied);
    CHECK(bongo_cat_path_is_file(canonical) && bongo_cat_path_is_file(exported));

    /* Never hide a broken canonical entry by selecting a different export. */
    CHECK(write_text(canonical, "{}"));
    memset(discovery, 0, sizeof(*discovery));
    CHECK(bongo_cat_import_mver_discover_exact(package, discovery, &error) == -1);
    CHECK(SDL_RemovePath(canonical));
    memset(discovery, 0, sizeof(*discovery));
    CHECK(bongo_cat_import_mver_discover_exact(package, discovery, &error) == 1);
    CHECK(strcmp(discovery->candidates[0].setting, "ys.model3.json") == 0);
    CHECK(child(extra, sizeof(extra), model, "other.model3.json", false));
    CHECK(write_text(extra, export_json));
    memset(discovery, 0, sizeof(*discovery));
    CHECK(bongo_cat_import_mver_discover_exact(package, discovery, &error) == -1);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
    free(discovery);
}

static void many_behaviors(const char *directory) {
    BongoCatModelEntry model = {0};
    snprintf(model.id, sizeof(model.id), "many-behaviors");
    snprintf(model.directory, sizeof(model.directory), "%s", directory);
    snprintf(model.adapter_directory, sizeof(model.adapter_directory), "%s", directory);
    snprintf(model.setting_file, sizeof(model.setting_file), "many.model3.json");
    char path[BONGO_CAT_PATH_CAP];
    CHECK(child(path, sizeof(path), directory, model.setting_file, false));
    BongoCatBehaviorCatalog catalog = {0}, copy = {0}, moved = {0};
    const size_t counts[] = {1300, BONGO_CAT_BEHAVIOR_LIMIT,
        BONGO_CAT_BEHAVIOR_LIMIT + 1, 3, 0};
    for (size_t run = 0; run < sizeof(counts) / sizeof(counts[0]); ++run) {
        FILE *file = bongo_cat_file_open(path, "wb");
        CHECK(file != NULL);
        if (!file) break;
        fputs("{\"FileReferences\":{\"Expressions\":[", file);
        for (size_t i = 0; i < counts[run]; ++i)
            fprintf(file, "%s{\"Name\":\"Expression %zu\"}", i ? "," : "", i);
        fputs("]}}", file);
        CHECK(fclose(file) == 0);
        BongoCatError error = {0};
        BongoCatBehaviorEntry *previous = catalog.entries;
        size_t previous_count = catalog.count;
        BongoCatResult result = bongo_cat_behaviors_load(&catalog, &model, &error);
        if (counts[run] > BONGO_CAT_BEHAVIOR_LIMIT) {
            CHECK(result == BONGO_CAT_ERROR_FORMAT);
            CHECK(catalog.entries == previous && catalog.count == previous_count);
            continue;
        }
        CHECK(result == BONGO_CAT_OK && catalog.count == counts[run]);
        CHECK(catalog.capacity >= catalog.count && catalog.capacity <= BONGO_CAT_BEHAVIOR_LIMIT);
        if (result != BONGO_CAT_OK || !catalog.count) continue;
        CHECK(catalog.entries[catalog.count - 1].index == (int)catalog.count - 1);
        catalog.entries[0].shortcut_active = true;
        catalog.entries[0].audio_playing = true;
        bool copied = bongo_cat_behaviors_copy(&copy, &catalog, &error);
        CHECK(copied);
        if (!copied) break;
        CHECK(copy.entries != catalog.entries && copy.count == catalog.count);
        CHECK(!copy.entries[0].shortcut_active && !copy.entries[0].audio_playing);
        copy.entries[0].label[0] = 'X';
        CHECK(catalog.entries[0].label[0] == 'E');
        BongoCatBehaviorEntry *allocation = copy.entries;
        bongo_cat_behaviors_move(&moved, &copy);
        CHECK(moved.entries == allocation && moved.count == catalog.count);
        CHECK(!copy.entries && !copy.count && !copy.capacity);
    }
    CHECK(!catalog.entries && !catalog.count && !catalog.capacity);
    bongo_cat_behaviors_clear(&catalog);
    bongo_cat_behaviors_clear(&catalog);
    bongo_cat_behaviors_clear(&copy);
    bongo_cat_behaviors_clear(&moved);
}

void test_mver_manifest(void) {
    multiple_mver_manifests();
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char model[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    char models[BONGO_CAT_PATH_CAP], stored[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-manifest-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    many_behaviors(root);
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
    bongo_cat_behaviors_clear(behaviors); free(behaviors);
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
