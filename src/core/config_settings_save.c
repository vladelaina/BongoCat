#include "config_internal.h"

#include <string.h>

#define SETTINGS_FORMAT "bongocat/settings"

static bool write_model(yyjson_mut_doc *doc, yyjson_mut_val *object,
    const BongoCatModelPreferences *value) {
    return object &&
        yyjson_mut_obj_add_bool(doc, object, "multiplePets",
            value->multiple_pets) &&
        yyjson_mut_obj_add_bool(doc, object, "modelMirrored", value->mirror) &&
        yyjson_mut_obj_add_bool(doc, object, "pointerMirrored",
            value->mouse_mirror) &&
        yyjson_mut_obj_add_bool(doc, object, "centerPointerTracking",
            value->mouse_centered) &&
        yyjson_mut_obj_add_bool(doc, object, "ignorePointerInput",
            value->ignore_mouse) &&
        yyjson_mut_obj_add_int(doc, object, "maximumFps", value->max_fps);
}

static bool write_window(yyjson_mut_doc *doc, yyjson_mut_val *object,
    const BongoCatWindowPreferences *value) {
    return object &&
        yyjson_mut_obj_add_bool(doc, object, "clickThrough",
            value->pass_through) &&
        yyjson_mut_obj_add_bool(doc, object, "alwaysOnTop",
            value->always_on_top) &&
        yyjson_mut_obj_add_bool(doc, object, "hideOnPointerOver",
            value->hide_on_hover) &&
        yyjson_mut_obj_add_bool(doc, object, "keepOnScreen",
            value->keep_in_screen) &&
        yyjson_mut_obj_add_bool(doc, object, "captureBackground",
            value->obs_background) &&
        yyjson_mut_obj_add_bool(doc, object, "randomExpression",
            value->random_expression) &&
        yyjson_mut_obj_add_bool(doc, object, "roundedCorners",
            value->rounded_corners) &&
        yyjson_mut_obj_add_real(doc, object, "cornerRadiusPercent",
            value->corner_radius_percent) &&
        yyjson_mut_obj_add_strcpy(doc, object, "captureBackgroundColor",
            bongo_cat_obs_background_color_name(
                value->obs_background_color)) &&
        yyjson_mut_obj_add_real(doc, object, "hideDelaySeconds",
            value->hide_delay_seconds) &&
        yyjson_mut_obj_add_real(doc, object, "hideFadeSeconds",
            value->hide_fade_seconds) &&
        yyjson_mut_obj_add_real(doc, object,
            "randomExpressionIntervalSeconds",
            value->random_expression_interval_seconds);
}

static bool write_app(yyjson_mut_doc *doc, yyjson_mut_val *object,
    const BongoCatApplicationPreferences *value) {
    return object &&
        yyjson_mut_obj_add_bool(doc, object, "launchAtLogin",
            value->autostart) &&
        yyjson_mut_obj_add_bool(doc, object, "showTrayIcon",
            value->tray_visible) &&
        yyjson_mut_obj_add_strcpy(doc, object, "theme",
            bongo_cat_theme_name(value->theme)) &&
        yyjson_mut_obj_add_strcpy(doc, object, "language",
            bongo_cat_language_name(value->language));
}

static bool write_shortcuts(yyjson_mut_doc *doc, yyjson_mut_val *object,
    const BongoCatShortcutPreferences *value) {
    return object &&
        yyjson_mut_obj_add_strcpy(doc, object, "toggleVisibility",
            value->toggle_pet_visibility) &&
        yyjson_mut_obj_add_strcpy(doc, object, "openSettings",
            value->visible_preferences) &&
        yyjson_mut_obj_add_strcpy(doc, object, "toggleModelMirror",
            value->mirror) &&
        yyjson_mut_obj_add_strcpy(doc, object, "toggleClickThrough",
            value->pass_through) &&
        yyjson_mut_obj_add_strcpy(doc, object, "toggleAlwaysOnTop",
            value->always_on_top);
}

static bool write_behaviors(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatSettings *settings) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    if (!array || !yyjson_mut_obj_add_val(
            doc, root, "behaviorOverrides", array)) return false;
    for (size_t i = 0; i < settings->behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *value =
            &settings->behavior_shortcuts[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (!item || !yyjson_mut_obj_add_strcpy(
                doc, item, "behaviorId", value->id) ||
            (value->shortcut_disabled && !yyjson_mut_obj_add_bool(
                doc, item, "shortcutDisabled", true)) ||
            (value->shortcut[0] && !yyjson_mut_obj_add_strcpy(
                doc, item, "shortcut", value->shortcut)) ||
            (value->label[0] && !yyjson_mut_obj_add_strcpy(
                doc, item, "displayName", value->label)) ||
            !yyjson_mut_arr_add_val(array, item)) return false;
    }
    return true;
}

static bool write_model_labels(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatSettings *settings) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    if (!array || !yyjson_mut_obj_add_val(
            doc, root, "modelOverrides", array)) return false;
    for (size_t i = 0; i < settings->model_label_count; ++i) {
        const BongoCatModelLabel *value = &settings->model_labels[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (!item || !yyjson_mut_obj_add_strcpy(
                doc, item, "modelId", value->id) ||
            !yyjson_mut_obj_add_strcpy(
                doc, item, "displayName", value->label) ||
            !yyjson_mut_arr_add_val(array, item)) return false;
    }
    return true;
}

static bool write_removed_models(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatSettings *settings) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    if (!array || !yyjson_mut_obj_add_val(
            doc, root, "removedModels", array)) return false;
    for (size_t i = 0; i < settings->removed_model_count; ++i)
        if (!yyjson_mut_arr_add_strcpy(doc, array,
                settings->removed_models[i].id)) return false;
    return true;
}

static yyjson_mut_val *write_extensions(yyjson_mut_doc *target,
    const BongoCatSettings *settings, BongoCatResult *failure,
    BongoCatError *error) {
    *failure = BONGO_CAT_OK;
    const char *json = settings->extensions_json;
    const char *end = memchr(json, '\0', sizeof(settings->extensions_json));
    if (!end) {
        *failure = BONGO_CAT_ERROR_FORMAT;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Settings extensions are not null-terminated");
        return NULL;
    }
    size_t length = (size_t)(end - json);
    if (!length) {
        json = "{}";
        length = 2;
    }
    yyjson_read_err read_error = {0};
    yyjson_doc *source = yyjson_read_opts(
        (char *)json, length, 0, NULL, &read_error);
    yyjson_val *root = source ? yyjson_doc_get_root(source) : NULL;
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(source);
        BongoCatResult result = read_error.code ==
            YYJSON_READ_ERROR_MEMORY_ALLOCATION ? BONGO_CAT_ERROR_MEMORY :
            BONGO_CAT_ERROR_FORMAT;
        *failure = result;
        bongo_cat_error_set(error, result,
            "Settings extensions are not a valid JSON object");
        return NULL;
    }
    yyjson_mut_val *copy = yyjson_val_mut_copy(target, root);
    yyjson_doc_free(source);
    if (!copy) {
        *failure = BONGO_CAT_ERROR_MEMORY;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate settings extensions");
    }
    return copy;
}

static BongoCatResult build_error(BongoCatError *error) {
    bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
        "Cannot allocate settings JSON");
    return BONGO_CAT_ERROR_MEMORY;
}

BongoCatResult bongo_cat_settings_save(const char *path,
    const BongoCatSettings *settings, BongoCatError *error) {
    if (!path || !settings) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatSettings canonical = *settings;
    bongo_cat_settings_validate(&canonical);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return build_error(error);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return build_error(error);
    }
    yyjson_mut_doc_set_root(doc, root);
    BongoCatResult extension_result = BONGO_CAT_OK;
    yyjson_mut_val *extensions = write_extensions(
        doc, &canonical, &extension_result, error);
    if (!extensions) {
        yyjson_mut_doc_free(doc);
        return extension_result;
    }
    bool built = yyjson_mut_obj_add_strcpy(
            doc, root, "format", SETTINGS_FORMAT) &&
        yyjson_mut_obj_add_int(doc, root, "schemaVersion",
            BONGO_CAT_SETTINGS_SCHEMA) &&
        write_model(doc, yyjson_mut_obj_add_obj(doc, root, "rendering"),
            &canonical.model) &&
        write_window(doc, yyjson_mut_obj_add_obj(doc, root, "window"),
            &canonical.window) &&
        write_app(doc, yyjson_mut_obj_add_obj(doc, root, "application"),
            &canonical.app) &&
        write_shortcuts(doc, yyjson_mut_obj_add_obj(doc, root, "shortcuts"),
            &canonical.shortcuts) &&
        write_behaviors(doc, root, &canonical) &&
        write_model_labels(doc, root, &canonical) &&
        write_removed_models(doc, root, &canonical) &&
        yyjson_mut_obj_add_val(doc, root, "extensions", extensions);
    if (!built) {
        yyjson_mut_doc_free(doc);
        return build_error(error);
    }
    BongoCatResult result = bongo_cat_config_write_document(path, doc,
        "settings file", error);
    yyjson_mut_doc_free(doc);
    return result;
}
