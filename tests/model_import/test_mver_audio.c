#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "runtime.h"
#include "window_menu.h"
#include "test_mver_support.h"
#include "test_mver_import_internal.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static void sound_chord(const char *text, const char *expected) {
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    char value[BONGO_CAT_SHORTCUT_CAP];
    bool ok = bongo_cat_mver_sound_chord(yyjson_doc_get_root(doc), value, sizeof(value));
    CHECK(expected ? ok && strcmp(value, expected) == 0 : !ok);
    yyjson_doc_free(doc);
}

void test_mver_audio(void) {
    sound_chord("[90]", "Z");
    sound_chord("[17,65,66]", "Control+A+B");
    sound_chord("[17]", "Control");
    sound_chord("[17,1]", "Control+Left");
    sound_chord("[0]", "");
    sound_chord("[255]", "");
    sound_chord("[144]", "NumLock");
    sound_chord("[]", NULL);
    sound_chord("[256]", NULL);
    char root[BONGO_CAT_PATH_CAP], adapter[BONGO_CAT_PATH_CAP];
    char sound_dir[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "bongocat-sound-import-%llu", (unsigned long long)SDL_GetTicksNS());
    CHECK(mver_fixture(root));
    BongoCatImportCandidate candidate = {.format = BONGO_CAT_IMPORT_MVER,
        .mode = BONGO_CAT_MODE_STANDARD};
    CHECK(child(candidate.directory, sizeof(candidate.directory), root, "img/standard/cat_model", false));
    snprintf(candidate.setting, sizeof(candidate.setting), "cat.model3.json");
    CHECK(child(candidate.assets, sizeof(candidate.assets), root, "img/standard", false));
    CHECK(child(candidate.config, sizeof(candidate.config), root, "config.json", false));
    CHECK(child(adapter, sizeof(adapter), root, "adapter", true));
    CHECK(child(sound_dir, sizeof(sound_dir), candidate.assets, "sounds", true));
    CHECK(child(path, sizeof(path), sound_dir, "0.flac", false));
    CHECK(bongo_cat_path_copy_file(BONGO_CAT_NATIVE_SOURCE_DIR
        "/resources/assets/models/standard/live2d_motion1.flac", path));
    BongoCatModelEntry *model = calloc(1, sizeof(*model));
    BongoCatBehaviorCatalog *catalog = calloc(1, sizeof(*catalog));
    CHECK(model && catalog);
    if (!model || !catalog) { free(model); bongo_cat_behaviors_clear(catalog); free(catalog); return; }
    snprintf(model->id, sizeof(model->id), "test-audio");
    snprintf(model->directory, sizeof(model->directory), "%s", candidate.directory);
    snprintf(model->setting_file, sizeof(model->setting_file), "%s", candidate.setting);
    snprintf(model->adapter_directory, sizeof(model->adapter_directory), "%s", adapter);
    for (int keep = 0; keep <= 1; ++keep) {
        char config[256];
        snprintf(config, sizeof(config), "{\"decoration\":{\"soundKeep\":%s,"
            "\"soundClear\":[17,222]},\"standard\":{\"sounds\":[[90],[88]]}}",
            keep ? "true" : "false");
        CHECK(write_text(candidate.config, config));
        BongoCatError error = {0};
        CHECK(bongo_cat_import_adapter_metadata(&candidate, adapter, &error));
        CHECK(bongo_cat_model_adapter_metadata_path(adapter, path, sizeof(path)));
        yyjson_doc *metadata = bongo_cat_json_read_file(path, 0, NULL);
        yyjson_val *items = yyjson_obj_get(yyjson_doc_get_root(metadata), "bindings");
        size_t item_index, item_count; yyjson_val *item;
        yyjson_arr_foreach(items, item_index, item_count, item)
            CHECK(!yyjson_obj_get(item, "shortcut"));
        yyjson_doc_free(metadata);
        CHECK(bongo_cat_behaviors_load(catalog, model, &error) == BONGO_CAT_OK);
        CHECK(catalog->count == 2); /* Missing 1.flac skipped; clear always imported. */
        CHECK(catalog->entries[0].sound[0] && !catalog->entries[0].momentary);
        CHECK(catalog->entries[0].sound_overlap == (keep != 0));
        CHECK(!catalog->entries[0].sound_clear && catalog->entries[1].sound_clear);
    }
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        model->source_format = BONGO_CAT_MODEL_SOURCE_MVER;
        model->mode = BONGO_CAT_MODE_STANDARD;
        app->models.entries[0] = *model;
        app->models.count = 1;
        bongo_cat_import_apply_metadata(app, "test-audio", adapter);
        CHECK(app->settings.behavior_shortcut_count == 0);
        BongoCatBehaviorShortcut *binding = bongo_cat_app_behavior_binding_mut(app, "test-audio:sound:0");
        CHECK(binding && !strcmp(binding->shortcut, "Z"));
        if (binding) binding->shortcut[0] = '\0';
        bongo_cat_import_apply_metadata(app, "test-audio", adapter);
        CHECK(binding == bongo_cat_app_behavior_binding(app, "test-audio:sound:0"));
        CHECK(binding && !strcmp(binding->shortcut, "Z") && binding->shortcut_external);
        CHECK(bongo_cat_model_shortcut_save(app, "test-audio:sound:0", "", NULL));
        CHECK(binding && !binding->shortcut[0]);
        char names[2][BONGO_CAT_MENU_LABEL_CAP];
        bool checked[2] = {false};
        size_t count = 99;
        bongo_cat_window_audio_labels(app, names, checked, &count);
        CHECK(count == 0);
        CHECK(bongo_cat_behaviors_copy(&app->behaviors, catalog, NULL));
        bongo_cat_window_audio_labels(app, names, checked, &count);
        CHECK(count == 2 && !checked[0] && !checked[1]);
        app->behaviors.entries[0].sound[0] = '\0';
        bongo_cat_window_audio_labels(app, names, checked, &count);
        CHECK(count == 0); /* A clear command alone does not expose Audio. */
        bongo_cat_app_model_shortcuts_clear(app);
        bongo_cat_behaviors_clear(&app->behaviors);
        free(app);
    }
    CHECK(child(path, sizeof(path), adapter, BONGO_CAT_MODEL_ADAPTER_FILE, false));
    CHECK(write_text(path, "{\"bindings\":["
        "{\"kind\":\"sound\",\"sound\":\"resources/sounds/0.flac\",\"overlap\":false},"
        "{\"kind\":\"sound\",\"sound\":\"missing.flac\"},"
        "{\"kind\":\"sound-clear\"}]}"));
    CHECK(bongo_cat_behaviors_load(catalog, model, NULL) == BONGO_CAT_OK);
    CHECK(catalog->count == 3);
    CHECK(!catalog->entries[0].momentary && !catalog->entries[0].sound_overlap);
    CHECK(!catalog->entries[1].sound[0] && !catalog->entries[1].sound_clear);
    CHECK(catalog->entries[2].sound_clear);
    bongo_cat_behaviors_clear(catalog); free(catalog); free(model);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}
