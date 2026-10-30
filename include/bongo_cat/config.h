#ifndef BONGO_CAT_CONFIG_H
#define BONGO_CAT_CONFIG_H

#include "bongo_cat/common.h"

#define BONGO_CAT_DEFAULT_AUTO_RELEASE_SECONDS 3.0f
#define BONGO_CAT_DEFAULT_MAX_FPS 60
#define BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT 100.0f
#define BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT 100.0f

typedef enum BongoCatTheme { BONGO_CAT_THEME_AUTO, BONGO_CAT_THEME_LIGHT, BONGO_CAT_THEME_DARK } BongoCatTheme;
typedef enum BongoCatLanguage {
    BONGO_CAT_LANG_EN_US,
    BONGO_CAT_LANG_ZH_CN,
    BONGO_CAT_LANG_ZH_TW,
    BONGO_CAT_LANG_FR_FR,
    BONGO_CAT_LANG_DE_DE,
    BONGO_CAT_LANG_JA_JP,
    BONGO_CAT_LANG_KO_KR,
    BONGO_CAT_LANG_PT_BR,
    BONGO_CAT_LANG_RU_RU,
    BONGO_CAT_LANG_ES_ES,
    BONGO_CAT_LANG_COUNT
} BongoCatLanguage;
typedef enum BongoCatModelMode {
    BONGO_CAT_MODE_STANDARD,
    BONGO_CAT_MODE_KEYBOARD,
    BONGO_CAT_MODE_GAMEPAD
} BongoCatModelMode;

typedef struct BongoCatModelOptions {
    bool mirror;
    bool mouse_mirror;
    bool mouse_centered;
    bool ignore_mouse;
    float auto_release_seconds;
    int max_fps;
} BongoCatModelOptions;

typedef struct BongoCatWindowOptions {
    bool visible;
    bool pass_through;
    bool always_on_top;
    bool hide_on_hover;
    bool keep_in_screen;
    bool edge_snap;
    float scale_percent;
    float opacity_percent;
    float hide_delay_seconds;
    int x;
    int y;
    int width;
    int height;
} BongoCatWindowOptions;

typedef struct BongoCatAppOptions {
    bool autostart;
    bool tray_visible;
    BongoCatTheme theme;
    BongoCatLanguage language;
} BongoCatAppOptions;

typedef struct BongoCatShortcutOptions {
    char visible_cat[BONGO_CAT_SHORTCUT_CAP];
    char visible_preferences[BONGO_CAT_SHORTCUT_CAP];
    char mirror[BONGO_CAT_SHORTCUT_CAP];
    char pass_through[BONGO_CAT_SHORTCUT_CAP];
    char always_on_top[BONGO_CAT_SHORTCUT_CAP];
} BongoCatShortcutOptions;

typedef struct BongoCatBehaviorShortcut {
    char id[BONGO_CAT_PATH_CAP];
    char shortcut[BONGO_CAT_SHORTCUT_CAP];
    char label[BONGO_CAT_ID_CAP];
} BongoCatBehaviorShortcut;

typedef struct BongoCatConfig {
    BongoCatModelOptions model;
    BongoCatWindowOptions window;
    BongoCatAppOptions app;
    BongoCatShortcutOptions shortcuts;
    BongoCatBehaviorShortcut behavior_shortcuts[BONGO_CAT_BEHAVIOR_CAP];
    size_t behavior_shortcut_count;
    char current_model[BONGO_CAT_PATH_CAP];
    BongoCatModelMode current_mode;
} BongoCatConfig;

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_config_defaults(BongoCatConfig *config);
void bongo_cat_config_validate(BongoCatConfig *config);
bool bongo_cat_config_shortcut_conflicts(const BongoCatConfig *config,
    const char *shortcut, const char *exclude);
BongoCatResult bongo_cat_preferences_load(const char *path,
    BongoCatConfig *config, BongoCatError *error);
BongoCatResult bongo_cat_preferences_save(const char *path,
    const BongoCatConfig *config, BongoCatError *error);
BongoCatResult bongo_cat_session_load(const char *path,
    BongoCatConfig *config, BongoCatError *error);
BongoCatResult bongo_cat_session_save(const char *path,
    const BongoCatConfig *config, BongoCatError *error);
const char *bongo_cat_theme_name(BongoCatTheme value);
const char *bongo_cat_language_name(BongoCatLanguage value);
const char *bongo_cat_mode_name(BongoCatModelMode value);

#ifdef __cplusplus
}
#endif

#endif
