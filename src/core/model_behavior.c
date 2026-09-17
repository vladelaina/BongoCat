#include "bongo_cat/file.h"
#include "bongo_cat/json.h"
#include "bongo_cat/model.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool add_behavior(BongoCatBehaviorCatalog *catalog, const BongoCatModelEntry *model,
    BongoCatBehaviorKind kind, const char *group, int index, const char *label,
    const char *asset, const char *asset_root) {
    if (catalog->count >= BONGO_CAT_BEHAVIOR_CAP) return false;
    BongoCatBehaviorEntry *entry = &catalog->entries[catalog->count++];
    entry->kind = kind;
    entry->index = index;
    snprintf(entry->group, sizeof(entry->group), "%s", group ? group : "");
    snprintf(entry->label, sizeof(entry->label), "%s", label ? label : "");
    if (kind == BONGO_CAT_BEHAVIOR_MOTION)
        snprintf(entry->id, sizeof(entry->id), "%s:motion:%s:%d",
            model->id, group ? group : "", index);
    else if (kind == BONGO_CAT_BEHAVIOR_EXPRESSION)
        snprintf(entry->id, sizeof(entry->id), "%s:expression:%d", model->id, index);
    else if (kind == BONGO_CAT_BEHAVIOR_SOUND)
        snprintf(entry->id, sizeof(entry->id), "%s:sound:%d", model->id, index);
    else snprintf(entry->id, sizeof(entry->id), "%s:effect:%d", model->id, index);
    if (asset && *asset && (kind == BONGO_CAT_BEHAVIOR_SOUND ||
        kind == BONGO_CAT_BEHAVIOR_MOTION)) {
        bongo_cat_path_join(entry->sound, sizeof(entry->sound),
            asset_root ? asset_root : model->directory, asset);
        if (!bongo_cat_path_is_file(entry->sound)) entry->sound[0] = '\0';
    }
    if (asset && *asset && kind == BONGO_CAT_BEHAVIOR_EFFECT)
        bongo_cat_path_join(entry->effect, sizeof(entry->effect),
            asset_root ? asset_root : model->directory, asset);
    return true;
}

static bool read_motions(BongoCatBehaviorCatalog *catalog, const BongoCatModelEntry *model,
    yyjson_val *motions) {
    if (!yyjson_is_obj(motions)) return true;
    size_t group_index, group_count;
    yyjson_val *group_key, *items;
    yyjson_obj_foreach(motions, group_index, group_count, group_key, items) {
        if (!yyjson_is_arr(items)) continue;
        const char *group = yyjson_get_str(group_key);
        size_t index, count; yyjson_val *item;
        yyjson_arr_foreach(items, index, count, item) {
            const char *sound = yyjson_get_str(yyjson_obj_get(item, "Sound"));
            char label[BONGO_CAT_ID_CAP];
            snprintf(label, sizeof(label), "%s %zu", group, index + 1);
            if (!add_behavior(catalog, model, BONGO_CAT_BEHAVIOR_MOTION, group,
                (int)index, label, sound, NULL)) return false;
        }
    }
    return true;
}

static bool read_expressions(BongoCatBehaviorCatalog *catalog,
    const BongoCatModelEntry *model, yyjson_val *expressions) {
    if (!yyjson_is_arr(expressions)) return true;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(expressions, index, count, item) {
        const char *name = yyjson_get_str(yyjson_obj_get(item, "Name"));
        char label[BONGO_CAT_ID_CAP];
        snprintf(label, sizeof(label), "%s", name ? name : "Expression");
        if (!add_behavior(catalog, model, BONGO_CAT_BEHAVIOR_EXPRESSION, NULL,
            (int)index, label, NULL, NULL)) return false;
    }
    return true;
}

static bool read_adapter_assets(BongoCatBehaviorCatalog *catalog,
    const BongoCatModelEntry *model) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_model_adapter_metadata_path(model->adapter_directory,
        path, sizeof(path))) return false;
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    if (!document) return true;
    yyjson_val *items = yyjson_obj_get(yyjson_doc_get_root(document), "bindings");
    size_t index, count; yyjson_val *item;
    int sound_index = 0, effect_index = 0; bool ok = true;
    yyjson_arr_foreach(items, index, count, item) {
        const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
        bool effect = kind && (strcmp(kind, "effect") == 0 ||
            strcmp(kind, "effect-clear") == 0);
        bool sound = kind && (strcmp(kind, "sound") == 0 ||
            strcmp(kind, "sound-clear") == 0);
        if (!effect && !sound) continue;
        bool clear = strstr(kind, "-clear") != NULL;
        if (clear) {
            char label[BONGO_CAT_ID_CAP];
            snprintf(label, sizeof(label), "Clear %s", effect ? "effect" : "sound");
            if (!add_behavior(catalog, model, effect ? BONGO_CAT_BEHAVIOR_EFFECT :
                BONGO_CAT_BEHAVIOR_SOUND, NULL,
                effect ? effect_index++ : sound_index++, label, NULL,
                model->adapter_directory)) {
                ok = false; break;
            }
            catalog->entries[catalog->count - 1].sound_clear = sound;
            continue;
        }
        const char *asset = yyjson_get_str(yyjson_obj_get(item, effect ? "effect" : "sound"));
        const char *configured_label = yyjson_get_str(yyjson_obj_get(item, "label"));
        char label[BONGO_CAT_ID_CAP];
        int current = effect ? effect_index++ : sound_index++;
        if (configured_label) snprintf(label, sizeof(label), "%s", configured_label);
        else snprintf(label, sizeof(label), "%s %d",
            effect ? "Effect" : "Sound", current + 1);
        if (!add_behavior(catalog, model, effect ? BONGO_CAT_BEHAVIOR_EFFECT :
            BONGO_CAT_BEHAVIOR_SOUND, NULL, current, label, asset,
            model->adapter_directory)) { ok = false; break; }
        BongoCatBehaviorEntry *entry = &catalog->entries[catalog->count - 1];
        bool momentary = yyjson_get_bool(yyjson_obj_get(item, "momentary"));
        entry->momentary = effect && momentary;
        yyjson_val *overlap = yyjson_obj_get(item, "overlap");
        entry->sound_overlap = sound && (overlap ? yyjson_get_bool(overlap) : true);
    }
    yyjson_doc_free(document);
    return ok;
}

BongoCatResult bongo_cat_behaviors_load(BongoCatBehaviorCatalog *catalog,
    const BongoCatModelEntry *model, BongoCatError *error) {
    if (!catalog || !model) return BONGO_CAT_ERROR_ARGUMENT;
    memset(catalog, 0, sizeof(*catalog));
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), model->directory, model->setting_file))
        return BONGO_CAT_ERROR_FORMAT;
    yyjson_doc *document = bongo_cat_model_json_read(path, NULL);
    if (!document) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "Cannot read model setting: %s",
            path);
        return BONGO_CAT_ERROR_FORMAT;
    }
    yyjson_val *references = yyjson_obj_get(yyjson_doc_get_root(document), "FileReferences");
    bool ok = read_motions(catalog, model, yyjson_obj_get(references, "Motions")) &&
        read_expressions(catalog, model, yyjson_obj_get(references, "Expressions")) &&
        read_adapter_assets(catalog, model);
    yyjson_doc_free(document);
    if (!ok) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "Too many model behaviors");
        return BONGO_CAT_ERROR_FORMAT;
    }
    return BONGO_CAT_OK;
}
