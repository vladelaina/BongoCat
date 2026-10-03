#include "runtime.h"
#include "mver_config_keys.h"
#include "model_import.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static bool mver_model(const BongoCatModelEntry *model) {
    return model && (model->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
        model->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH);
}

static bool numbered_asset(const char *path, int *index) {
    const char *name = bongo_cat_path_name(path);
    if (!name || *name < '0' || *name > '9') return false;
    char *end;
    long number = strtol(name, &end, 10);
    if (*end != '.' || number < 0 || number >= BONGO_CAT_BEHAVIOR_LIMIT) return false;
    *index = (int)number; return true;
}

/* An address in the authored config; indexes are never compacted. */
static bool address(const BongoCatModelEntry *model, const BongoCatBehaviorEntry *entry,
    const char **mode, const char **field, int *index) {
    *mode = bongo_cat_mode_name(model->mode); *index = entry->index;
    switch (entry->kind) {
    case BONGO_CAT_BEHAVIOR_EXPRESSION: *field = "l2d_expression"; break;
    case BONGO_CAT_BEHAVIOR_MOTION:
        if (!strcmp(entry->group, "CAT_motion")) *field = "l2d_motion";
        else if (!strcmp(entry->group, "CAT_motion_lock")) *field = "l2d_motion_lockhand";
        else return false;
        break;
    case BONGO_CAT_BEHAVIOR_SOUND:
        if (entry->sound_clear) {
            *mode = "decoration"; *field = "soundClear"; *index = -1;
        } else {
            *field = "sounds";
            if (!numbered_asset(entry->sound, index)) return false;
        }
        break;
    case BONGO_CAT_BEHAVIOR_EFFECT:
        if (!entry->effect[0]) {
            *mode = "decoration"; *field = "emoticonClear"; *index = -1;
        } else {
            *field = "face";
            if (!numbered_asset(entry->effect, index)) return false;
        }
        break;
    default: return false;
    }
    *mode = bongo_cat_mver_binding_mode(*mode, *field);
    return true;
}

static void free_nodes(BongoCatModelShortcutNode *node) {
    while (node) {
        BongoCatModelShortcutNode *next = node->next;
        free(node); node = next;
    }
}

/* Commit only after the complete file/catalog has been read. Existing nodes
   remain at the same addresses so shortcut capture can safely finish. */
static bool commit_bindings(BongoCatApp *app, const char *model_id,
    BongoCatModelShortcutNode *parsed, BongoCatError *error) {
    BongoCatModelShortcutCache *cache = app->model_shortcuts;
    while (cache && strcmp(cache->model_id, model_id)) cache = cache->next;
    if (!cache) {
        cache = calloc(1, sizeof(*cache));
        if (!cache) {
            free_nodes(parsed);
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate model shortcut cache");
            return false;
        }
        snprintf(cache->model_id, sizeof(cache->model_id), "%s", model_id);
        cache->next = app->model_shortcuts;
        app->model_shortcuts = cache;
    }
    for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next) {
        node->binding.shortcut[0] = '\0';
        node->binding.shortcut_disabled = true;
        node->binding.label[0] = '\0';
    }
    while (parsed) {
        BongoCatModelShortcutNode *next = parsed->next;
        BongoCatModelShortcutNode *existing = cache->bindings;
        while (existing && strcmp(existing->binding.id, parsed->binding.id)) existing = existing->next;
        if (existing) {
            existing->binding = parsed->binding;
            free(parsed);
        } else {
            parsed->next = cache->bindings;
            cache->bindings = parsed;
        }
        parsed = next;
    }
    /* Existing custom names stay private; old private keys are not authoritative. */
    size_t length = strlen(model_id);
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        if (!strncmp(value->id, model_id, length) && value->id[length] == ':')
            value->shortcut_external = true;
    }
    return true;
}

bool bongo_cat_mver_shortcuts_load(BongoCatApp *app, const BongoCatModelEntry *model,
    BongoCatError *error) {
    if (!mver_model(model)) return true;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_mver_config_find(model->directory, path, sizeof(path))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot find Mver configuration");
        return false;
    }
    yyjson_doc *document = bongo_cat_json_read_file(path,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL);
    BongoCatBehaviorCatalog *catalog = calloc(1, sizeof(*catalog));
    BongoCatMverLabels *labels = calloc(1, sizeof(*labels));
    bool ok = document && catalog && labels &&
        yyjson_is_obj(yyjson_doc_get_root(document)) &&
        bongo_cat_behaviors_load(catalog, model, error) == BONGO_CAT_OK;
    if (ok) bongo_cat_mver_labels_load(path, bongo_cat_mode_name(model->mode), labels);
    BongoCatModelShortcutNode *parsed = NULL;
    for (size_t i = 0; ok && i < catalog->count; ++i) {
        const BongoCatBehaviorEntry *entry = &catalog->entries[i];
        const char *mode, *field; int index;
        BongoCatModelShortcutNode *node = calloc(1, sizeof(*node));
        if (!node) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate model shortcut cache");
            ok = false; break;
        }
        node->next = parsed; parsed = node;
        BongoCatBehaviorShortcut *value = &node->binding;
        snprintf(value->id, sizeof(value->id), "%.*s",
            (int)sizeof(value->id) - 1, entry->id);
        value->shortcut_external = true;
        value->shortcut[0] = '\0'; value->shortcut_disabled = true;
        if (!address(model, entry, &mode, &field, &index)) continue;
        yyjson_val *root = yyjson_doc_get_root(document);
        yyjson_val *rows = yyjson_obj_get(yyjson_obj_get(root, mode), field);
        yyjson_val *row = index < 0 ? rows : yyjson_arr_get(rows, (size_t)index);
        yyjson_val *input = yyjson_obj_get(yyjson_obj_get(root,
            bongo_cat_mode_name(model->mode)), "input_mode");
        BongoCatImportCandidate candidate = {0};
        candidate.gamepad_buttons = model->mode == BONGO_CAT_MODE_GAMEPAD &&
            (!yyjson_is_int(input) || yyjson_get_int(input) != 0);
        yyjson_val *only = yyjson_is_arr(row) && yyjson_arr_size(row) == 1
            ? yyjson_arr_get_first(row) : NULL;
        int code = yyjson_is_int(only) ? (int)yyjson_get_int(only) : -1;
        bool gamepad = candidate.gamepad_buttons && entry->kind == BONGO_CAT_BEHAVIOR_EFFECT;
        bool disabled = !row || yyjson_is_null(row) ||
            (yyjson_is_arr(row) && !yyjson_arr_size(row)) || code == 255 ||
            (code == 0 && !gamepad);
        if (!disabled) {
            bool chord_valid = entry->kind == BONGO_CAT_BEHAVIOR_EFFECT
                ? bongo_cat_mver_chord(&candidate, row, value->shortcut, sizeof(value->shortcut))
                : entry->kind == BONGO_CAT_BEHAVIOR_SOUND
                ? bongo_cat_mver_sound_chord(row, value->shortcut, sizeof(value->shortcut))
                : bongo_cat_mver_keyboard_chord(row, value->shortcut, sizeof(value->shortcut));
            if (!chord_valid) value->shortcut[0] = '\0';
        }
        value->shortcut_disabled = !value->shortcut[0];
        const char *label = index >= 0 ? bongo_cat_mver_label(labels, field, (size_t)index) : NULL;
        if (label && !value->label[0]) snprintf(value->label, sizeof(value->label), "%s", label);
    }
    if (ok) ok = commit_bindings(app, model->id, parsed, error);
    else free_nodes(parsed);
    if (!ok && error && !error->message[0])
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read Mver shortcuts: %s", path);
    yyjson_doc_free(document); bongo_cat_behaviors_clear(catalog); free(catalog); bongo_cat_mver_labels_clear(labels); free(labels);
    return ok;
}

bool bongo_cat_model_shortcut_save(BongoCatApp *app, const char *id,
    const char *shortcut, BongoCatError *error) {
    const BongoCatModelEntry *model = NULL;
    for (size_t i = 0; i < app->models.count; ++i) {
        const BongoCatModelEntry *entry = &app->models.entries[i];
        size_t length = strlen(entry->id);
        if (!strncmp(id, entry->id, length) && id[length] == ':') {
            model = entry; break;
        }
    }
    if (!mver_model(model)) return true;
    char path[BONGO_CAT_PATH_CAP], row[BONGO_CAT_SHORTCUT_CAP];
    if (!bongo_cat_mver_config_find(model->directory, path, sizeof(path))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot find Mver configuration");
        return false;
    }
    if (!bongo_cat_mver_shortcut_codes(shortcut, row, sizeof(row))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "This shortcut cannot be saved in Mver format");
        return false;
    }
    BongoCatBehaviorCatalog *catalog = calloc(1, sizeof(*catalog));
    bool ok = catalog && bongo_cat_behaviors_load(catalog, model, error) == BONGO_CAT_OK;
    const char *mode = NULL, *field = NULL; int index = 0;
    if (ok) for (size_t i = 0; i < catalog->count; ++i) {
        if (!strcmp(catalog->entries[i].id, id)) {
            ok = address(model, &catalog->entries[i], &mode, &field, &index);
            break;
        }
    }
    ok = ok && mode && field;
    if (!ok) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_FORMAT, "This action has no Mver shortcut configuration");
        bongo_cat_behaviors_clear(catalog); free(catalog); return false;
    }
    bongo_cat_behaviors_clear(catalog); free(catalog);
    if (model->mode == BONGO_CAT_MODE_GAMEPAD &&
        (!strcmp(field, "face") || !strcmp(field, "emoticonClear"))) {
        yyjson_doc *config = bongo_cat_json_read_file(path, YYJSON_READ_JSON5, NULL);
        yyjson_val *input = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(config),
            "gamepad"), "input_mode");
        char *end;
        long key = strtol(row + 1, &end, 10);
        bool ambiguous = *end == ']' && key >= 0 && key <= 15 &&
            (!yyjson_is_int(input) || yyjson_get_int(input) != 0);
        yyjson_doc_free(config);
        if (ambiguous) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                "Mver interprets this key as a gamepad button; use a modifier combination");
            return false;
        }
    }
    if (!bongo_cat_mver_config_write_row(path, mode, field, index, row, error)) return false;
    /* Refresh all variants sharing this file, including an already loaded pet.
       These values are an in-memory input cache, never a second saved binding. */
    for (size_t i = 0; i < app->models.count; ++i) {
        const BongoCatModelEntry *entry = &app->models.entries[i];
        char other[BONGO_CAT_PATH_CAP];
        if (mver_model(entry) && bongo_cat_mver_config_find(entry->directory, other, sizeof(other)) && !strcmp(path, other)) {
            BongoCatError refresh_error = {0};
            if (!bongo_cat_mver_shortcuts_load(app, entry, &refresh_error))
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", refresh_error.message);
        }
    }
    memset(&app->sound_shortcut_state, 0, sizeof(app->sound_shortcut_state));
    bongo_cat_app_reset_sound_bindings(app);
    return true;
}
