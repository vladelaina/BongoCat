#include "bongo_cat/config.h"
#include "bongo_cat/utf8.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf_or(float value, float low, float high, float fallback) {
    if (!isfinite(value)) return fallback;
    return value < low ? low : value > high ? high : value;
}

static bool shortcut_equal(const char *left, const char *right) {
    if (!left || !right || !left[0] || !right[0]) return false;
    while (*left && *right) {
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++))
            return false;
    }
    return *left == *right;
}

static bool normalize_text(char *text, size_t capacity) {
    text[capacity - 1] = '\0';
    if (!bongo_cat_utf8_valid(text)) {
        memset(text, 0, capacity);
        return false;
    }
    size_t length = strlen(text);
    memset(text + length + 1, 0, capacity - length - 1);
    return true;
}

bool bongo_cat_settings_shortcut_conflicts(const BongoCatSettings *config,
    const char *shortcut, const char *exclude) {
    if (!config || !shortcut || !shortcut[0]) return false;
    const char *global[] = {config->shortcuts.toggle_pet_visibility,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i)
        if (global[i] != exclude && shortcut_equal(global[i], shortcut))
            return true;
    size_t behavior_count = config->behavior_shortcut_count;
    if (behavior_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    /* Model behaviors may share a key; global commands remain exclusive. */
    for (size_t i = 0; i < behavior_count; ++i)
        if (config->behavior_shortcuts[i].shortcut == exclude) return false;
    for (size_t i = 0; i < behavior_count; ++i) {
        const char *bound = config->behavior_shortcuts[i].shortcut;
        if (shortcut_equal(bound, shortcut)) return true;
    }
    return false;
}

static void validate_shortcuts(BongoCatSettings *config) {
    char *global[] = {config->shortcuts.toggle_pet_visibility,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i) {
        normalize_text(global[i], BONGO_CAT_SHORTCUT_CAP);
        for (size_t j = 0; j < i; ++j)
            if (shortcut_equal(global[i], global[j]))
                memset(global[i], 0, BONGO_CAT_SHORTCUT_CAP);
    }
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        char *shortcut = config->behavior_shortcuts[i].shortcut;
        bool duplicate = false;
        for (size_t j = 0; j < sizeof(global) / sizeof(global[0]); ++j)
            duplicate = duplicate || shortcut_equal(shortcut, global[j]);
        if (duplicate) shortcut[0] = '\0';
    }
}

static void compact_behavior_overrides(BongoCatSettings *config) {
    size_t input_count = config->behavior_shortcut_count;
    if (input_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        input_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    size_t output_count = 0;
    for (size_t i = 0; i < input_count; ++i) {
        BongoCatBehaviorShortcut entry = config->behavior_shortcuts[i];
        entry.id[sizeof(entry.id) - 1] = '\0';
        entry.shortcut[sizeof(entry.shortcut) - 1] = '\0';
        entry.label[sizeof(entry.label) - 1] = '\0';
        if (!bongo_cat_utf8_valid(entry.id) ||
            !bongo_cat_utf8_valid(entry.shortcut) ||
            !bongo_cat_utf8_valid(entry.label)) continue;
        if (entry.shortcut[0]) entry.shortcut_disabled = false;
        if (!entry.id[0] || (!entry.shortcut[0] && !entry.label[0] && !entry.shortcut_disabled)) continue;
        BongoCatBehaviorShortcut canonical = {0};
        canonical.shortcut_disabled = entry.shortcut_disabled;
        snprintf(canonical.id, sizeof(canonical.id), "%s", entry.id);
        snprintf(canonical.shortcut, sizeof(canonical.shortcut), "%s",
            entry.shortcut);
        snprintf(canonical.label, sizeof(canonical.label), "%s", entry.label);
        size_t existing = output_count;
        for (size_t j = 0; j < output_count; ++j)
            if (!strcmp(config->behavior_shortcuts[j].id, entry.id)) {
                existing = j;
                break;
            }
        if (existing < output_count) {
            if (canonical.shortcut[0] || canonical.shortcut_disabled) {
                config->behavior_shortcuts[existing].shortcut_disabled = canonical.shortcut_disabled;
                memset(config->behavior_shortcuts[existing].shortcut, 0,
                    sizeof(config->behavior_shortcuts[existing].shortcut));
                snprintf(
                    config->behavior_shortcuts[existing].shortcut,
                    sizeof(config->behavior_shortcuts[existing].shortcut),
                    "%s", canonical.shortcut);
            }
            if (canonical.label[0]) {
                memset(config->behavior_shortcuts[existing].label, 0,
                    sizeof(config->behavior_shortcuts[existing].label));
                snprintf(
                    config->behavior_shortcuts[existing].label,
                    sizeof(config->behavior_shortcuts[existing].label),
                    "%s", canonical.label);
            }
            continue;
        }
        config->behavior_shortcuts[output_count++] = canonical;
    }
    memset(&config->behavior_shortcuts[output_count], 0,
        (BONGO_CAT_BEHAVIOR_BINDING_CAP - output_count) *
        sizeof(config->behavior_shortcuts[0]));
    config->behavior_shortcut_count = output_count;
}

static void compact_model_overrides(BongoCatSettings *config) {
    size_t input_count = config->model_label_count;
    if (input_count > BONGO_CAT_MODEL_CAP) input_count = BONGO_CAT_MODEL_CAP;
    size_t output_count = 0;
    for (size_t i = 0; i < input_count; ++i) {
        BongoCatModelLabel entry = config->model_labels[i];
        entry.id[sizeof(entry.id) - 1] = '\0';
        entry.label[sizeof(entry.label) - 1] = '\0';
        if (!entry.id[0] || !entry.label[0] ||
            !bongo_cat_utf8_valid(entry.id) ||
            !bongo_cat_utf8_valid(entry.label)) continue;
        BongoCatModelLabel canonical = {0};
        snprintf(canonical.id, sizeof(canonical.id), "%s", entry.id);
        snprintf(canonical.label, sizeof(canonical.label), "%s", entry.label);
        size_t existing = output_count;
        for (size_t j = 0; j < output_count; ++j)
            if (!strcmp(config->model_labels[j].id, entry.id)) {
                existing = j;
                break;
            }
        if (existing < output_count) {
            memset(config->model_labels[existing].label, 0,
                sizeof(config->model_labels[existing].label));
            snprintf(config->model_labels[existing].label,
                sizeof(config->model_labels[existing].label), "%s",
                canonical.label);
            continue;
        }
        config->model_labels[output_count++] = canonical;
    }
    memset(&config->model_labels[output_count], 0,
        (BONGO_CAT_MODEL_CAP - output_count) *
        sizeof(config->model_labels[0]));
    config->model_label_count = output_count;
}

static void compact_removed_models(BongoCatSettings *config) {
    size_t input_count = config->removed_model_count;
    if (input_count > BONGO_CAT_MODEL_CAP) input_count = BONGO_CAT_MODEL_CAP;
    size_t output_count = 0;
    for (size_t i = 0; i < input_count; ++i) {
        BongoCatRemovedModel entry = config->removed_models[i];
        entry.id[sizeof(entry.id) - 1] = '\0';
        if (!entry.id[0] || !bongo_cat_utf8_valid(entry.id)) continue;
        bool duplicate = false;
        for (size_t j = 0; j < output_count; ++j)
            if (!strcmp(config->removed_models[j].id, entry.id)) {
                duplicate = true;
                break;
            }
        if (!duplicate) {
            BongoCatRemovedModel canonical = {0};
            snprintf(canonical.id, sizeof(canonical.id), "%s", entry.id);
            config->removed_models[output_count++] = canonical;
        }
    }
    memset(&config->removed_models[output_count], 0,
        (BONGO_CAT_MODEL_CAP - output_count) *
        sizeof(config->removed_models[0]));
    config->removed_model_count = output_count;
}

void bongo_cat_settings_defaults(BongoCatSettings *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->model.mouse_centered = true;
    config->model.max_fps = BONGO_CAT_DEFAULT_MAX_FPS;
    config->window.always_on_top = true;
    config->window.keep_in_screen = false;
    config->window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_GREEN;
    config->window.corner_radius_percent = BONGO_CAT_DEFAULT_WINDOW_CORNER_PERCENT;
    config->window.hide_fade_seconds = BONGO_CAT_DEFAULT_HIDE_FADE_SECONDS;
    config->window.random_expression_interval_seconds =
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS;
    config->app.tray_visible = true;
    config->app.theme = BONGO_CAT_THEME_AUTO;
    config->app.language = BONGO_CAT_LANG_EN_US;
    memcpy(config->extensions_json, "{}", sizeof("{}"));
}

void bongo_cat_settings_validate(BongoCatSettings *config) {
    if (!config) return;
    if (config->model.max_fps < 1) config->model.max_fps = 1;
    if (config->model.max_fps > 240) config->model.max_fps = 240;
    config->window.hide_delay_seconds = clampf_or(
        config->window.hide_delay_seconds, 0.0f, 60.0f, 0.0f);
    config->window.hide_fade_seconds = clampf_or(
        config->window.hide_fade_seconds, 0.0f,
        BONGO_CAT_MAX_HIDE_FADE_SECONDS, BONGO_CAT_DEFAULT_HIDE_FADE_SECONDS);
    config->window.corner_radius_percent = clampf_or(
        config->window.corner_radius_percent, 0.0f, 50.0f,
        BONGO_CAT_DEFAULT_WINDOW_CORNER_PERCENT);
    config->window.random_expression_interval_seconds = clampf_or(
        config->window.random_expression_interval_seconds, 1.0f, 3600.0f,
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    if ((unsigned)config->window.obs_background_color >=
        BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT)
        config->window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_GREEN;
    if ((unsigned)config->app.theme > BONGO_CAT_THEME_DARK)
        config->app.theme = BONGO_CAT_THEME_AUTO;
    if ((unsigned)config->app.language >= BONGO_CAT_LANG_COUNT)
        config->app.language = BONGO_CAT_LANG_EN_US;
    compact_behavior_overrides(config);
    compact_model_overrides(config);
    compact_removed_models(config);
    validate_shortcuts(config);
    compact_behavior_overrides(config);
}
