#include "test.h"
#include "bongo_cat/config.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "bongo_cat/utf8.h"

#include <stdio.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *file = bongo_cat_file_open(path, "wb");
    CHECK(file != NULL);
    if (file) { CHECK(fputs(text, file) >= 0); CHECK(fclose(file) == 0); }
}

static bool contains_text(const char *path, const char *needle) {
    char content[8192] = {0};
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    size_t length = fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    content[length] = '\0';
    return strstr(content, needle) != NULL;
}

void test_config(void) {
    BongoCatConfig value;
    bongo_cat_config_defaults(&value);
    CHECK(value.model.max_fps == 60);
    CHECK(value.window.width == 612 && value.window.height == 354);
    CHECK(value.window.visible && value.window.always_on_top);
    CHECK(!value.window.keep_in_screen);
    CHECK(!value.window.edge_snap);
    CHECK(value.model.mouse_centered);

    value.model.max_fps = 900;
    value.window.scale_percent = -2.0f;
    value.window.opacity_percent = 123.0f;
    bongo_cat_config_validate(&value);
    CHECK(value.model.max_fps == 240);
    CHECK(value.window.scale_percent == 10.0f);
    CHECK(value.window.opacity_percent == 100.0f);

    BongoCatConfig duplicates;
    bongo_cat_config_defaults(&duplicates);
    memcpy(duplicates.shortcuts.visible_cat, "Control+B", sizeof("Control+B"));
    memcpy(duplicates.shortcuts.visible_preferences, "control+b", sizeof("control+b"));
    duplicates.behavior_shortcut_count = 2;
    memcpy(duplicates.behavior_shortcuts[0].id, "motion:0", sizeof("motion:0"));
    memcpy(duplicates.behavior_shortcuts[0].shortcut, "CONTROL+B", sizeof("CONTROL+B"));
    memcpy(duplicates.behavior_shortcuts[1].id, "motion:1", sizeof("motion:1"));
    memcpy(duplicates.behavior_shortcuts[1].shortcut, "Control+M", sizeof("Control+M"));
    bongo_cat_config_validate(&duplicates);
    CHECK(strcmp(duplicates.shortcuts.visible_cat, "Control+B") == 0);
    CHECK(!duplicates.shortcuts.visible_preferences[0]);
    CHECK(!duplicates.behavior_shortcuts[0].shortcut[0]);
    CHECK(strcmp(duplicates.behavior_shortcuts[1].shortcut, "Control+M") == 0);
    CHECK(bongo_cat_config_shortcut_conflicts(&duplicates, "control+b", NULL));
    CHECK(!bongo_cat_config_shortcut_conflicts(&duplicates, "control+b",
        duplicates.shortcuts.visible_cat));
    memcpy(duplicates.behavior_shortcuts[1].label, "\xc4\xe3",
        sizeof("\xc4\xe3"));
    bongo_cat_config_validate(&duplicates);
    CHECK(bongo_cat_utf8_valid(duplicates.behavior_shortcuts[1].label));

    value.model.max_fps = 30;
    value.model.mirror = true;
    value.model.mouse_centered = false;
    value.window.pass_through = true;
    value.window.edge_snap = true;
    value.window.x = -321;
    value.window.opacity_percent = 75.0f;
    value.app.language = BONGO_CAT_LANG_ZH_CN;
    memcpy(value.current_model, "keyboard", sizeof("keyboard"));
    value.current_mode = BONGO_CAT_MODE_KEYBOARD;
    value.behavior_shortcut_count = 1;
    memcpy(value.behavior_shortcuts[0].id, "keyboard:motion:Tap:0",
        sizeof("keyboard:motion:Tap:0"));
    memcpy(value.behavior_shortcuts[0].shortcut, "Control+1", sizeof("Control+1"));
    memcpy(value.behavior_shortcuts[0].label, "Happy tap", sizeof("Happy tap"));

    const char *preferences = "bongo-cat-\xE5\x81\x8F\xE5\xA5\xBD.json";
    const char *session = "bongo-cat-\xE4\xBC\x9A\xE8\xAF\x9D.json";
    BongoCatError error = {0};
    CHECK(bongo_cat_preferences_save(preferences, &value, &error) == BONGO_CAT_OK);
    CHECK(!contains_text(preferences, "\"motionSound\""));
    CHECK(!contains_text(preferences, "\"behavior\""));
    CHECK(!contains_text(preferences, "\"taskbarVisible\""));
    CHECK(bongo_cat_session_save(session, &value, &error) == BONGO_CAT_OK);
    CHECK(bongo_cat_path_is_file(preferences));
    CHECK(bongo_cat_path_is_file(session));

    BongoCatConfig loaded;
    bongo_cat_config_defaults(&loaded);
    CHECK(bongo_cat_preferences_load(preferences, &loaded, &error) == BONGO_CAT_OK);
    CHECK(loaded.model.max_fps == 30 && loaded.model.mirror);
    CHECK(!loaded.model.mouse_centered);
    CHECK(loaded.window.pass_through);
    CHECK(loaded.window.edge_snap);
    CHECK(loaded.window.x == 0);
    CHECK(loaded.window.opacity_percent == 100.0f);
    CHECK(loaded.app.language == BONGO_CAT_LANG_ZH_CN);
    CHECK(strcmp(loaded.current_model, "standard") == 0);
    CHECK(loaded.behavior_shortcut_count == 1);
    CHECK(strcmp(loaded.behavior_shortcuts[0].shortcut, "Control+1") == 0);
    CHECK(strcmp(loaded.behavior_shortcuts[0].label, "Happy tap") == 0);

    for (int language = 0; language < BONGO_CAT_LANG_COUNT; ++language) {
        value.app.language = (BongoCatLanguage)language;
        CHECK(bongo_cat_preferences_save(preferences, &value, &error) == BONGO_CAT_OK);
        BongoCatConfig language_loaded;
        bongo_cat_config_defaults(&language_loaded);
        CHECK(bongo_cat_preferences_load(preferences, &language_loaded,
            &error) == BONGO_CAT_OK);
        CHECK(language_loaded.app.language == (BongoCatLanguage)language);
    }

    CHECK(bongo_cat_session_load(session, &loaded, &error) == BONGO_CAT_OK);
    CHECK(loaded.window.x == -321);
    CHECK(loaded.window.opacity_percent == 75.0f);
    CHECK(strcmp(loaded.current_model, "keyboard") == 0);
    CHECK(loaded.current_mode == BONGO_CAT_MODE_GAMEPAD);

    const char *legacy_switches = "bongo-cat-legacy-switches.json";
    write_text(legacy_switches,
        "{\"format\":\"bongo-cat/preferences\",\"version\":2,"
        "\"model\":{\"maxFPS\":42,\"motionSound\":false,\"behavior\":false}}");
    CHECK(bongo_cat_preferences_load(legacy_switches, &loaded, &error) == BONGO_CAT_OK);
    CHECK(loaded.model.max_fps == 42);
    CHECK(bongo_cat_preferences_save(legacy_switches, &loaded, &error) == BONGO_CAT_OK);
    CHECK(!contains_text(legacy_switches, "\"motionSound\""));
    CHECK(!contains_text(legacy_switches, "\"behavior\""));

    const char *unsupported = "bongo-cat-unsupported.json";
    write_text(unsupported, "{\"schemaVersion\":2,\"model\":{\"maxFPS\":1}}");
    int preserved_fps = loaded.model.max_fps;
    CHECK(bongo_cat_preferences_load(unsupported, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    CHECK(loaded.model.max_fps == preserved_fps);
    write_text(unsupported,
        "{\"format\":\"bongo-cat/preferences\",\"version\":1,\"model\":{\"maxFPS\":1}}");
    CHECK(bongo_cat_preferences_load(unsupported, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    CHECK(loaded.model.max_fps == preserved_fps);
    write_text(unsupported,
        "{\"format\":\"bongo-cat/preferences\",\"version\":3,\"model\":{\"maxFPS\":1}}");
    CHECK(bongo_cat_preferences_load(unsupported, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);

    const char *broken_session = "bongo-cat-broken-session.json";
    write_text(broken_session, "{ invalid");
    bool preserved_mirror = loaded.model.mirror;
    CHECK(bongo_cat_session_load(broken_session, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    CHECK(loaded.model.mirror == preserved_mirror);
    write_text(broken_session,
        "{\"format\":\"bongo-cat/session\",\"version\":1,\"window\":{\"visible\":false}}");
    CHECK(bongo_cat_session_load(broken_session, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    CHECK(loaded.model.mirror == preserved_mirror);
    write_text(broken_session,
        "{\"format\":\"bongo-cat/session\",\"version\":3,\"window\":{\"visible\":false}}");
    CHECK(bongo_cat_session_load(broken_session, &loaded, &error) ==
        BONGO_CAT_ERROR_FORMAT);

    CHECK(bongo_cat_file_remove(preferences));
    CHECK(bongo_cat_file_remove(session));
    CHECK(bongo_cat_file_remove(unsupported));
    CHECK(bongo_cat_file_remove(broken_session));
    CHECK(bongo_cat_file_remove(legacy_switches));
}
