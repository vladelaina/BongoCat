#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "preferences_model_glyphs.h"
#include "preferences_state.h"
#include "ui_font_atlas.h"
#include "runtime.h"
#include "test_mver_support.h"
#include "test_mver_import_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
int failures;
int test_preferences_text(void); int test_mver_nearby_identity(void);
int test_mver_nearby_refresh(void);
int test_mver_missing_motion_groups(void);
int test_mver_policy(void);
int test_model_import_identity(void);
int test_slim_package(void);
static bool chord(const char *json, bool gamepad, const char *expected) {
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    BongoCatImportCandidate candidate = {0};
    candidate.gamepad_buttons = gamepad;
    char output[BONGO_CAT_SHORTCUT_CAP];
    bool ok = document && bongo_cat_mver_chord(&candidate,
        yyjson_doc_get_root(document), output, sizeof(output));
    bool matches = ok && expected && strcmp(output, expected) == 0;
    yyjson_doc_free(document);
    return expected ? matches : !ok;
}

static void labels_from_shortcut_rows(void) {
    static const char default_expression[] =
        "\xE9\xBB\x98\xE8\xAE\xA4\xE8\xA1\xA8\xE6\x83\x85";
    static const char keyboard_hidden[] =
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1";
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-labels-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(path, sizeof(path), root, "config.json", false));
    CHECK(write_text(path,
        "{/* misleading: \\\"standard\\\" */standard:{"
        "l2d_expression:[[18,//\xE9\xBB\x98\xE8\xAE\xA4\xE8\xA1\xA8\xE6\x83\x85\n76],"
        "[18,77]],l2d_motion:[],l2d_motion_lockhand:"
        "[[18,//\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1\n49]],sounds:[]},"
        "keyboard:{l2d_expression:[]}}"));
    BongoCatMverLabels labels = {0};
    CHECK(bongo_cat_mver_labels_load(path, "standard", &labels));
    CHECK(labels.count == 2);
    CHECK(strcmp(bongo_cat_mver_label(&labels, "l2d_expression", 0),
        default_expression) == 0);
    CHECK(bongo_cat_mver_label(&labels, "l2d_expression", 1) == NULL);
    CHECK(strcmp(bongo_cat_mver_label(&labels, "l2d_motion_lockhand", 0),
        keyboard_hidden) == 0);
    bongo_cat_mver_labels_clear(&labels);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

static void metadata_backfills_labels(void) {
    static const char keyboard_hidden[] =
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1";
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-metadata-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(path, sizeof(path), root, ".bongo-cat-adapter.json", false));
    CHECK(write_text(path, "{\"schemaVersion\":1,"
        "\"kind\":\"bongo-cat-runtime-adapter\",\"bindings\":["
        "{\"kind\":\"motion\",\"group\":\"CAT_motion_lock\",\"index\":0,"
        "\"shortcut\":\"Alt+1\",\"label\":\"\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1\"},"
        "{\"kind\":\"motion\",\"group\":\"CAT_motion_lock\",\"index\":1,"
        "\"shortcut\":\"Alt+2\",\"label\":\"Imported\"},"
        "{\"kind\":\"expression\",\"index\":2,\"shortcut\":\"Alt+3\","
        "\"label\":\"Expression label\"}]}"));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        app->settings.behavior_shortcut_count = 2;
        snprintf(app->settings.behavior_shortcuts[0].id,
            sizeof(app->settings.behavior_shortcuts[0].id),
            "model:motion:CAT_motion_lock:0");
        snprintf(app->settings.behavior_shortcuts[0].shortcut,
            sizeof(app->settings.behavior_shortcuts[0].shortcut), "Control+1");
        snprintf(app->settings.behavior_shortcuts[1].id,
            sizeof(app->settings.behavior_shortcuts[1].id),
            "model:motion:CAT_motion_lock:1");
        snprintf(app->settings.behavior_shortcuts[1].label,
            sizeof(app->settings.behavior_shortcuts[1].label), "Custom name");
        bongo_cat_import_apply_metadata(app, "model", root);
        CHECK(app->settings.behavior_shortcut_count == 3);
        CHECK(strcmp(app->settings.behavior_shortcuts[0].shortcut, "Control+1") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[0].label, keyboard_hidden) == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[1].label, "Custom name") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[2].id,
            "model:expression:2") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[2].label,
            "Expression label") == 0);
        bongo_cat_behaviors_clear(&app->behaviors); free(app);
    }
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

static bool range_has(const uint32_t *ranges, uint32_t rune) {
    for (size_t i = 0; ranges[i]; i += 2)
        if (rune >= ranges[i] && rune <= ranges[i + 1]) return true;
    return false;
}

static void behavior_labels_add_font_glyphs(void) {
    static const unsigned char custom_label[] = {
        0xe7, 0x8c, 0xab, 0xe5, 0x92, 0xaa, 0xe5, 0xbd,
        0xa2, 0xe6, 0x80, 0x81, 0
    };
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) return;
    CHECK(bongo_cat_behaviors_reserve(&app->behaviors, 1, NULL));
    if (!app->behaviors.entries) { free(app); return; }
    snprintf(app->behaviors.entries[0].label,
        sizeof(app->behaviors.entries[0].label),
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1");
    app->behaviors.count = 1;
    memcpy(app->settings.behavior_shortcuts[0].label, custom_label,
        sizeof(custom_label));
    app->settings.behavior_shortcut_count = 1;
    uint32_t ranges[64] = {0x20, 0x7e, 0};
    bongo_cat_preferences_model_glyphs(app, ranges,
        sizeof(ranges) / sizeof(ranges[0]));
    CHECK(range_has(ranges, 0x952e));
    CHECK(range_has(ranges, 0x76d8));
    CHECK(range_has(ranges, 0x6d88));
    CHECK(range_has(ranges, 0x5931));
    CHECK(range_has(ranges, 0x732b));
    CHECK(range_has(ranges, 0x54aa));
    CHECK(range_has(ranges, 0x5f62));
    CHECK(range_has(ranges, 0x6001));
    BongoCatPreferences *preferences = calloc(1, sizeof(*preferences));
    CHECK(preferences != NULL);
    if (preferences) {
        preferences->app = app;
        preferences->ui_initialized = true;
        preferences->glyph_ranges[0] = 0x20;
        preferences->glyph_ranges[1] = 0x7e;
        CHECK(!bongo_cat_preferences_behavior_glyphs_ready(preferences));
        bongo_cat_preferences_behavior_dialog_open(preferences);
        CHECK(preferences->font_reload_pending);
        bongo_cat_preferences_model_glyphs(app, preferences->glyph_ranges,
            sizeof(preferences->glyph_ranges) / sizeof(preferences->glyph_ranges[0]));
        CHECK(bongo_cat_preferences_behavior_glyphs_ready(preferences));
        preferences->font_reload_pending = false;
        bongo_cat_preferences_behavior_dialog_open(preferences);
        CHECK(!preferences->font_reload_pending);
        CHECK(bongo_cat_behaviors_reserve(&app->behaviors, 600, NULL));
        app->behaviors.count = 600;
        for (size_t i = 0; i < app->behaviors.count; ++i) {
            char *label = app->behaviors.entries[i].label;
            int bytes = nk_utf_encode((nk_rune)(0x4e00 + i * 4), label,
                BONGO_CAT_ID_CAP);
            bytes += nk_utf_encode((nk_rune)(0x4e02 + i * 4), label + bytes,
                BONGO_CAT_ID_CAP - bytes);
            label[bytes] = '\0';
        }
        CHECK(!bongo_cat_preferences_behavior_glyphs_ready(preferences));
        bongo_cat_preferences_model_glyphs(app, preferences->glyph_ranges,
            sizeof(preferences->glyph_ranges) / sizeof(preferences->glyph_ranges[0]));
        CHECK(bongo_cat_preferences_behavior_glyphs_ready(preferences));
        CHECK(range_has(preferences->glyph_ranges, 0x4e02 + 599 * 4));
        free(preferences);
    }
    bongo_cat_behaviors_clear(&app->behaviors); free(app);
}

static void font_reload_defers_during_frame(void) {
    BongoCatPreferences value = {0};
    value.ui_initialized = true;
    value.ui.frame_building = true;
    CHECK(bongo_cat_preferences_reload_fonts(&value));
    CHECK(value.font_reload_pending);
    CHECK(value.render_dirty);
    BongoCatUIBackend backend = {0};
    backend.frame_building = true;
    CHECK(!bongo_cat_ui_font_atlas_reload(&backend, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, 1.0f));
}

static void model_change_reloads_glyphs_before_drawing(void) {
    for (int page = 0; page < 4; ++page) {
        BongoCatPreferences value = {0};
        value.page = page;
        value.ui_initialized = true;
        value.font_reload_defer_once = true;
        bongo_cat_preferences_models_changed(&value);
        CHECK(value.render_dirty);
        CHECK(value.font_reload_pending && !value.font_reload_defer_once);
    }
    BongoCatPreferences unopened = {0};
    bongo_cat_preferences_models_changed(&unopened);
    CHECK(unopened.render_dirty && !unopened.font_reload_pending);
}

static void model_visual_expires_without_window(void) {
    BongoCatPreferences value = {0};
    value.model_load_visual_active = true;
    value.model_load_visual_started_ns = SDL_GetTicksNS() -
        BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS;
    CHECK(!bongo_cat_preferences_needs_frame(&value));
    CHECK(!value.model_load_visual_active && value.model_load_progress == 1.0f);
}

static void model_visual_curve(void) {
    BongoCatPreferences value = {0};
    bongo_cat_preferences_model_visual_begin(&value, "curve");
    value.model_loading = true;
    uint64_t now = SDL_GetTicksNS();
    value.model_load_visual_started_ns = now -
        BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    float ramp = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(ramp > .79f && ramp < .81f);
    value.model_load_visual_started_ns = now - 4000000000ull;
    float waiting = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(waiting > .86f && waiting < .89f);
    value.model_loading = false;
    value.model_load_visual_completion_ns = SDL_GetTicksNS() -
        BONGO_CAT_MODEL_LOAD_VISUAL_COMPLETE_NS;
    float complete = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(complete == 1.0f && !value.model_load_visual_active);
}

int main(void) {
    test_mver_config();
    test_mver_pointer_modes();
    test_mver_audio();
    failures += test_preferences_text();
    CHECK(chord("[17,65]", true, "Control+A"));
    CHECK(chord("[0]", true, "Gamepad:South"));
    CHECK(chord("[15]", true, "Gamepad:Select"));
    CHECK(chord("[8]", false, "Backspace"));
    CHECK(chord("[24]", false, "24"));
    CHECK(chord("[16,65]", false, "Shift+A"));
    CHECK(chord("[17,18,90]", false, "Control+Alt+Z"));
    CHECK(chord("[0]", false, ""));
    CHECK(chord("[16]", false, "Shift"));
    CHECK(chord("[17,65,66]", true, "Control+A+B"));
    CHECK(chord("[-1]", false, NULL));
    CHECK(chord("[17,255]", true, NULL));
    BongoCatMverKeyNames modifier = bongo_cat_mver_device_names(16, 1, 2);
    CHECK(modifier.count == 1 && strcmp(modifier.items[0], "ShiftRight") == 0);
    BongoCatMverKeyNames dpad = bongo_cat_mver_gamepad_names(12);
    CHECK(dpad.count == 1 && strcmp(dpad.items[0], "DPadUp") == 0);
    BongoCatMverKeyNames numeric = bongo_cat_mver_device_names(24, 0, 1);
    CHECK(numeric.count == 1 && strcmp(numeric.generated, "24") == 0);
    labels_from_shortcut_rows();
    metadata_backfills_labels();
    failures += test_mver_missing_motion_groups();
    behavior_labels_add_font_glyphs();
    font_reload_defers_during_frame();
    model_change_reloads_glyphs_before_drawing();
    model_visual_expires_without_window();
    model_visual_curve();
    failures += test_mver_nearby_identity();
    failures += test_mver_nearby_refresh();
    failures += test_mver_policy();
    failures += test_model_import_identity();
    test_mver_container_discovery();
    test_model_import_source();
    test_model_import_archive();
    test_mver_manifest();
    test_tauri_portable();
    failures += test_slim_package();
    return failures ? 1 : 0;
}
