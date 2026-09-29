#include "bongo_cat/model.h"
#include "model_import_manifest.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_log.h>
#include <stdlib.h>

struct BongoCatLive2D {
    int width;
    int height;
    bool loaded;
};

static BongoCatLive2D *create_runtime(const char *asset_root,
    BongoCatError *error) {
    static bool warning_logged;
    (void)asset_root;
    if (!warning_logged) {
        warning_logged = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cubism SDK unavailable: diagnostic backend active; Live2D "
            "rendering, animation, pointer tracking, and cover generation "
            "are disabled");
    }
    BongoCatLive2D *value = calloc(1, sizeof(*value));
    if (!value) bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate Live2D runtime");
    return value;
}

BongoCatLive2D *bongo_cat_live2d_create(const char *asset_root,
    BongoCatError *error) {
    return create_runtime(asset_root, error);
}

void bongo_cat_live2d_destroy(BongoCatLive2D *live2d) { free(live2d); }

BongoCatResult bongo_cat_live2d_load_ex(BongoCatLive2D *live2d,
    const char *model_dir,
    const char *setting_file, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    const BongoCatLive2DTextureOptions *texture_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error) {
    (void)preset; (void)render_options; (void)texture_options;
    if (!live2d || !model_dir || !setting_file) return BONGO_CAT_ERROR_ARGUMENT;
    if (progress) progress(userdata, 0.1f);
    char path[BONGO_CAT_PATH_CAP];
    yyjson_doc *document = bongo_cat_path_join(path, sizeof(path), model_dir,
        setting_file) ? bongo_cat_model_json_read(path, NULL) : NULL;
    bool valid = bongo_cat_import_manifest_document_valid(model_dir, document, true);
    yyjson_doc_free(document);
    if (!valid) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model manifest or required assets are invalid: %s", setting_file);
        return BONGO_CAT_ERROR_FORMAT;
    }
    live2d->loaded = true;
    if (progress) progress(userdata, 1.0f);
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_live2d_load(BongoCatLive2D *live2d, const char *model_dir,
    const char *setting_file, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error) {
    return bongo_cat_live2d_load_ex(live2d, model_dir, setting_file, preset,
        render_options, NULL, progress, userdata, error);
}

bool bongo_cat_live2d_ready(const BongoCatLive2D *live2d) {
    return live2d && live2d->loaded;
}

bool bongo_cat_live2d_canvas_size(const BongoCatLive2D *live2d,
    int *width, int *height) {
    (void)live2d; (void)width; (void)height; return false;
}

bool bongo_cat_live2d_frame(const BongoCatLive2D *live2d,
    BongoCatLive2DFrame *frame) {
    if (!live2d || !frame) return false;
    *frame = (BongoCatLive2DFrame){0};
    return true;
}

bool bongo_cat_live2d_viewport(const BongoCatLive2D *live2d,
    int *x, int *y, int *width, int *height) {
    if (!live2d || !x || !y || !width || !height) return false;
    *x = 0;
    *y = 0;
    *width = live2d->width;
    *height = live2d->height;
    return true;
}

void bongo_cat_live2d_resize(BongoCatLive2D *live2d, int width, int height) {
    if (!live2d) return;
    live2d->width = width;
    live2d->height = height;
}
void bongo_cat_live2d_reshape(BongoCatLive2D *live2d, int width, int height) {
    bongo_cat_live2d_resize(live2d, width, height);
}

bool bongo_cat_live2d_update(BongoCatLive2D *live2d, float delta_seconds) {
    (void)live2d; (void)delta_seconds; return false;
}
bool bongo_cat_live2d_texture_refresh_pending(const BongoCatLive2D *live2d, bool active) {
    (void)live2d; (void)active; return false;
}
bool bongo_cat_live2d_texture_refresh_due(const BongoCatLive2D *live2d,
    bool active, bool allow_start) {
    (void)live2d; (void)active; (void)allow_start; return false;
}
bool bongo_cat_live2d_try_reuse_texture_quality(BongoCatLive2D *live2d,
    float quality_percent) {
    (void)live2d; (void)quality_percent; return false;
}

bool bongo_cat_live2d_measure_frame(BongoCatLive2D *live2d,
    BongoCatLive2DFrame *required) {
    (void)live2d; (void)required;
    return false;
}

void bongo_cat_live2d_set_frame(BongoCatLive2D *live2d,
    const BongoCatLive2DFrame *frame) {
    (void)live2d; (void)frame;
}
bool bongo_cat_live2d_refresh_textures(BongoCatLive2D *live2d,
    bool active, bool allow_start) {
    (void)live2d; (void)active; (void)allow_start; return false;
}
bool bongo_cat_live2d_texture_refresh_busy(const BongoCatLive2D *live2d) {
    (void)live2d; return false;
}
void bongo_cat_live2d_cancel_texture_refresh(BongoCatLive2D *live2d) {
    (void)live2d;
}
void bongo_cat_live2d_draw(BongoCatLive2D *live2d) { (void)live2d; }
void bongo_cat_live2d_set_vertical_flip(BongoCatLive2D *live2d, bool flipped) {
    (void)live2d; (void)flipped;
}

void bongo_cat_live2d_set_mirror(BongoCatLive2D *live2d, bool mirror) {
    (void)live2d; (void)mirror;
}
void bongo_cat_live2d_set_render_options(BongoCatLive2D *live2d,
    const BongoCatLive2DRenderOptions *options) {
    (void)live2d; (void)options;
}
void bongo_cat_live2d_set_dragging(BongoCatLive2D *live2d, float x, float y) {
    (void)live2d; (void)x; (void)y;
}
void bongo_cat_live2d_set_centered_dragging(BongoCatLive2D *live2d,
    float x, float y) { (void)live2d; (void)x; (void)y; }
void bongo_cat_live2d_prepare_viewer_audit(BongoCatLive2D *live2d) { (void)live2d; }
bool bongo_cat_live2d_prepare_cover_capture(BongoCatLive2D *live2d) {
    return live2d && live2d->loaded;
}
bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value) {
    (void)live2d; (void)id; (void)value; return false;
}
bool bongo_cat_live2d_parameter(BongoCatLive2D *value, const char *id, BongoCatParameterRange *range) {
    (void)value; (void)id; (void)range; return false;
}
bool bongo_cat_live2d_start_motion(BongoCatLive2D *value, const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_restore_motion_state(BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_preview_motion(BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_restore_motion_preview(BongoCatLive2D *value) {
    (void)value; return false;
}
bool bongo_cat_live2d_commit_motion_preview(BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_motion_selected(const BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_motion_persistent(const BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_live2d_motion_visible(const BongoCatLive2D *value,
    const char *group, int index) {
    (void)value; (void)group; (void)index; return true;
}
bool bongo_cat_live2d_motion_same_toggle(const BongoCatLive2D *value,
    const char *left_group, int left_index,
    const char *right_group, int right_index) {
    (void)value; (void)left_group; (void)left_index;
    (void)right_group; (void)right_index; return false;
}
bool bongo_cat_live2d_set_expression(BongoCatLive2D *value, int index) {
    (void)value; (void)index; return false;
}
int bongo_cat_live2d_expression(const BongoCatLive2D *value) {
    (void)value; return -1;
}
bool bongo_cat_live2d_visual_state(const BongoCatLive2D *value,
    BongoCatLive2DVisualState *state) {
    (void)value; (void)state; return false;
}
