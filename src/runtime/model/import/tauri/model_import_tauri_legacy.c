#include "model_import_tauri_internal.h"
#include "bongo_cat/image.h"

static bool valid_canvas_size(int width, int height) {
    return width >= 64 && width <= 8192 && height >= 64 && height <= 8192;
}

static bool legacy_converter_double_scale(
    const BongoCatImportCandidate *candidate, int width, int height) {
    if (!candidate || !valid_canvas_size(width, height) ||
        width < 256 || height < 256) return false;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_tauri_find_resource_directory(candidate, "left-keys",
            path, sizeof(path))) return false;
    if (candidate->mode != BONGO_CAT_MODE_STANDARD) return true;
    return width == height && width >= 512;
}

typedef struct InputCanvas {
    int width, height;
    bool mismatch;
} InputCanvas;

static BongoCatPathVisit inspect_input_canvas(void *userdata,
    const char *directory, const char *name) {
    InputCanvas *canvas = userdata;
    char path[BONGO_CAT_PATH_CAP];
    int width = 0, height = 0;
    if (bongo_cat_tauri_key_code(name) < 0 ||
        !bongo_cat_path_join(path, sizeof(path), directory, name) ||
        !bongo_cat_image_info(path, &width, &height) ||
        !valid_canvas_size(width, height)) return BONGO_CAT_PATH_CONTINUE;
    if (canvas->width && (canvas->width != width || canvas->height != height))
        canvas->mismatch = true;
    canvas->width = width;
    canvas->height = height;
    return BONGO_CAT_PATH_CONTINUE;
}

static void legacy_input_canvas(const BongoCatImportCandidate *candidate,
    TauriMverCalibration *calibration) {
    InputCanvas canvas = {0};
    const char *groups[] = {"left-keys", "right-keys"};
    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_tauri_find_resource_directory(candidate, groups[i],
                path, sizeof(path)) &&
            !bongo_cat_path_enumerate(path, inspect_input_canvas, &canvas))
            return;
    }
    if (!canvas.width || canvas.mismatch ||
        (canvas.width == calibration->window_width &&
         canvas.height == calibration->window_height)) return;
    /* Legacy exporters can leave a square placeholder background while keeping
       the original input canvas. Preserve vertical projection when recovering
       its aspect ratio; a cropped cover is not calibration metadata. */
    double vertical_scale = calibration->l2d_correct *
        calibration->window_width / calibration->window_height;
    double scale = vertical_scale * canvas.height / canvas.width;
    if (scale <= 0.01 || scale > 100.0) return;
    calibration->window_width = canvas.width;
    calibration->window_height = canvas.height;
    calibration->l2d_correct = scale;
}

void bongo_cat_tauri_legacy_calibration(const BongoCatImportCandidate *candidate,
    TauriMverCalibration *calibration) {
    bongo_cat_tauri_calibration_defaults(calibration);
    /* Legacy packages lost the authored projection. Keep aligned layers, but
       allow visible model geometry to extend beyond the inferred canvas. */
    calibration->auto_frame = true;
    char path[BONGO_CAT_PATH_CAP];
    const char *names[] = {"background.png", "cover.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!bongo_cat_tauri_find_resource_file(candidate, names[i], path,
                sizeof(path))) continue;
        int width = 0, height = 0;
        if (bongo_cat_image_info(path, &width, &height) &&
            valid_canvas_size(width, height)) {
            calibration->window_width = width;
            calibration->window_height = height;
            break;
        }
    }
    /* Converter packages before schemaVersion 1 omitted their Mver projection
       calibration. Keyboard/gamepad packages and the converter's square
       standard profile use approximately 2.0. Non-square standard packages
       retain Mver's 1.1 default; cover.png is only a cropped preview and must
       not be used to infer runtime projection. */
    if (legacy_converter_double_scale(candidate, calibration->window_width,
            calibration->window_height)) {
        calibration->l2d_correct = 2.0;
        if (candidate->mode == BONGO_CAT_MODE_STANDARD &&
            calibration->window_width == calibration->window_height)
            calibration->l2d_offset_y = -0.005;
        legacy_input_canvas(candidate, calibration);
    }
}

