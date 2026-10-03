#include "mver_render.h"
#include "bongo_cat/json.h"
#include <math.h>
#include <limits.h>

static double number(yyjson_val *value, double fallback) {
    double result = yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
    return isfinite(result) ? result : fallback;
}
static int coordinate(yyjson_val *value, int fallback) {
    double result = number(value, fallback);
    return result >= INT_MIN && result <= INT_MAX ? (int)result : fallback;
}
void bongo_cat_mver_render_options(yyjson_val *config,
    BongoCatLive2DRenderOptions *options) {
    yyjson_val *decoration = yyjson_obj_get(config, "decoration");
    yyjson_val *workarea = yyjson_obj_get(config, "workarea");
    yyjson_val *window = yyjson_obj_get(decoration, "window_size");
    yyjson_val *offset = yyjson_obj_get(decoration, "l2d_offset");
    yyjson_val *top_left = yyjson_obj_get(workarea, "top_left");
    yyjson_val *bottom_right = yyjson_obj_get(workarea, "right_bottom");
    *options = (BongoCatLive2DRenderOptions){
        .mver_projection = true,
        .auto_frame = yyjson_get_bool(yyjson_obj_get(decoration, "l2d_auto_frame")),
        .source_mirror = yyjson_get_bool(yyjson_obj_get(decoration, "l2d_horizontal_flip")),
        .pointer_left_handed = yyjson_get_bool(yyjson_obj_get(decoration, "leftHanded")),
        .mouse_force_move = yyjson_get_bool(yyjson_obj_get(decoration, "mouse_force_move")),
        .mouse_speed = (float)number(yyjson_obj_get(decoration, "mouse_speed"), 1.0),
        .projection_scale = (float)number(yyjson_obj_get(decoration, "l2d_correct"), 1.1),
        .offset_x = (float)number(yyjson_arr_get(offset, 0), 0.0),
        .offset_y = (float)number(yyjson_arr_get(offset, 1), 0.0),
        .reference_width = coordinate(yyjson_arr_get(window, 0), 612),
        .reference_height = coordinate(yyjson_arr_get(window, 1), 352),
        .custom_pointer_bounds = yyjson_get_bool(yyjson_obj_get(workarea, "workarea")),
        .pointer_left = coordinate(yyjson_arr_get(top_left, 0), 0),
        .pointer_top = coordinate(yyjson_arr_get(top_left, 1), 0),
        .pointer_right = coordinate(yyjson_arr_get(bottom_right, 0), 0),
        .pointer_bottom = coordinate(yyjson_arr_get(bottom_right, 1), 0)};
    if (options->projection_scale <= 0.0f || options->projection_scale > 100.0f)
        options->projection_scale = 1.1f;
    if (options->reference_width <= 0 || options->reference_height <= 0) {
        options->reference_width = 612;
        options->reference_height = 352;
    }
    if (options->pointer_right <= options->pointer_left ||
        options->pointer_bottom <= options->pointer_top)
        options->custom_pointer_bounds = false;
}
bool bongo_cat_mver_render_read(const char *path,
    BongoCatLive2DRenderOptions *options) {
    yyjson_doc *doc = bongo_cat_json_read_file(path,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL);
    bool ok = doc && yyjson_is_obj(yyjson_doc_get_root(doc));
    if (ok) bongo_cat_mver_render_options(yyjson_doc_get_root(doc), options);
    yyjson_doc_free(doc);
    return ok;
}
