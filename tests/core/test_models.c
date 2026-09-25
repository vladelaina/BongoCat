#include "test.h"
#include "bongo_cat/file.h"
#include "bongo_cat/model.h"
#include "bongo_cat/path.h"

#include <string.h>
#include <stdlib.h>
#include <yyjson.h>

typedef struct ParameterSet {
    char ids[128][BONGO_CAT_ID_CAP];
    size_t count;
} ParameterSet;

static yyjson_doc *read_json(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file, 0, NULL, NULL) : NULL;
    if (file) fclose(file);
    return document;
}

static bool signature(const char *path, const char *expected, size_t length) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    char actual[8] = {0};
    bool result = length <= sizeof(actual) && fread(actual, 1, length, file) == length &&
        memcmp(actual, expected, length) == 0;
    fclose(file);
    return result;
}

static bool reference_path(const BongoCatModelEntry *model, const char *relative,
    char output[BONGO_CAT_PATH_CAP]) {
    return relative && bongo_cat_path_join(output, BONGO_CAT_PATH_CAP, model->directory, relative);
}

static bool add_parameters(const BongoCatModelEntry *model, const char *relative,
    ParameterSet *parameters) {
    char path[BONGO_CAT_PATH_CAP];
    if (!reference_path(model, relative, path)) return false;
    yyjson_doc *document = read_json(path);
    if (!document) return false;
    yyjson_val *array = yyjson_obj_get(yyjson_doc_get_root(document), "Parameters");
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        const char *id = yyjson_get_str(yyjson_obj_get(item, "Id"));
        if (!id || parameters->count >= 128) { yyjson_doc_free(document); return false; }
        snprintf(parameters->ids[parameters->count++], BONGO_CAT_ID_CAP, "%s", id);
    }
    yyjson_doc_free(document);
    return parameters->count > 0;
}

static bool has_parameter(const ParameterSet *parameters, const char *id) {
    for (size_t i = 0; i < parameters->count; ++i)
        if (strcmp(parameters->ids[i], id) == 0) return true;
    return false;
}

static bool validate_parameter_file(const BongoCatModelEntry *model,
    const char *relative, const ParameterSet *parameters, bool motion) {
    char path[BONGO_CAT_PATH_CAP];
    if (!reference_path(model, relative, path)) return false;
    yyjson_doc *document = read_json(path);
    if (!document) return false;
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *array = yyjson_obj_get(root, motion ? "Curves" : "Parameters");
    size_t index, count; yyjson_val *item;
    bool valid = yyjson_is_arr(array);
    yyjson_arr_foreach(array, index, count, item) {
        const char *target = motion ? yyjson_get_str(yyjson_obj_get(item, "Target")) : "Parameter";
        const char *id = yyjson_get_str(yyjson_obj_get(item, "Id"));
        if (target && strcmp(target, "Parameter") == 0 && (!id || !has_parameter(parameters, id))) {
            valid = false; break;
        }
    }
    yyjson_doc_free(document);
    return valid;
}

static bool validate_model(const BongoCatModelEntry *model) {
    char setting_path[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    if (!reference_path(model, model->setting_file, setting_path)) return false;
    yyjson_doc *document = read_json(setting_path);
    if (!document) return false;
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *refs = yyjson_obj_get(root, "FileReferences");
    const char *moc = yyjson_get_str(yyjson_obj_get(refs, "Moc"));
    const char *display = yyjson_get_str(yyjson_obj_get(refs, "DisplayInfo"));
    bool valid = yyjson_get_int(yyjson_obj_get(root, "Version")) == 3 &&
        reference_path(model, moc, path) && signature(path, "MOC3", 4);
    ParameterSet parameters = {0};
    valid = valid && display && add_parameters(model, display, &parameters);
    yyjson_val *textures = yyjson_obj_get(refs, "Textures");
    size_t index, count; yyjson_val *item;
    size_t texture_count = 0, expression_count = 0, motion_count = 0;
    yyjson_arr_foreach(textures, index, count, item) {
        const char *file = yyjson_get_str(item); texture_count++;
        valid = valid && reference_path(model, file, path) &&
            signature(path, "\x89PNG\r\n\x1a\n", 8);
    }
    yyjson_val *expressions = yyjson_obj_get(refs, "Expressions");
    yyjson_arr_foreach(expressions, index, count, item) {
        const char *file = yyjson_get_str(yyjson_obj_get(item, "File")); expression_count++;
        valid = valid && validate_parameter_file(model, file, &parameters, false);
    }
    yyjson_val *motions = yyjson_obj_get(refs, "Motions");
    size_t group_index, group_count; yyjson_val *key, *group;
    yyjson_obj_foreach(motions, group_index, group_count, key, group) {
        yyjson_arr_foreach(group, index, count, item) {
            const char *file = yyjson_get_str(yyjson_obj_get(item, "File")); motion_count++;
            valid = valid && validate_parameter_file(model, file, &parameters, true);
            const char *sound = yyjson_get_str(yyjson_obj_get(item, "Sound"));
            if (sound) valid = valid && reference_path(model, sound, path) &&
                signature(path, "fLaC", 4);
        }
    }
    yyjson_doc_free(document);
    reference_path(model, "resources/background.png", path);
    return valid && texture_count == 3 && expression_count == 3 && motion_count == 4 &&
        signature(path, "\x89PNG\r\n\x1a\n", 8);
}

void test_models(void) {
    BongoCatModelCatalog *catalog = calloc(1, sizeof(*catalog));
    BongoCatBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    BongoCatError error = {0};
    CHECK(catalog && behaviors);
    if (!catalog || !behaviors) {
        free(catalog);
        bongo_cat_behaviors_clear(behaviors); free(behaviors);
        return;
    }
    bongo_cat_models_init(catalog);
    CHECK(bongo_cat_models_scan(catalog,
        BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models",
        false, &error) == BONGO_CAT_OK);
    CHECK(catalog->count == 0);
    bongo_cat_models_init(catalog);
    CHECK(bongo_cat_models_scan(catalog, BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models",
        true, &error) == BONGO_CAT_OK);
    CHECK(catalog->count == 3);
    const BongoCatModelEntry *standard = bongo_cat_models_find(catalog, "standard");
    const BongoCatModelEntry *keyboard = bongo_cat_models_find(catalog, "keyboard");
    const BongoCatModelEntry *gamepad = bongo_cat_models_find(catalog, "gamepad");
    CHECK(standard && standard->mode == BONGO_CAT_MODE_STANDARD && validate_model(standard));
    CHECK(keyboard && keyboard->mode == BONGO_CAT_MODE_KEYBOARD && validate_model(keyboard));
    CHECK(gamepad && gamepad->mode == BONGO_CAT_MODE_GAMEPAD && validate_model(gamepad));
    CHECK(bongo_cat_behaviors_load(behaviors, standard, &error) == BONGO_CAT_OK);
    CHECK(behaviors->count == 7);
    CHECK(behaviors->entries[0].kind == BONGO_CAT_BEHAVIOR_MOTION);
    CHECK(strcmp(behaviors->entries[0].group, "CAT_motion") == 0);
    CHECK(strcmp(behaviors->entries[0].label, "Motion One") == 0);
    CHECK(strcmp(behaviors->entries[1].label, "Motion Two") == 0);
    CHECK(strcmp(behaviors->entries[2].label, "CAT_motion_lock 1") == 0);
    CHECK(strstr(behaviors->entries[0].sound, "live2d_motion1.flac") != NULL);
    CHECK(behaviors->entries[6].kind == BONGO_CAT_BEHAVIOR_EXPRESSION);

    bongo_cat_behaviors_clear(behaviors); free(behaviors);
    free(catalog);
}
