#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "runtime.h"
#include "test_mver_support.h"
#include "test_mver_import_internal.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"
#include "../../src/runtime/model/mver/mver_render.h"
#include "../../src/runtime/model/mver/mver_config_text.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static void compatible_keyboard_bindings(void) {
    static const struct { const char *row; const char *shortcut; } cases[] = {
        {"[17]", "Control"}, {"[16,17]", "Shift+Control"},
        {"[91]", "Meta"}, {"[1]", "Left"}, {"[17,2]", "Control+Right"},
        {"[162,65]", "ControlLeft+A"}, {"[165]", "AltGr"},
        {"[144]", "NumLock"}, {"[124,219]", "F13+BracketLeft"},
        {"[18,187]", "Alt+="},
        {"[18,189]", "Alt+-"},
        {"[18,219]", "Alt+BracketLeft"},
        {"[18,221]", "Alt+BracketRight"},
        {"[18,220]", "Alt+Backslash"},
        {"[18,186]", "Alt+Semicolon"},
        {"[18,222]", "Alt+Quote"},
        {"[18,188]", "Alt+Comma"},
        {"[18,190]", "Alt+Period"},
        {"[18,191]", "Alt+Slash"},
        {"[18,192]", "Alt+BackQuote"},
        {"[18,107]", "Alt+KpPlus"},
        {"[18,109]", "Alt+KpMinus"},
        {"[18,106]", "Alt+KpMultiply"},
        {"[18,111]", "Alt+KpDivide"},
        {"[18,110]", "Alt+KpDecimal"},
        {"[0]", ""}, {"[255]", ""}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        yyjson_doc *doc = yyjson_read(cases[i].row, strlen(cases[i].row), 0);
        char shortcut[BONGO_CAT_SHORTCUT_CAP];
        CHECK(bongo_cat_mver_keyboard_chord(yyjson_doc_get_root(doc),
            shortcut, sizeof(shortcut)));
        CHECK(!strcmp(shortcut, cases[i].shortcut));
        yyjson_doc_free(doc);
        if (!shortcut[0]) continue;
        char codes[BONGO_CAT_SHORTCUT_CAP];
        CHECK(bongo_cat_mver_shortcut_codes(shortcut, codes, sizeof(codes)));
        CHECK(!strcmp(codes, cases[i].row));
    }
    /* Invalid numbers must not wrap into valid keys, and small buffers fail. */
    const char *invalid[] = {"[4294967361]", "[-1]", "[17,255]"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        yyjson_doc *doc = yyjson_read(invalid[i], strlen(invalid[i]), 0);
        char shortcut[BONGO_CAT_SHORTCUT_CAP];
        CHECK(!bongo_cat_mver_keyboard_chord(yyjson_doc_get_root(doc),
            shortcut, sizeof(shortcut)));
        yyjson_doc_free(doc);
    }
    yyjson_doc *doc = yyjson_read("[144]", 5, 0);
    char small[2];
    CHECK(!bongo_cat_mver_keyboard_chord(yyjson_doc_get_root(doc), small, sizeof(small)));
    yyjson_doc_free(doc);
}

static size_t runtime_binding_count(const BongoCatApp *app) {
    size_t count = 0;
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next)
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next) count++;
    return count;
}

static void large_model_bindings(const char *root, const BongoCatImportCandidate *candidate) {
    enum { ACTIONS = 1300 };
    char config_path[BONGO_CAT_PATH_CAP], manifest_path[BONGO_CAT_PATH_CAP];
    CHECK(child(config_path, sizeof(config_path), root, "config.json", false));
    CHECK(child(manifest_path, sizeof(manifest_path), candidate->directory, candidate->setting, false));
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *manifest = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, manifest);
    yyjson_mut_obj_add_int(doc, manifest, "Version", 3);
    yyjson_mut_val *refs = yyjson_mut_obj_add_obj(doc, manifest, "FileReferences");
    yyjson_mut_val *expressions = yyjson_mut_obj_add_arr(doc, refs, "Expressions");
    for (size_t i = 0; i < ACTIONS; ++i) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, expressions);
        char name[32]; snprintf(name, sizeof(name), "Expression-%zu", i);
        yyjson_mut_obj_add_strcpy(doc, item, "Name", name);
        yyjson_mut_obj_add_str(doc, item, "File", "expression.json");
    }
    CHECK(bongo_cat_json_write_file(manifest_path, doc, 0, NULL));
    yyjson_mut_doc_free(doc);
    doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *config = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, config);
    yyjson_mut_val *standard = yyjson_mut_obj_add_obj(doc, config, "standard");
    yyjson_mut_val *keys = yyjson_mut_obj_add_arr(doc, standard, "l2d_expression");
    for (size_t i = 0; i < ACTIONS; ++i) {
        yyjson_mut_val *row = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_int(doc, row, 65);
        yyjson_mut_arr_add_val(keys, row);
    }
    CHECK(bongo_cat_json_write_file(config_path, doc, 0, NULL));
    yyjson_mut_doc_free(doc);
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) return;
    /* A full private override store must not restrict file-backed keys. */
    app->settings.behavior_shortcut_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < BONGO_CAT_BEHAVIOR_BINDING_CAP; ++i) {
        BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        snprintf(value->id, sizeof(value->id), "private-%zu:expression:0", i);
        snprintf(value->label, sizeof(value->label), "Custom-%zu", i);
    }
    app->models.count = 2;
    for (size_t i = 0; i < app->models.count; ++i) {
        BongoCatModelEntry *model = &app->models.entries[i];
        snprintf(model->id, sizeof(model->id), "large-%zu", i);
        snprintf(model->directory, sizeof(model->directory), "%s", candidate->directory);
        snprintf(model->setting_file, sizeof(model->setting_file), "%s", candidate->setting);
        snprintf(model->adapter_directory, sizeof(model->adapter_directory), "%s", root);
        model->mode = i ? BONGO_CAT_MODE_GAMEPAD : BONGO_CAT_MODE_STANDARD;
        model->source_format = BONGO_CAT_MODEL_SOURCE_MVER;
        CHECK(bongo_cat_mver_shortcuts_load(app, model, NULL));
    }
    CHECK(runtime_binding_count(app) == 2 * ACTIONS);
    CHECK(app->settings.behavior_shortcut_count == BONGO_CAT_BEHAVIOR_BINDING_CAP);
    CHECK(!strcmp(app->settings.behavior_shortcuts[255].label, "Custom-255"));
    BongoCatBehaviorShortcut *last = bongo_cat_app_behavior_binding_mut(app, "large-0:expression:1299");
    CHECK(last && !strcmp(last->shortcut, "A"));
    /* Sound edge state is indexed by current behavior, including indexes >255. */
    CHECK(bongo_cat_behaviors_reserve(&app->behaviors, ACTIONS, NULL));
    app->behaviors.count = ACTIONS;
    BongoCatBehaviorEntry *sound = &app->behaviors.entries[ACTIONS - 1];
    sound->kind = BONGO_CAT_BEHAVIOR_SOUND;
    snprintf(sound->id, sizeof(sound->id), "large-0:expression:1299");
    BongoCatInputEvent press = {.kind = BONGO_CAT_INPUT_KEY_DOWN};
    snprintf(press.name, sizeof(press.name), "KeyA");
    bool changed = false;
    CHECK(bongo_cat_app_sound_shortcuts(app, &press, &changed) && changed);
    CHECK(app->behaviors.entries[ACTIONS - 1].shortcut_active);
    press.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_app_sound_shortcuts(app, &press, &changed);
    CHECK(changed && !app->behaviors.entries[ACTIONS - 1].shortcut_active);
    for (int i = 0; i < 4; ++i)
        CHECK(bongo_cat_mver_shortcuts_load(app, &app->models.entries[0], NULL));
    CHECK(runtime_binding_count(app) == 2 * ACTIONS);
    CHECK(last == bongo_cat_app_behavior_binding(app, "large-0:expression:1299"));
    CHECK(last && bongo_cat_app_behavior_binding_target(app, last->shortcut) == last);
    CHECK(last && !bongo_cat_app_shortcut_conflicts(app, "A", last->shortcut));
    snprintf(app->settings.shortcuts.mirror, BONGO_CAT_SHORTCUT_CAP, "Control+M");
    CHECK(last && bongo_cat_app_shortcut_conflicts(app, "Control+M", last->shortcut));
    CHECK(bongo_cat_app_shortcut_conflicts(app, "A", app->settings.shortcuts.mirror));
    /* A failed reread must not leave a partially cleared cache. */
    CHECK(write_text(config_path, "{broken"));
    CHECK(!bongo_cat_mver_shortcuts_load(app, &app->models.entries[0], NULL));
    CHECK(last && !strcmp(last->shortcut, "A"));
    CHECK(runtime_binding_count(app) == 2 * ACTIONS);
    CHECK(write_text(config_path, "{standard:{l2d_expression:[]}}"));
    CHECK(bongo_cat_model_shortcut_save(app, "large-0:expression:1299", "Control+F24", NULL));
    const BongoCatBehaviorShortcut *shared = bongo_cat_app_behavior_binding(app, "large-1:expression:1299");
    CHECK(last && !strcmp(last->shortcut, "Control+F24"));
    CHECK(shared && !strcmp(shared->shortcut, "Control+F24"));
    CHECK(bongo_cat_model_shortcut_save(app, "large-1:expression:1299", "", NULL));
    CHECK(last && last->shortcut_disabled && !last->shortcut[0]);
    CHECK(shared && shared->shortcut_disabled && !shared->shortcut[0]);
    CHECK(runtime_binding_count(app) == 2 * ACTIONS);
    app->models.count = 1;
    bongo_cat_app_model_shortcuts_prune(app);
    CHECK(runtime_binding_count(app) == ACTIONS);
    CHECK(last == bongo_cat_app_behavior_binding(app, "large-0:expression:1299"));
    bongo_cat_app_model_shortcuts_clear(app);
    CHECK(!app->model_shortcuts);
    bongo_cat_behaviors_clear(&app->behaviors);
    free(app);
}

void test_mver_config(void) {
    compatible_keyboard_bindings();
    static const char config[] =
        "{standard:{l2d_expression:[[124,219,//Standard expression\n]],"
        "l2d_motion:[[124,57,//Standard motion\n]],"
        "l2d_motion_lockhand:[[8,//Shared lock\n]],sounds:[]},"
        "keyboard:{l2d_expression:[[67,//Keyboard expression\n]],"
        "l2d_motion:[[68,//Keyboard motion\n]],"
        "l2d_motion_lockhand:[[69,//Ignored keyboard lock\n]],sounds:[]},"
        "gamepad:{l2d_expression:[[70,//Ignored gamepad expression\n]],"
        "l2d_motion:[[71,//Ignored gamepad motion\n]],"
        "l2d_motion_lockhand:[[72,//Ignored gamepad lock\n]],"
        "sounds:[[73,//Gamepad sound\n]]}}";
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-shared-bindings-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(mver_fixture(root));
    BongoCatImportCandidate candidate = {0};
    CHECK(child(candidate.directory, sizeof(candidate.directory), root,
        "img/standard/cat_model", false));
    snprintf(candidate.setting, sizeof(candidate.setting), "cat.model3.json");
    CHECK(child(path, sizeof(path), candidate.directory, candidate.setting, false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"],"
        "\"Expressions\":[{\"Name\":\"Expression\",\"File\":\"expression.json\"}],"
        "\"Motions\":{\"CAT_motion\":[{\"File\":\"motion.json\"}],"
        "\"CAT_motion_lock\":[{\"File\":\"motion.json\"}]}}}"));
    CHECK(child(path, sizeof(path), candidate.directory, "expression.json", false));
    CHECK(write_text(path, "{}"));
    CHECK(child(path, sizeof(path), candidate.directory, "motion.json", false));
    CHECK(write_text(path, "{}"));
    CHECK(child(path, sizeof(path), root, "config.json", false));
    static const struct { const char *json; bool keyboard; } input_modes[] = {
        {"{gamepad:{input_mode:0}}", true},
        {"{gamepad:{input_mode:1}}", false},
        {"{gamepad:{input_mode:2}}", false},
        {"{gamepad:{}}", false},
        {"{gamepad:{input_mode:false}}", false},
        {"{gamepad:{input_mode:\"0\"}}", false},
        {"{broken", false}
    };
    for (size_t i = 0; i < sizeof(input_modes) / sizeof(input_modes[0]); ++i) {
        CHECK(write_text(path, input_modes[i].json));
        CHECK(bongo_cat_mver_gamepad_keyboard(candidate.directory) ==
            input_modes[i].keyboard);
    }
    CHECK(write_text(path, config));
    CHECK(bongo_cat_mver_gamepad_input_mode(candidate.directory) == -1);
    yyjson_doc *document = yyjson_read(config, strlen(config), YYJSON_READ_JSON5);
    CHECK(document != NULL);
    const BongoCatModelMode modes[] = {
        BONGO_CAT_MODE_STANDARD, BONGO_CAT_MODE_KEYBOARD, BONGO_CAT_MODE_GAMEPAD
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        candidate.mode = modes[i];
        candidate.gamepad_buttons = modes[i] == BONGO_CAT_MODE_GAMEPAD;
        BongoCatMverLabels labels = {0};
        CHECK(bongo_cat_mver_labels_load(path, bongo_cat_mode_name(modes[i]), &labels));
        yyjson_mut_doc *output = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *items = yyjson_mut_arr(output);
        yyjson_mut_doc_set_root(output, items);
        BongoCatError error = {0};
        CHECK(bongo_cat_mver_add_behaviors(output, items,
            yyjson_doc_get_root(document), &candidate, &labels, &error));
        CHECK(yyjson_mut_arr_size(items) == 3);
        const char *keys[] = {i == 1 ? "C" : "F13+BracketLeft",
            i == 1 ? "D" : "F13+9", "Backspace"};
        const char *names[] = {i == 1 ? "Keyboard expression" : "Standard expression",
            i == 1 ? "Keyboard motion" : "Standard motion", "Shared lock"};
        for (size_t j = 0; j < 3; ++j) {
            yyjson_mut_val *item = yyjson_mut_arr_get(items, j);
            const char *key = yyjson_mut_get_str(yyjson_mut_obj_get(item, "shortcut"));
            const char *label = yyjson_mut_get_str(yyjson_mut_obj_get(item, "label"));
            CHECK(key && strcmp(key, keys[j]) == 0);
            CHECK(label && strcmp(label, names[j]) == 0);
        }
        if (i == 2) {
            const char *sound = bongo_cat_mver_label(&labels, "sounds", 0);
            CHECK(sound && strcmp(sound, "Gamepad sound") == 0);
        }
        yyjson_mut_doc_free(output);
        bongo_cat_mver_labels_clear(&labels);
    }
    yyjson_doc_free(document);
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        /* A stored package must survive adapter generation and enter the list.
           These are the multi-key expression bindings used by Kati. */
        CHECK(child(app->cache_root, sizeof(app->cache_root), root, "cache", true));
        BongoCatError scan_error = {0};
        CHECK(bongo_cat_import_installed_package(app, temporary,
            bongo_cat_path_name(root), &scan_error) == BONGO_CAT_OK);
        CHECK(app->models.count == 1);
        memset(&app->models, 0, sizeof(app->models));
        app->models.count = 3;
        for (size_t i = 0; i < 3; ++i) {
            BongoCatModelEntry *model = &app->models.entries[i];
            snprintf(model->id, sizeof(model->id), "shared-%zu", i);
            snprintf(model->directory, sizeof(model->directory), "%s", candidate.directory);
            snprintf(model->setting_file, sizeof(model->setting_file), "%s", candidate.setting);
            snprintf(model->adapter_directory, sizeof(model->adapter_directory), "%s", root);
            model->mode = modes[i]; model->source_format = BONGO_CAT_MODEL_SOURCE_MVER;
            CHECK(bongo_cat_mver_shortcuts_load(app, model, NULL));
        }
        const BongoCatBehaviorShortcut *expression =
            bongo_cat_app_behavior_binding(app, "shared-0:expression:0");
        CHECK(expression && !strcmp(expression->shortcut, "F13+BracketLeft"));
        snprintf(app->loaded_model, sizeof(app->loaded_model), "shared-0");
        CHECK(bongo_cat_behaviors_load(&app->behaviors, &app->models.entries[0],
            NULL) == BONGO_CAT_OK);
        CHECK(app->behaviors.count > 0);
        BongoCatBehaviorEntry *expression_behavior = NULL;
        for (size_t i = 0; i < app->behaviors.count; ++i) {
            BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
            if (!strcmp(entry->id, "shared-0:expression:0")) {
                expression_behavior = entry;
                break;
            }
        }
        CHECK(expression_behavior != NULL);
        if (expression_behavior) {
            BongoCatInputEvent key = {.kind = BONGO_CAT_INPUT_KEY_DOWN};
            snprintf(key.name, sizeof(key.name), "F13");
            bongo_cat_app_shortcuts(app, &key);
            CHECK(!expression_behavior->shortcut_active);
            snprintf(key.name, sizeof(key.name), "BracketLeft");
            bongo_cat_app_shortcuts(app, &key);
            CHECK(expression_behavior->shortcut_active);
            bongo_cat_app_shortcuts(app, &key); /* Key repeat keeps the chord held. */
            CHECK(expression_behavior->shortcut_active);
            key.kind = BONGO_CAT_INPUT_KEY_UP;
            snprintf(key.name, sizeof(key.name), "F13");
            bongo_cat_app_shortcuts(app, &key);
            CHECK(!expression_behavior->shortcut_active);
            key.kind = BONGO_CAT_INPUT_KEY_DOWN;
            bongo_cat_app_shortcuts(app, &key); /* Reverse press order. */
            CHECK(expression_behavior->shortcut_active);
            key.kind = BONGO_CAT_INPUT_KEY_UP;
            bongo_cat_app_shortcuts(app, &key);
            snprintf(key.name, sizeof(key.name), "BracketLeft");
            bongo_cat_app_shortcuts(app, &key);
        }
        bongo_cat_behaviors_clear(&app->behaviors);
        char encoded[BONGO_CAT_SHORTCUT_CAP];
        CHECK(bongo_cat_mver_shortcut_codes("F13+BracketLeft", encoded, sizeof(encoded)));
        CHECK(!strcmp(encoded, "[124,219]"));
        BongoCatError error = {0};
        CHECK(bongo_cat_model_shortcut_save(app, "shared-1:motion:CAT_motion_lock:0",
            "Control+Alt+F24", &error));
        CHECK(app->settings.behavior_shortcut_count == 0);
        for (size_t i = 0; i < 3; ++i) {
            char id[BONGO_CAT_BEHAVIOR_ID_CAP];
            snprintf(id, sizeof(id), "shared-%zu:motion:CAT_motion_lock:0", i);
            const BongoCatBehaviorShortcut *value = bongo_cat_app_behavior_binding(app, id);
            CHECK(value != NULL);
            if (!value) continue;
            CHECK(value->shortcut_external);
            if (strstr(value->id, ":motion:CAT_motion_lock:"))
                CHECK(!strcmp(value->shortcut, "Control+Alt+F24"));
        }
        document = bongo_cat_json_read_file(path, YYJSON_READ_JSON5, NULL);
        yyjson_val *root_value = yyjson_doc_get_root(document);
        yyjson_val *keys = yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(root_value,
            "standard"), "l2d_motion_lockhand"), 0);
        CHECK(yyjson_arr_size(keys) == 3);
        CHECK(yyjson_get_int(yyjson_arr_get(keys, 0)) == 17);
        CHECK(yyjson_get_int(yyjson_arr_get(keys, 1)) == 18);
        CHECK(yyjson_get_int(yyjson_arr_get(keys, 2)) == 135);
        keys = yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(root_value,
            "keyboard"), "l2d_motion_lockhand"), 0);
        CHECK(yyjson_get_int(yyjson_arr_get(keys, 0)) == 69);
        yyjson_doc_free(document);
        BongoCatMverLabels preserved = {0};
        CHECK(bongo_cat_mver_labels_load(path, "keyboard", &preserved));
        const char *label = bongo_cat_mver_label(&preserved, "l2d_motion_lockhand", 0);
        CHECK(label && !strcmp(label, "Shared lock"));
        bongo_cat_mver_labels_clear(&preserved);
        CHECK(bongo_cat_model_shortcut_save(app, "shared-2:motion:CAT_motion_lock:0", "", &error));
        CHECK(!bongo_cat_model_shortcut_save(app, "shared-2:motion:CAT_motion_lock:0",
            "NotAMverKey", &error));
        CHECK(app->settings.behavior_shortcut_count == 0);
        for (size_t i = 0; i < 3; ++i) {
            char id[BONGO_CAT_BEHAVIOR_ID_CAP];
            snprintf(id, sizeof(id), "shared-%zu:motion:CAT_motion_lock:0", i);
            const BongoCatBehaviorShortcut *value = bongo_cat_app_behavior_binding(app, id);
            CHECK(value != NULL);
            if (!value) continue;
            if (strstr(value->id, ":motion:CAT_motion_lock:"))
                CHECK(!value->shortcut[0] && value->shortcut_disabled);
        }
        char settings_path[BONGO_CAT_PATH_CAP];
        CHECK(child(settings_path, sizeof(settings_path), root, "settings.json", false));
        CHECK(bongo_cat_settings_save(settings_path, &app->settings, &error) == BONGO_CAT_OK);
        document = bongo_cat_json_read_file(settings_path, 0, NULL);
        yyjson_val *overrides = yyjson_obj_get(yyjson_doc_get_root(document), "behaviorOverrides");
        CHECK(yyjson_arr_size(overrides) == 0);
        size_t index, count; yyjson_val *item;
        yyjson_arr_foreach(overrides, index, count, item) {
            CHECK(!yyjson_obj_get(item, "shortcut"));
            CHECK(!yyjson_obj_get(item, "shortcutDisabled"));
        }
        yyjson_doc_free(document);
        /* Simulate Mver editing the file while our cached value is stale. */
        CHECK(bongo_cat_mver_config_write_row(path, "standard",
            "l2d_motion_lockhand", 0, "[18,49]", &error));
        CHECK(bongo_cat_mver_shortcuts_load(app, &app->models.entries[2], &error));
        const BongoCatBehaviorShortcut *reloaded = bongo_cat_app_behavior_binding(app,
            "shared-2:motion:CAT_motion_lock:0");
        CHECK(reloaded && !strcmp(reloaded->shortcut, "Alt+1"));
        bongo_cat_app_model_shortcuts_clear(app);
        bongo_cat_behaviors_clear(&app->behaviors);
    free(app);
    }
    CHECK(bongo_cat_mver_config_write_row(path, "standard", "l2d_motion", 3, "[65]", NULL));
    CHECK(bongo_cat_mver_config_write_row(path, "decoration", "soundClear", -1, "[17,222]", NULL));
    document = bongo_cat_json_read_file(path, YYJSON_READ_JSON5, NULL);
    yyjson_val *extended = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(document),
        "standard"), "l2d_motion");
    CHECK(yyjson_arr_size(extended) == 4);
    CHECK(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(extended, 1), 0)) == 255);
    CHECK(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(extended, 3), 0)) == 65);
    yyjson_doc_free(document);
    CHECK(write_text(path, "{standard:{l2d_motion:null},other:42}"));
    CHECK(bongo_cat_mver_config_write_row(path, "standard", "l2d_motion", 0, "[66]", NULL));
    CHECK(!bongo_cat_mver_config_write_row(path, "standard", "l2d_motion", 0, "[invalid]", NULL));
    document = bongo_cat_json_read_file(path, YYJSON_READ_JSON5, NULL);
    CHECK(yyjson_get_int(yyjson_obj_get(yyjson_doc_get_root(document), "other")) == 42);
    yyjson_doc_free(document);
    /* Reading authored geometry and editing model keys must leave global
       user preferences and the author's decoration fields independent. */
    static const char authored[] =
        "{decoration:{l2d_horizontal_flip:true,framerateLimit:144 /* fps */,"
        "topWindow:false,window_size:[800,400],rgb:[12,34,56],"
        "l2d_correct:1.25,l2d_offset:[3,-4],mouse_speed:2.5},"
        "standard:{l2d_motion:[[65]]},unrelated:{author:'preserve'}}";
    CHECK(write_text(path, authored));
    BongoCatLive2DRenderOptions options;
    CHECK(bongo_cat_mver_render_read(path, &options));
    CHECK(options.reference_width == 800 && options.reference_height == 400);
    CHECK(options.projection_scale == 1.25f && options.offset_y == -4.0f);
    CHECK(options.mouse_speed == 2.5f);
    size_t length = 0;
    char *text = mver_text_read(path, &length);
    CHECK(text && !strcmp(text, authored));
    free(text);
    CHECK(bongo_cat_mver_config_write_row(path, "standard", "l2d_motion", 0, "[66]", NULL));
    text = mver_text_read(path, &length);
    CHECK(text && strstr(text, "framerateLimit:144 /* fps */"));
    CHECK(text && strstr(text, "window_size:[800,400],rgb:[12,34,56]"));
    CHECK(text && strstr(text, "l2d_horizontal_flip:true") && strstr(text, "topWindow:false"));
    CHECK(text && strstr(text, "author:'preserve'"));
    free(text);
    BongoCatSettings *settings = calloc(1, sizeof(*settings));
    BongoCatSettings *loaded = calloc(1, sizeof(*loaded));
    CHECK(settings && loaded);
    if (settings && loaded) {
        bongo_cat_settings_defaults(settings);
        settings->model.mirror = false;
        settings->model.max_fps = 30;
        settings->window.always_on_top = true;
        settings->window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_MAGENTA;
        char own_path[BONGO_CAT_PATH_CAP];
        CHECK(child(own_path, sizeof(own_path), root, "global-settings.json", false));
        CHECK(bongo_cat_settings_save(own_path, settings, NULL) == BONGO_CAT_OK);
        CHECK(bongo_cat_settings_load(own_path, loaded, NULL) == BONGO_CAT_OK);
        CHECK(!loaded->model.mirror && loaded->model.max_fps == 30);
        CHECK(loaded->window.always_on_top);
        CHECK(loaded->window.obs_background_color == BONGO_CAT_OBS_BACKGROUND_MAGENTA);
        BongoCatSessionState session, restored;
        bongo_cat_session_defaults(&session);
        session.window.scale_percent = 150.0f;
        session.window.width = session.window.content_width = 600;
        session.window.height = session.window.content_height = 300;
        CHECK(child(own_path, sizeof(own_path), root, "global-session.json", false));
        CHECK(bongo_cat_session_save(own_path, &session, NULL) == BONGO_CAT_OK);
        CHECK(bongo_cat_session_load(own_path, &restored, NULL) == BONGO_CAT_OK);
        CHECK(restored.window.scale_percent == 150.0f);
        CHECK(restored.window.content_width == 600 && restored.window.content_height == 300);
        CHECK(bongo_cat_mver_render_read(path, &options));
        CHECK(options.reference_width == 800 && options.reference_height == 400);
    }
    free(settings); free(loaded);
    large_model_bindings(root, &candidate);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

