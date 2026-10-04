#include "model_import.h"
#include "model_import_mver.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "model_storage.h"
#include "preferences_internal.h"
#include "preferences_overlay.h"
#include "preferences_state.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <yyjson.h>

static bool overlay_input_self_test(void);
static bool overlay_chrome_self_test(void);
static bool ui_input_reset_self_test(void);

const char *test_mver_pointer_config(bool live2d) {
    const char *prefix = "{\"decoration\":{\"l2d_correct\":1.987,"
        "\"l2d_offset\":[0,-0.005],\"l2d_horizontal_flip\":true,"
        "\"window_size\":[1400,1400],\"offsetX\":[10,11],"
        "\"offsetY\":[-10,-65],\"scalar\":[1,1],"
        "\"armLineColor\":[1,2,3],\"hand_offset\":[4,5],"
        "\"leftHanded\":true,\"mouse_force_move\":true,"
        "\"mouse_speed\":1.25},\"workarea\":{\"workarea\":true,"
        "\"top_left\":[100,200],\"right_bottom\":[2100,1400]},";
    const char *suffix = ",\"mouse\":false,"
        "\"hand_offset\":[4,5],"
        "\"keyboard\":[[65]],\"hand\":[[65]],"
        "\"l2d_expression\":[[65],[66]]}}";
    static char config[1024];
    SDL_snprintf(config, sizeof(config), "%s\"standard\":{\"l2d\":%s%s",
        prefix, live2d ? "true" : "false", suffix);
    return config;
}

bool test_mver_pointer_fixture_assets(const char *standard,
    const char *source) {
    static const char *const names[] = {
        "arm.png", "tablet.png", "tablet_left.png", "tablet_right.png"
    };
    char path[BONGO_CAT_PATH_CAP];
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (!bongo_cat_path_join(path, sizeof(path), standard, names[index]) ||
            !SDL_CopyFile(source, path)) return false;
    return true;
}

bool test_mver_pointer_adapter(const char *adapter, bool expected_enabled) {
    if (!overlay_input_self_test() || !overlay_chrome_self_test() ||
        !ui_input_reset_self_test()) return false;
    char path[BONGO_CAT_PATH_CAP];
    bool files = bongo_cat_path_join(path, sizeof(path), adapter,
        "resources/mver-pointer/arm.png") && bongo_cat_path_is_file(path) &&
        bongo_cat_path_join(path, sizeof(path), adapter,
            "resources/mver-pointer/tablet.png") && bongo_cat_path_is_file(path) &&
        bongo_cat_path_join(path, sizeof(path), adapter,
            ".bongo-cat-adapter.json");
    yyjson_doc *metadata = files ? bongo_cat_json_read_file(path, 0, NULL) : NULL;
    yyjson_val *pointer = metadata ? yyjson_obj_get(
        yyjson_doc_get_root(metadata), "standardPointer") : NULL;
    bool valid = yyjson_is_obj(pointer) &&
        yyjson_get_bool(yyjson_obj_get(pointer, "enabled")) == expected_enabled &&
        !yyjson_get_bool(yyjson_obj_get(pointer, "mouse")) &&
        yyjson_get_num(yyjson_obj_get(pointer, "offsetY")) == -65.0 &&
        yyjson_get_num(yyjson_obj_get(pointer, "handOffsetX")) == 4.0 &&
        yyjson_get_int(yyjson_obj_get(pointer, "lineBlue")) == 3;
    yyjson_doc_free(metadata);
    BongoCatLive2DRenderOptions render = {0};
    return valid && bongo_cat_import_render_options(adapter, &render) &&
        !render.auto_frame &&
        render.pointer_left_handed && render.mouse_force_move &&
        render.mouse_speed > 1.249f && render.mouse_speed < 1.251f;
}

void test_mver_pointer_modes(void) {
    char *current = SDL_GetCurrentDirectory();
    CHECK(current != NULL);
    if (!current) return;
    char root[BONGO_CAT_PATH_CAP], adapter[BONGO_CAT_PATH_CAP];
    char config_path[BONGO_CAT_PATH_CAP], config[2048];
    SDL_snprintf(root, sizeof(root), "%spointer-modes-%llu", current,
        (unsigned long long)SDL_GetTicksNS());
    SDL_free(current);
    CHECK(mver_fixture(root));
    CHECK(child(adapter, sizeof(adapter), root, "adapter", true));
    CHECK(child(config_path, sizeof(config_path), root, "config.json", false));
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_mver_discover(root, &discovery, &error) == 1);
    CHECK(discovery.count == 1);
    if (discovery.count == 1) {
        const struct { int mode; bool live2d; bool enabled; } cases[] = {
            {1, true, false}, {1, false, true}, {98, true, true}
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            SDL_snprintf(config, sizeof(config), "{\"mode\":%d,%s",
                cases[i].mode, test_mver_pointer_config(cases[i].live2d) + 1);
            CHECK(write_text(config_path, config));
            CHECK(bongo_cat_import_prepare_adapter(&discovery.candidates[0],
                adapter, &error));
            CHECK(test_mver_pointer_adapter(adapter, cases[i].enabled));
        }
    }
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}

static bool overlay_input_self_test(void) {
    struct nk_context context = {0};
    struct nk_mouse_button *left =
        &context.input.mouse.buttons[NK_BUTTON_LEFT];
    bool armed = false;
    left->down = nk_true; left->clicked = 1;
    if (bongo_cat_preferences_overlay_input_ready(&context, &armed) || armed)
        return false;
    left->clicked = 0;
    if (bongo_cat_preferences_overlay_input_ready(&context, &armed) || armed)
        return false;
    left->down = nk_false; left->clicked = 1;
    if (bongo_cat_preferences_overlay_input_ready(&context, &armed) || !armed)
        return false;
    left->clicked = 2;
    return bongo_cat_preferences_overlay_input_ready(&context, &armed);
}

static bool overlay_chrome_self_test(void) {
    BongoCatApp *app = SDL_calloc(1, sizeof(*app));
    BongoCatPreferences *preferences = SDL_calloc(1, sizeof(*preferences));
    bool passed = app && preferences;
    if (passed) {
        preferences->app = app;
        passed = bongo_cat_preferences_chrome_drag_allowed(preferences);
        preferences->behavior_dialog = true;
        passed = passed &&
            !bongo_cat_preferences_chrome_drag_allowed(preferences);
    }
    SDL_free(preferences);
    SDL_free(app);
    return passed;
}

static bool ui_input_reset_self_test(void) {
    BongoCatUIBackend ui = {0};
    struct nk_mouse *mouse = &ui.context.input.mouse;
    mouse->pos = nk_vec2(42.0f, 24.0f);
    mouse->scroll_delta = nk_vec2(1.0f, -2.0f);
    mouse->buttons[NK_BUTTON_LEFT].down = nk_true;
    mouse->buttons[NK_BUTTON_LEFT].clicked = 2;
    mouse->buttons[NK_BUTTON_RIGHT].down = nk_true;
    mouse->buttons[NK_BUTTON_RIGHT].clicked = 1;
    ui.last_left_click_ns = 99;
    ui.double_click_down = true;
    bongo_cat_ui_input_reset(&ui);
    return mouse->pos.x == -1.0f && mouse->pos.y == -1.0f &&
        mouse->scroll_delta.x == 0.0f && mouse->scroll_delta.y == 0.0f &&
        !mouse->buttons[NK_BUTTON_LEFT].down &&
        !mouse->buttons[NK_BUTTON_LEFT].clicked &&
        !mouse->buttons[NK_BUTTON_RIGHT].down &&
        !mouse->buttons[NK_BUTTON_RIGHT].clicked &&
        !ui.last_left_click_ns && !ui.double_click_down;
}
