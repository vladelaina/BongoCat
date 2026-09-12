#include "config_internal.h"
#include "bongo_cat/path.h"

#include <stdlib.h>
#include <string.h>

#define SETTINGS_FORMAT "bongocat/settings"
#define type_error(error, field, expected) bongo_cat_config_type_error( \
    "Settings", error, field, expected)
#define read_value(object, key, target, error) bongo_cat_config_read_value( \
    "Settings", object, key, target, error)
#define read_object(object, key, target, error) bongo_cat_config_read_object( \
    "Settings", object, key, target, error)
#define read_array(object, key, target, error) bongo_cat_config_read_array( \
    "Settings", object, key, target, error)
#define read_bool(object, key, target, error) bongo_cat_config_read_bool( \
    "Settings", object, key, target, error)
#define read_int(object, key, target, error) bongo_cat_config_read_int( \
    "Settings", object, key, target, false, error)
#define read_float(object, key, target, error) bongo_cat_config_read_float( \
    "Settings", object, key, target, error)
#define read_string(object, key, target, length, error) \
    bongo_cat_config_read_string( \
        "Settings", object, key, target, length, error)
#define read_text(object, key, target, capacity, error) \
    bongo_cat_config_read_text( \
        "Settings", object, key, target, capacity, error)
#define copy_text(target, capacity, value, length, field, error) \
    bongo_cat_config_copy_text( \
        "Settings", target, capacity, value, length, field, error)

static bool parse_theme(const char *value, BongoCatTheme *target) {
    if (!strcmp(value, "auto")) *target = BONGO_CAT_THEME_AUTO;
    else if (!strcmp(value, "light")) *target = BONGO_CAT_THEME_LIGHT;
    else if (!strcmp(value, "dark")) *target = BONGO_CAT_THEME_DARK;
    else return false;
    return true;
}

static bool parse_background_color(const char *value,
    BongoCatObsBackgroundColor *target) {
    for (int i = 0; i < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT; ++i)
        if (!strcmp(value, bongo_cat_obs_background_color_name(
                (BongoCatObsBackgroundColor)i))) {
            *target = (BongoCatObsBackgroundColor)i;
            return true;
        }
    return false;
}

static bool read_model(yyjson_val *object, BongoCatModelPreferences *value,
    BongoCatError *error) {
    return read_bool(object, "multiplePets", &value->multiple_pets, error) &&
        read_bool(object, "modelMirrored", &value->mirror, error) &&
        read_bool(object, "pointerMirrored", &value->mouse_mirror, error) &&
        read_bool(object, "centerPointerTracking", &value->mouse_centered,
            error) &&
        read_bool(object, "ignorePointerInput", &value->ignore_mouse, error) &&
        read_int(object, "maximumFps", &value->max_fps, error);
}

static bool read_window(yyjson_val *object, BongoCatWindowPreferences *value,
    BongoCatError *error) {
    if (!read_bool(object, "clickThrough", &value->pass_through, error) ||
        !read_bool(object, "alwaysOnTop", &value->always_on_top, error) ||
        !read_bool(object, "hideOnPointerOver", &value->hide_on_hover, error) ||
        !read_bool(object, "keepOnScreen", &value->keep_in_screen, error) ||
        !read_bool(object, "captureBackground", &value->obs_background,
            error) ||
        !read_bool(object, "randomExpression", &value->random_expression,
            error) ||
        !read_bool(object, "roundedCorners", &value->rounded_corners, error) ||
        !read_float(object, "cornerRadiusPercent", &value->corner_radius_percent,
            error) ||
        !read_float(object, "hideDelaySeconds", &value->hide_delay_seconds,
            error) ||
        !read_float(object, "hideFadeSeconds", &value->hide_fade_seconds,
            error) ||
        !read_float(object, "randomExpressionIntervalSeconds",
            &value->random_expression_interval_seconds,
            error)) return false;
    const char *color;
    size_t length;
    if (!read_string(object, "captureBackgroundColor", &color, &length,
            error)) return false;
    (void)length;
    if (color && !parse_background_color(color, &value->obs_background_color))
        return type_error(error, "captureBackgroundColor",
            "a supported color string");
    return true;
}

static bool read_app(yyjson_val *object, BongoCatApplicationPreferences *value,
    BongoCatError *error) {
    if (!read_bool(object, "launchAtLogin", &value->autostart, error) ||
        !read_bool(object, "showTrayIcon", &value->tray_visible, error))
        return false;
    const char *text;
    size_t length;
    if (!read_string(object, "theme", &text, &length, error)) return false;
    (void)length;
    if (text && !parse_theme(text, &value->theme))
        return type_error(error, "theme", "auto, light, or dark");
    if (!read_string(object, "language", &text, &length, error)) return false;
    if (text && !bongo_cat_language_parse(text, &value->language))
        return type_error(error, "language", "a supported locale string");
    return true;
}

static bool read_shortcuts(yyjson_val *object,
    BongoCatShortcutPreferences *value, BongoCatError *error) {
    return read_text(object, "toggleVisibility", value->toggle_pet_visibility,
            sizeof(value->toggle_pet_visibility), error) &&
        read_text(object, "openSettings", value->visible_preferences,
            sizeof(value->visible_preferences), error) &&
        read_text(object, "toggleModelMirror", value->mirror,
            sizeof(value->mirror), error) &&
        read_text(object, "toggleClickThrough", value->pass_through,
            sizeof(value->pass_through), error) &&
        read_text(object, "toggleAlwaysOnTop", value->always_on_top,
            sizeof(value->always_on_top), error);
}

static bool read_behaviors(yyjson_val *array, BongoCatSettings *settings,
    BongoCatError *error) {
    if (!array) return true;
    if (yyjson_arr_size(array) > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        return type_error(error, "behaviorOverrides", "a smaller array");
    settings->behavior_shortcut_count = 0;
    size_t index, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        if (!yyjson_is_obj(item))
            return type_error(error, "behaviorOverrides[]", "an object");
        const char *id, *shortcut, *label;
        size_t id_length, shortcut_length, label_length;
        if (!read_string(item, "behaviorId", &id, &id_length, error) ||
            !read_string(item, "shortcut", &shortcut, &shortcut_length,
                error) ||
            !read_string(item, "displayName", &label, &label_length, error))
            return false;
        if (!id || !id_length)
            return type_error(error, "behaviorId", "a non-empty string");
        BongoCatBehaviorShortcut *entry = &settings->behavior_shortcuts[
            settings->behavior_shortcut_count++];
        memset(entry, 0, sizeof(*entry));
        if (!read_bool(item, "shortcutDisabled", &entry->shortcut_disabled, error)) return false;
        if (!copy_text(entry->id, sizeof(entry->id), id, id_length,
                "behaviorId", error) ||
            !copy_text(entry->shortcut, sizeof(entry->shortcut), shortcut,
                shortcut_length, "shortcut", error) ||
            !copy_text(entry->label, sizeof(entry->label), label,
                label_length, "displayName", error)) return false;
    }
    return true;
}

static bool read_model_labels(yyjson_val *array, BongoCatSettings *settings,
    BongoCatError *error) {
    if (!array) return true;
    if (yyjson_arr_size(array) > BONGO_CAT_MODEL_CAP)
        return type_error(error, "modelOverrides", "a smaller array");
    settings->model_label_count = 0;
    size_t index, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        if (!yyjson_is_obj(item))
            return type_error(error, "modelOverrides[]", "an object");
        const char *id, *label;
        size_t id_length, label_length;
        if (!read_string(item, "modelId", &id, &id_length, error) ||
            !read_string(item, "displayName", &label, &label_length, error))
            return false;
        if (!id || !id_length || !label || !label_length)
            return type_error(error, "modelOverrides[]",
                "non-empty modelId and displayName strings");
        BongoCatModelLabel *entry =
            &settings->model_labels[settings->model_label_count++];
        memset(entry, 0, sizeof(*entry));
        if (!copy_text(entry->id, sizeof(entry->id), id, id_length,
                "modelId", error) ||
            !copy_text(entry->label, sizeof(entry->label), label,
                label_length, "displayName", error)) return false;
    }
    return true;
}

static bool read_removed_models(yyjson_val *array, BongoCatSettings *settings,
    BongoCatError *error) {
    if (!array) return true;
    if (yyjson_arr_size(array) > BONGO_CAT_MODEL_CAP)
        return type_error(error, "removedModels", "a smaller array");
    settings->removed_model_count = 0;
    size_t index, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        const char *id = yyjson_get_str(item);
        size_t length = yyjson_get_len(item);
        if (!yyjson_is_str(item) || !id || !length || strlen(id) != length)
            return type_error(error, "removedModels[]",
                "a non-empty string without embedded nulls");
        BongoCatRemovedModel *entry =
            &settings->removed_models[settings->removed_model_count++];
        memset(entry, 0, sizeof(*entry));
        if (!copy_text(entry->id, sizeof(entry->id), id, length,
                "removedModels[]", error)) return false;
    }
    return true;
}

static BongoCatResult read_extensions(yyjson_val *value,
    BongoCatSettings *settings, BongoCatError *error) {
    if (!value) return BONGO_CAT_OK;
    if (!yyjson_is_obj(value)) {
        type_error(error, "extensions", "an object");
        return BONGO_CAT_ERROR_FORMAT;
    }
    size_t length = 0;
    char *json = yyjson_val_write(value, YYJSON_WRITE_NOFLAG, &length);
    if (!json) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot preserve settings extensions");
        return BONGO_CAT_ERROR_MEMORY;
    }
    if (length >= sizeof(settings->extensions_json)) {
        free(json);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Settings extensions exceed the %u-byte limit",
            (unsigned)(sizeof(settings->extensions_json) - 1));
        return BONGO_CAT_ERROR_FORMAT;
    }
    memset(settings->extensions_json, 0, sizeof(settings->extensions_json));
    memcpy(settings->extensions_json, json, length + 1);
    free(json);
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_settings_load(const char *path,
    BongoCatSettings *settings, BongoCatError *error) {
    if (!path || !settings) return BONGO_CAT_ERROR_ARGUMENT;
    if (!bongo_cat_path_is_file(path)) return BONGO_CAT_OK;
    yyjson_doc *document = NULL;
    BongoCatResult result = bongo_cat_config_read_document(path,
        SETTINGS_FORMAT, BONGO_CAT_SETTINGS_SCHEMA, &document, error);
    if (result != BONGO_CAT_OK) return result;
    BongoCatSettings loaded = *settings;
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *model = NULL;
    yyjson_val *window = NULL;
    yyjson_val *application = NULL;
    yyjson_val *shortcuts = NULL;
    yyjson_val *behaviors = NULL;
    yyjson_val *models = NULL;
    yyjson_val *removed_models = NULL;
    yyjson_val *extensions_value = NULL;
    bool valid = read_object(root, "rendering", &model, error) &&
        read_object(root, "window", &window, error) &&
        read_object(root, "application", &application, error) &&
        read_object(root, "shortcuts", &shortcuts, error) &&
        read_array(root, "behaviorOverrides", &behaviors, error) &&
        read_array(root, "modelOverrides", &models, error) &&
        read_array(root, "removedModels", &removed_models, error) &&
        read_value(root, "extensions", &extensions_value, error) &&
        (!model || read_model(model, &loaded.model, error)) &&
        (!window || read_window(window, &loaded.window, error)) &&
        (!application || read_app(application, &loaded.app, error)) &&
        (!shortcuts || read_shortcuts(shortcuts, &loaded.shortcuts, error)) &&
        read_behaviors(behaviors, &loaded, error) &&
        read_model_labels(models, &loaded, error) &&
        read_removed_models(removed_models, &loaded, error);
    if (!valid) result = BONGO_CAT_ERROR_FORMAT;
    if (valid) result = read_extensions(extensions_value, &loaded, error);
    yyjson_doc_free(document);
    if (result != BONGO_CAT_OK) return result;
    bongo_cat_settings_validate(&loaded);
    *settings = loaded;
    return BONGO_CAT_OK;
}
