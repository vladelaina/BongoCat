#include "bongo_cat/config.h"
#include "bongo_cat/utf8.h"

#include <ctype.h>
#include <string.h>

static float clampf(float value, float low, float high) {
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

bool bongo_cat_config_shortcut_conflicts(const BongoCatConfig *config,
    const char *shortcut, const char *exclude) {
    if (!config || !shortcut || !shortcut[0]) return false;
    const char *global[] = {config->shortcuts.visible_cat,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i)
        if (global[i] != exclude && shortcut_equal(global[i], shortcut))
            return true;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        const char *bound = config->behavior_shortcuts[i].shortcut;
        if (bound != exclude && shortcut_equal(bound, shortcut)) return true;
    }
    return false;
}

static void validate_shortcuts(BongoCatConfig *config) {
    char *global[] = {config->shortcuts.visible_cat,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i) {
        global[i][BONGO_CAT_SHORTCUT_CAP - 1] = '\0';
        for (size_t j = 0; j < i; ++j)
            if (shortcut_equal(global[i], global[j])) global[i][0] = '\0';
    }
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        char *shortcut = config->behavior_shortcuts[i].shortcut;
        bool duplicate = false;
        for (size_t j = 0; j < sizeof(global) / sizeof(global[0]); ++j)
            duplicate = duplicate || shortcut_equal(shortcut, global[j]);
        for (size_t j = 0; j < i; ++j)
            duplicate = duplicate || shortcut_equal(shortcut,
                config->behavior_shortcuts[j].shortcut);
        if (duplicate) shortcut[0] = '\0';
    }
}

void bongo_cat_config_defaults(BongoCatConfig *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->model.auto_release_seconds =
        BONGO_CAT_DEFAULT_AUTO_RELEASE_SECONDS;
    config->model.mouse_centered = true;
    config->model.max_fps = BONGO_CAT_DEFAULT_MAX_FPS;
    config->window.visible = true;
    config->window.always_on_top = true;
    config->window.keep_in_screen = false;
    config->window.edge_snap = false;
    config->window.scale_percent = BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT;
    config->window.opacity_percent =
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT;
    config->window.width = 612;
    config->window.height = 354;
    config->app.tray_visible = true;
    config->app.theme = BONGO_CAT_THEME_AUTO;
    config->app.language = BONGO_CAT_LANG_EN_US;
    config->current_mode = BONGO_CAT_MODE_GAMEPAD;
    memcpy(config->current_model, "standard", sizeof("standard"));
}

void bongo_cat_config_validate(BongoCatConfig *config) {
    if (!config) return;
    config->model.auto_release_seconds = clampf(config->model.auto_release_seconds, 0.05f, 30.0f);
    if (config->model.max_fps < 1) config->model.max_fps = 1;
    if (config->model.max_fps > 240) config->model.max_fps = 240;
    config->window.scale_percent = clampf(config->window.scale_percent, 10.0f, 500.0f);
    config->window.opacity_percent = clampf(config->window.opacity_percent, 10.0f, 100.0f);
    config->window.hide_delay_seconds = clampf(config->window.hide_delay_seconds, 0.0f, 60.0f);
    if (config->window.width < 64) config->window.width = 64;
    if (config->window.height < 64) config->window.height = 64;
    if (config->window.width > 8192) config->window.width = 8192;
    if (config->window.height > 8192) config->window.height = 8192;
    if (config->app.theme > BONGO_CAT_THEME_DARK) config->app.theme = BONGO_CAT_THEME_AUTO;
    if ((unsigned)config->app.language >= BONGO_CAT_LANG_COUNT)
        config->app.language = BONGO_CAT_LANG_EN_US;
    if (config->current_mode > BONGO_CAT_MODE_GAMEPAD) config->current_mode = BONGO_CAT_MODE_STANDARD;
    config->current_model[sizeof(config->current_model) - 1] = '\0';
    if (config->behavior_shortcut_count > BONGO_CAT_BEHAVIOR_CAP)
        config->behavior_shortcut_count = BONGO_CAT_BEHAVIOR_CAP;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        config->behavior_shortcuts[i].id[BONGO_CAT_PATH_CAP - 1] = '\0';
        config->behavior_shortcuts[i].shortcut[BONGO_CAT_SHORTCUT_CAP - 1] = '\0';
        config->behavior_shortcuts[i].label[BONGO_CAT_ID_CAP - 1] = '\0';
        char normalized[BONGO_CAT_ID_CAP] = {0};
        if (bongo_cat_utf8_normalize_legacy(
            config->behavior_shortcuts[i].label, normalized,
            sizeof(normalized)))
            memcpy(config->behavior_shortcuts[i].label, normalized,
                sizeof(normalized));
        else config->behavior_shortcuts[i].label[0] = '\0';
    }
    validate_shortcuts(config);
}

const char *bongo_cat_theme_name(BongoCatTheme value) {
    const char *names[] = {"auto", "light", "dark"};
    return value <= BONGO_CAT_THEME_DARK ? names[value] : names[0];
}

const char *bongo_cat_language_name(BongoCatLanguage value) {
    const char *names[] = {"en-US", "zh-CN", "zh-TW", "fr-FR", "de-DE",
        "ja-JP", "ko-KR", "pt-BR", "ru-RU", "es-ES"};
    return (unsigned)value < BONGO_CAT_LANG_COUNT ? names[value] : names[0];
}

const char *bongo_cat_mode_name(BongoCatModelMode value) {
    const char *names[] = {"standard", "keyboard", "gamepad"};
    return value <= BONGO_CAT_MODE_GAMEPAD ? names[value] : names[0];
}
