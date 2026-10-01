#include "test.h"
#include "test_config_validation.h"
#include "bongo_cat/config.h"
#include "bongo_cat/file.h"
#include "bongo_cat/model.h"

#include <math.h>
#include <string.h>

static void check_defaults_and_validation(void) {
    static BongoCatSettings settings;
    static BongoCatSessionState session;
    bongo_cat_settings_defaults(&settings);
    bongo_cat_session_defaults(&session);
    CHECK(settings.model.max_fps == 60 && settings.model.mouse_centered &&
        !settings.model.multiple_pets);
    CHECK(settings.window.always_on_top && !settings.window.keep_in_screen);
    CHECK(!settings.window.obs_background);
    CHECK(!settings.window.random_expression &&
        settings.window.random_expression_interval_seconds ==
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.visible && session.window.width == 612 &&
        session.window.height == 354 && session.window.content_width == 612 &&
        session.window.content_height == 354);
    CHECK(strcmp(session.active_model_id, "standard") == 0);
    CHECK(session.additional_model_count == 0);
    CHECK(bongo_cat_session_add_model(&session, "keyboard"));
    CHECK(bongo_cat_session_add_model(&session, "keyboard"));
    CHECK(bongo_cat_session_add_model(&session, "gamepad"));
    CHECK(session.additional_model_count == 2 &&
        bongo_cat_session_model_active(&session, "standard") &&
        bongo_cat_session_model_active(&session, "keyboard"));
    CHECK(bongo_cat_session_remove_model(&session, "keyboard"));
    CHECK(session.additional_model_count == 1 &&
        !bongo_cat_session_model_active(&session, "keyboard"));
    bongo_cat_session_clear_additional_models(&session);
    settings.model.max_fps = 900;
    settings.window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT;
    settings.window.hide_delay_seconds = NAN;
    settings.window.random_expression_interval_seconds = NAN;
    session.window.scale_percent = -2.0f;
    session.window.opacity_percent = NAN;
    session.active_behavior_count = 3;
    memcpy(session.active_behaviors[0].model_id, "model", sizeof("model"));
    memcpy(session.active_behaviors[0].behavior_id, "model:motion:Tap:0",
        sizeof("model:motion:Tap:0"));
    session.active_behaviors[1] = session.active_behaviors[0];
    memcpy(session.active_behaviors[2].model_id, "other", sizeof("other"));
    bongo_cat_settings_validate(&settings);
    bongo_cat_session_validate(&session);
    CHECK(settings.model.max_fps == 60);
    const int old_fps[] = {-1, 0, 1, 24, 30, 31, 60, 120, 240};
    const int new_fps[] = {60, 60, 30, 30, 30, 60, 60, 60, 60};
    for (size_t i = 0; i < sizeof(old_fps) / sizeof(old_fps[0]); ++i) {
        settings.model.max_fps = old_fps[i];
        bongo_cat_settings_validate(&settings);
        CHECK(settings.model.max_fps == new_fps[i]);
    }
    CHECK(settings.window.hide_delay_seconds == 0.0f);
    CHECK(settings.window.random_expression_interval_seconds ==
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.scale_percent == 10.0f);
    CHECK(session.window.opacity_percent ==
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    CHECK(session.active_behavior_count == 1);
    CHECK(strcmp(session.active_behaviors[0].model_id, "model") == 0);
}

static void check_override_canonicalization(void) {
    static BongoCatSettings settings;
    bongo_cat_settings_defaults(&settings);
    settings.behavior_shortcut_count = 4;
    memcpy(settings.behavior_shortcuts[0].id, "motion:1", sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[0].shortcut, "Control+1",
        sizeof("Control+1"));
    memcpy(settings.behavior_shortcuts[0].label, "First", sizeof("First"));
    memcpy(settings.behavior_shortcuts[1].id, "motion:1", sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[1].shortcut, "Control+2",
        sizeof("Control+2"));
    memcpy(settings.behavior_shortcuts[2].id, "empty", sizeof("empty"));
    memcpy(settings.behavior_shortcuts[3].id, "motion:2", sizeof("motion:2"));
    memcpy(settings.behavior_shortcuts[3].label, "Second", sizeof("Second"));
    settings.model_label_count = 2;
    memcpy(settings.model_labels[0].id, "model", sizeof("model"));
    memcpy(settings.model_labels[0].label, "Old", sizeof("Old"));
    memcpy(settings.model_labels[1].id, "model", sizeof("model"));
    memcpy(settings.model_labels[1].label, "New", sizeof("New"));
    bongo_cat_settings_validate(&settings);
    CHECK(settings.behavior_shortcut_count == 2);
    CHECK(strcmp(settings.behavior_shortcuts[0].shortcut, "Control+2") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[0].label, "First") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[1].id, "motion:2") == 0);
    CHECK(settings.model_label_count == 1);
    CHECK(strcmp(settings.model_labels[0].label, "New") == 0);
}

static void check_shortcuts(void) {
    static BongoCatSettings settings;
    bongo_cat_settings_defaults(&settings);
    memcpy(settings.shortcuts.toggle_pet_visibility, "Control+B",
        sizeof("Control+B"));
    memcpy(settings.shortcuts.visible_preferences, "control+b",
        sizeof("control+b"));
    settings.behavior_shortcut_count = 2;
    memcpy(settings.behavior_shortcuts[0].id, "motion:0", sizeof("motion:0"));
    memcpy(settings.behavior_shortcuts[0].shortcut, "CONTROL+B",
        sizeof("CONTROL+B"));
    memcpy(settings.behavior_shortcuts[1].id, "motion:1", sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[1].shortcut, "Control+M",
        sizeof("Control+M"));
    bongo_cat_settings_validate(&settings);
    CHECK(strcmp(settings.shortcuts.toggle_pet_visibility, "Control+B") == 0);
    CHECK(!settings.shortcuts.visible_preferences[0]);
    CHECK(settings.behavior_shortcut_count == 1);
    CHECK(strcmp(settings.behavior_shortcuts[0].id, "motion:1") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[0].shortcut, "Control+M") == 0);
    CHECK(bongo_cat_settings_shortcut_conflicts(&settings, "control+b", NULL));
    CHECK(!bongo_cat_settings_shortcut_conflicts(&settings, "control+b",
        settings.shortcuts.toggle_pet_visibility));
}

static void check_behavior_companions(void) {
    static BongoCatSettings settings, loaded;
    bongo_cat_settings_defaults(&settings);
    const char *ids[] = {"lucia:motion:CAT_motion_lock:14", "lucia:sound:0",
        "lucia:expression:0", "lucia:motion:Tap:0", "lucia:sound:3"};
    settings.behavior_shortcut_count = sizeof(ids) / sizeof(ids[0]);
    for (size_t i = 0; i < settings.behavior_shortcut_count; ++i)
        snprintf(settings.behavior_shortcuts[i].id, sizeof(settings.behavior_shortcuts[i].id), "%s", ids[i]);
    for (size_t i = 0; i < 4; ++i)
        snprintf(settings.behavior_shortcuts[i].shortcut, BONGO_CAT_SHORTCUT_CAP, "Alt+O");
    settings.behavior_shortcuts[4].shortcut_disabled = true;
    snprintf(settings.shortcuts.mirror, BONGO_CAT_SHORTCUT_CAP, "Control+M");
    for (size_t i = 0; i < 4; ++i) {
        CHECK(!bongo_cat_settings_shortcut_conflicts(&settings, "alt+o",
            settings.behavior_shortcuts[i].shortcut));
        CHECK(bongo_cat_settings_shortcut_conflicts(&settings, "Control+M",
            settings.behavior_shortcuts[i].shortcut));
    }
    bongo_cat_settings_validate(&settings);
    CHECK(settings.behavior_shortcut_count == 5);
    for (size_t i = 0; i < 4; ++i)
        CHECK(!strcmp(settings.behavior_shortcuts[i].shortcut, "Alt+O"));
    CHECK(bongo_cat_settings_shortcut_conflicts(&settings, "Alt+O", settings.shortcuts.mirror));
    CHECK(bongo_cat_settings_save("bongocat-audio-config-test.json", &settings, NULL) == BONGO_CAT_OK);
    bongo_cat_settings_defaults(&loaded);
    CHECK(bongo_cat_settings_load("bongocat-audio-config-test.json", &loaded, NULL) == BONGO_CAT_OK);
    CHECK(loaded.behavior_shortcut_count == 5);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(!strcmp(loaded.behavior_shortcuts[i].id, ids[i]));
        CHECK(!strcmp(loaded.behavior_shortcuts[i].shortcut, "Alt+O"));
    }
    CHECK(loaded.behavior_shortcuts[4].shortcut_disabled);
    CHECK(bongo_cat_file_remove("bongocat-audio-config-test.json"));
}

void test_config_validation(void) {
    check_defaults_and_validation();
    check_behavior_companions();
    check_shortcuts();
    check_override_canonicalization();
}
