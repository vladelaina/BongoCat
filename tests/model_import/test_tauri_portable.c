#include "model_import.h"
#include "model_import_mver_internal.h"
#include "tauri/model_import_tauri.h"
#include "tauri/model_import_tauri_internal.h"
#include "model_storage.h"
#include "runtime.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "bongo_cat/image.h"
#include "bongo_cat/file.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
#include <stb_image_write.h>

static void write_canvas_bytes(void *context, void *data, int size) {
    CHECK(fwrite(data, 1, (size_t)size, context) == (size_t)size);
}

static bool write_input_canvas(const char *path, int width, int height) {
    unsigned char *pixels = calloc((size_t)width * height, 4);
    FILE *file = pixels ? bongo_cat_file_open(path, "wb") : NULL;
    bool ok = file && stbi_write_png_to_func(write_canvas_bytes, file,
        width, height, 4, pixels, width * 4);
    if (file && fclose(file) != 0) ok = false;
    free(pixels);
    return ok;
}

static void check_portable_images(const char *root,
    const char *const *relative, size_t count) {
    char path[BONGO_CAT_PATH_CAP];
    for (size_t i = 0; i < count; ++i) {
        CHECK(child(path, sizeof(path), root, relative[i], false));
        CHECK(bongo_cat_path_is_file(path));
        CHECK(bongo_cat_image_info(path, NULL, NULL));
    }
}

void test_tauri_portable(void) {
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], model[BONGO_CAT_PATH_CAP];
    char path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongocat-tauri-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(model, sizeof(model), root, "keyboard", true));
    CHECK(child(path, sizeof(path), model, "cat.model3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":["
        "\"resources/model-texture.png\"]}}"));
    CHECK(child(path, sizeof(path), model, "cat.moc3", false));
    CHECK(write_text(path, "MOC3"));
    char tray[BONGO_CAT_PATH_CAP];
    snprintf(tray, sizeof(tray), "%s/resources/assets/bongocat.png",
        BONGO_CAT_NATIVE_SOURCE_DIR);
    char resources[BONGO_CAT_PATH_CAP], left_keys[BONGO_CAT_PATH_CAP];
    char right_keys[BONGO_CAT_PATH_CAP], east[BONGO_CAT_PATH_CAP];
    char numeric[BONGO_CAT_PATH_CAP], model_texture[BONGO_CAT_PATH_CAP];
    CHECK(child(resources, sizeof(resources), model, "resources", true));
    CHECK(child(model_texture, sizeof(model_texture), resources,
        "model-texture.png", false));
    CHECK(SDL_CopyFile(tray, model_texture));
    CHECK(child(path, sizeof(path), resources, "background.png", false));
    CHECK(SDL_CopyFile(tray, path));
    CHECK(child(path, sizeof(path), resources, "cover.png", false));
    CHECK(SDL_CopyFile(tray, path));
    CHECK(child(left_keys, sizeof(left_keys), resources, "left-keys", true));
    CHECK(child(numeric, sizeof(numeric), left_keys, "24.png", false));
    CHECK(SDL_CopyFile(tray, numeric));
    CHECK(child(right_keys, sizeof(right_keys), resources, "right-keys", true));
    CHECK(child(east, sizeof(east), right_keys, "East.png", false));
    CHECK(SDL_CopyFile(tray, east));
    char nested[BONGO_CAT_PATH_CAP], nested_resources[BONGO_CAT_PATH_CAP];
    CHECK(child(nested, sizeof(nested), model, "nested", true));
    CHECK(child(nested_resources, sizeof(nested_resources), nested,
        "resources", true));
    CHECK(child(path, sizeof(path), nested_resources, "keep.bin", false));
    CHECK(write_text(path, "keep"));
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    /* A referenced Moc may live below the manifest directory. */
    CHECK(child(path, sizeof(path), model, "cat.model3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"nested/CAT.MOC3\",\"Textures\":["
        "\"resources/model-texture.png\"]}}"));
    CHECK(child(path, sizeof(path), nested, "CAT.MOC3", false));
    CHECK(write_text(path, "MOC3"));
    CHECK(bongo_cat_import_tauri_discover_exact(model, &discovery, &error) == 1);
    CHECK(discovery.count == 1 &&
        discovery.candidates[0].format == BONGO_CAT_IMPORT_TAURI &&
        discovery.candidates[0].mode == BONGO_CAT_MODE_GAMEPAD &&
        discovery.candidates[0].gamepad_buttons);
    CHECK(bongo_cat_path_remove(east));
    CHECK(child(path, sizeof(path), right_keys, "KeyA.png", false));
    CHECK(SDL_CopyFile(tray, path));
    BongoCatImportDiscovery keyboard = {0};
    CHECK(bongo_cat_import_tauri_discover_exact(model, &keyboard, &error) == 1);
    CHECK(keyboard.count == 1 &&
        keyboard.candidates[0].mode == BONGO_CAT_MODE_KEYBOARD &&
        !keyboard.candidates[0].gamepad_buttons);
    char models_root[BONGO_CAT_PATH_CAP];
    CHECK(child(models_root, sizeof(models_root), root, "models", true));
    BongoCatImportReceipt receipt = {0};
    CHECK(child(path, sizeof(path), nested, "CAT.MOC3", false));
    CHECK(bongo_cat_import_install(path, models_root, &receipt, &error) ==
        BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1 &&
        strcmp(receipt.ids[0], "keyboard") == 0);
    BongoCatImportReceipt folder_receipt = {0};
    CHECK(bongo_cat_import_install(model, models_root, &folder_receipt,
        &error) == BONGO_CAT_OK);
    CHECK(folder_receipt.count == receipt.count &&
        folder_receipt.installed_count == 0 &&
        strcmp(folder_receipt.ids[0], receipt.ids[0]) == 0);
    BongoCatImportSession *session = bongo_cat_import_session_create(
        models_root, &error);
    BongoCatImportBatchStats stats = {0};
    CHECK(session != NULL);
    CHECK(session && bongo_cat_import_session_install_progressive(session,
        path, NULL, NULL, &stats, &error) == BONGO_CAT_OK);
    CHECK(stats.succeeded_count == 1 && stats.failed_count == 0);
    bongo_cat_import_session_destroy(session);
    char package[BONGO_CAT_PATH_CAP];
    CHECK(child(package, sizeof(package), models_root, receipt.ids[0], false));
    CHECK(child(path, sizeof(path), package, "config.json", false) &&
        bongo_cat_path_is_file(path));
    yyjson_doc *normalized = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *normalized_root = normalized
        ? yyjson_doc_get_root(normalized) : NULL;
    yyjson_val *normalized_mode = yyjson_obj_get(normalized_root, "keyboard");
    yyjson_val *normalized_left = yyjson_obj_get(normalized_mode, "lefthand");
    yyjson_val *window_size = yyjson_obj_get(
        yyjson_obj_get(normalized_root, "decoration"), "window_size");
    yyjson_val *l2d_correct = yyjson_obj_get(
        yyjson_obj_get(normalized_root, "decoration"), "l2d_correct");
    int expected_width = 0, expected_height = 0;
    CHECK(bongo_cat_image_info(tray, &expected_width, &expected_height));
    CHECK(yyjson_get_int(yyjson_obj_get(normalized_root, "mode")) == 2 &&
        yyjson_is_arr(normalized_left) &&
        yyjson_is_arr(yyjson_obj_get(normalized_mode, "righthand")) &&
        yyjson_get_int(yyjson_arr_get(yyjson_arr_get(normalized_left, 0), 0)) ==
            24 &&
        yyjson_get_int(yyjson_arr_get(window_size, 0)) == expected_width &&
        yyjson_get_int(yyjson_arr_get(window_size, 1)) == expected_height &&
        yyjson_is_num(l2d_correct) && yyjson_get_num(l2d_correct) == 2.0);
    yyjson_doc_free(normalized);
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/cat.model3.json", false) &&
        bongo_cat_path_is_file(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/resources/model-texture.png", false) &&
        bongo_cat_path_is_file(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/resources/right-keys", false) &&
        !bongo_cat_path_is_dir(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/nested/resources/keep.bin", false) &&
        bongo_cat_path_is_file(path));
    static const char *const keyboard_runtime[] = {
        "img/keyboard/lefthand/leftup.png",
        "img/keyboard/righthand/rightup.png"
    };
    check_portable_images(package, keyboard_runtime,
        sizeof(keyboard_runtime) / sizeof(keyboard_runtime[0]));
    CHECK(child(path, sizeof(path), package, ".bongo-cat-adapter", false) &&
        !bongo_cat_path_is_dir(path));
    CHECK(child(path, sizeof(path), package, ".bongo-cat-package.json", false) &&
        !bongo_cat_path_is_file(path));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        snprintf(app->models_root, sizeof(app->models_root), "%s", models_root);
        CHECK(child(app->cache_root, sizeof(app->cache_root), root, "cache",
            true));
        bongo_cat_models_init(&app->models);
        CHECK(bongo_cat_import_installed_models(app, models_root, &error) ==
            BONGO_CAT_OK);
        CHECK(app->models.count == 1 &&
            app->models.entries[0].source_format == BONGO_CAT_MODEL_SOURCE_MVER &&
            strcmp(app->models.entries[0].storage_directory, package) == 0 &&
            strcmp(app->models.entries[0].adapter_directory, package) != 0);
        free(app);
    }
    BongoCatImportCandidate portable = keyboard.candidates[0], installed = {0};
    char portable_root[BONGO_CAT_PATH_CAP];
    portable.mode = BONGO_CAT_MODE_STANDARD;
    portable.gamepad_buttons = false;
    CHECK(child(portable_root, sizeof(portable_root), root,
        "portable-standard", false));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_tauri_convert_to_mver(&portable, portable_root,
        &installed, &error));
    static const char *const standard_runtime[] = {
        "img/standard/arm.png", "img/standard/up.png",
        "img/standard/mousebg.png", "img/standard/l2dmousebg.png",
        "img/standard/mouse.png", "img/standard/mouse_left.png",
        "img/standard/mouse_right.png", "img/standard/mouse_side.png",
        "img/standard/tabletbg.png", "img/standard/l2dtabletbg.png",
        "img/standard/tablet.png", "img/standard/tablet_left.png",
        "img/standard/tablet_right.png"
    };
    check_portable_images(portable_root, standard_runtime,
        sizeof(standard_runtime) / sizeof(standard_runtime[0]));
    CHECK(child(path, sizeof(path), portable_root, "config.json", false));
    yyjson_doc *standard_config = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *standard_root = standard_config
        ? yyjson_doc_get_root(standard_config) : NULL;
    yyjson_val *standard_decoration = yyjson_obj_get(standard_root,
        "decoration");
    yyjson_val *standard_l2d_correct = yyjson_obj_get(standard_decoration,
        "l2d_correct");
    CHECK(yyjson_get_int(yyjson_obj_get(standard_root, "mode")) == 1 &&
        yyjson_is_arr(yyjson_obj_get(yyjson_obj_get(standard_root,
            "standard"), "hand")) && yyjson_is_num(standard_l2d_correct) &&
        yyjson_get_num(standard_l2d_correct) == 2.0);
    yyjson_doc_free(standard_config);
    CHECK(child(path, sizeof(path), portable_root, "img/standard/bg.png",
        false) && !bongo_cat_path_is_file(path));
    char adapter_root[BONGO_CAT_PATH_CAP];
    CHECK(child(adapter_root, sizeof(adapter_root), root,
        "portable-standard-adapter", true));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_prepare_adapter(&installed, adapter_root, &error));
    CHECK(child(path, sizeof(path), adapter_root,
        BONGO_CAT_MODEL_ADAPTER_FILE, false));
    yyjson_doc *adapter = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *pointer = adapter ? yyjson_obj_get(
        yyjson_doc_get_root(adapter), "standardPointer") : NULL;
    CHECK(yyjson_is_obj(pointer) &&
        !yyjson_get_bool(yyjson_obj_get(pointer, "enabled")));
    yyjson_doc_free(adapter);
    BongoCatLive2DRenderOptions render = {0};
    CHECK(bongo_cat_import_render_options(adapter_root, &render) &&
        render.mver_projection && render.auto_frame);
    CHECK(child(path, sizeof(path), adapter_root,
        "resources/mver-pointer/arm.png", false) &&
        !bongo_cat_path_is_file(path));
    portable.mode = BONGO_CAT_MODE_GAMEPAD;
    portable.gamepad_buttons = true;
    installed = (BongoCatImportCandidate){0};
    CHECK(child(portable_root, sizeof(portable_root), root,
        "portable-gamepad", false));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_tauri_convert_to_mver(&portable, portable_root,
        &installed, &error));
    static const char *const gamepad_runtime[] = {
        "img/gamepad/arm_L.png", "img/gamepad/arm_R.png",
        "img/gamepad/left_stick.png", "img/gamepad/left_stick_down.png",
        "img/gamepad/right_stick.png", "img/gamepad/right_stick_down.png",
        "img/gamepad/lefthand/leftup.png",
        "img/gamepad/righthand/rightup.png"
    };
    check_portable_images(portable_root, gamepad_runtime,
        sizeof(gamepad_runtime) / sizeof(gamepad_runtime[0]));
    /* Legacy backgrounds can disagree with the retained input canvas. */
    portable.mode = BONGO_CAT_MODE_STANDARD;
    portable.gamepad_buttons = false;
    CHECK(write_input_canvas(numeric, 500, 405));
    TauriMverCalibration calibration = {0};
    CHECK(child(path, sizeof(path), right_keys, "KeyA.png", false));
    CHECK(write_input_canvas(path, 500, 405));
    CHECK(child(path, sizeof(path), left_keys, "instructions.png", false));
    CHECK(write_input_canvas(path, 800, 600));
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.window_width == 500 && calibration.window_height == 405);
    CHECK(bongo_cat_tauri_key_code("F1.png") == 112 &&
        bongo_cat_tauri_key_code("F12.png") == 123 &&
        bongo_cat_tauri_key_code("F1x.png") == -1);
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.auto_frame && calibration.window_width == 500 &&
        calibration.window_height == 405 &&
        calibration.l2d_correct > 1.619 && calibration.l2d_correct < 1.621);
    /* Disagreeing image canvases do not supply a reliable replacement. */
    CHECK(child(path, sizeof(path), right_keys, "KeyA.png", false));
    CHECK(write_input_canvas(path, 600, 405));
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.window_width == expected_width &&
        calibration.window_height == expected_height);
    CHECK(write_input_canvas(path, 500, 405));
    /* A real source calibration must override the recovered projection. */
    CHECK(child(path, sizeof(path), model, BONGO_CAT_TAURI_SOURCE_FILE, false));
    CHECK(write_text(path, "{\"schemaVersion\":1,"
        "\"kind\":\"bongo-cat-mver-source\",\"mode\":\"standard\","
        "\"decoration\":{\"window_size\":[500,405],\"l2d_correct\":1.6,"
        "\"l2d_offset\":[0,-0.005]}}"));
    CHECK(child(portable_root, sizeof(portable_root), root,
        "portable-calibrated", false));
    CHECK(bongo_cat_import_tauri_convert_to_mver(&portable, portable_root,
        &installed, &error));
    CHECK(child(adapter_root, sizeof(adapter_root), root,
        "portable-calibrated-adapter", true));
    CHECK(bongo_cat_import_prepare_adapter(&installed, adapter_root, &error));
    CHECK(bongo_cat_import_render_options(adapter_root, &render) &&
        render.mver_projection && !render.auto_frame &&
        render.reference_width == 500 && render.reference_height == 405 &&
        render.projection_scale > 1.599f && render.projection_scale < 1.601f &&
        render.offset_y > -0.0051f && render.offset_y < -0.0049f);
    CHECK(child(path, sizeof(path), model, BONGO_CAT_TAURI_SOURCE_FILE, false));
    CHECK(write_text(path, "{\"schemaVersion\":0}"));
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.auto_frame);
    CHECK(write_text(path, "{\"schemaVersion\":1,"
        "\"kind\":\"bongo-cat-mver-source\",\"mode\":\"standard\","
        "\"decoration\":{\"l2d_correct\":1.6}}"));
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.auto_frame && calibration.window_width == 500 &&
        calibration.window_height == 405 && calibration.l2d_correct == 1.6);
    CHECK(write_text(path, "{\"schemaVersion\":1,"
        "\"kind\":\"bongo-cat-mver-source\",\"mode\":\"standard\","
        "\"decoration\":{\"window_size\":[500.5,405],\"l2d_correct\":-1}}"));
    CHECK(bongo_cat_tauri_read_calibration(&portable, &calibration) &&
        calibration.auto_frame && calibration.window_width == 500 &&
        calibration.l2d_correct > 1.619 && calibration.l2d_correct < 1.621);
    /* Resources can belong to a package root above the model directory. */
    CHECK(bongo_cat_path_remove(path));
    BongoCatImportCandidate package_resources = portable;
    CHECK(child(package_resources.directory, sizeof(package_resources.directory),
        root, "separate-model", true));
    snprintf(package_resources.assets, sizeof(package_resources.assets), "%s",
        package_resources.directory);
    snprintf(package_resources.package_root, sizeof(package_resources.package_root),
        "%s", model);
    CHECK(child(path, sizeof(path), root, "separate-inputs", true));
    TauriKeyFiles *copied_left = calloc(1, sizeof(*copied_left));
    TauriKeyFiles *copied_right = calloc(1, sizeof(*copied_right));
    CHECK(copied_left && copied_right);
    if (copied_left && copied_right) {
        CHECK(bongo_cat_tauri_copy_input_images(&package_resources, path,
            copied_left, copied_right, &error));
        CHECK(copied_left->count == 1 && copied_left->values[0].code == 24);
        CHECK(bongo_cat_tauri_read_calibration(&package_resources, &calibration) &&
            calibration.window_width == 500 && calibration.window_height == 405);
    }
    free(copied_left);
    free(copied_right);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}
