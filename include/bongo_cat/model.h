#ifndef BONGO_CAT_MODEL_H
#define BONGO_CAT_MODEL_H

#include "bongo_cat/config.h"

typedef enum BongoCatModelSourceFormat {
    BONGO_CAT_MODEL_SOURCE_UNKNOWN,
    BONGO_CAT_MODEL_SOURCE_BUILTIN,
    BONGO_CAT_MODEL_SOURCE_TAURI,
    BONGO_CAT_MODEL_SOURCE_MVER,
    BONGO_CAT_MODEL_SOURCE_MVER_PATCH
} BongoCatModelSourceFormat;

typedef enum BongoCatModelCapability {
    BONGO_CAT_MODEL_CAPABILITY_LIVE2D = 1u << 0,
    BONGO_CAT_MODEL_CAPABILITY_PREVIEW = 1u << 1,
    BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER = 1u << 2,
    BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES = 1u << 3,
    BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT = 1u << 4,
    BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT = 1u << 5,
    BONGO_CAT_MODEL_CAPABILITY_BEHAVIORS = 1u << 6,
    BONGO_CAT_MODEL_CAPABILITY_AUDIO = 1u << 7,
    BONGO_CAT_MODEL_CAPABILITY_EFFECTS = 1u << 8,
    BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION = 1u << 9,
    BONGO_CAT_MODEL_CAPABILITY_POINTER_OVERLAY = 1u << 10,
    BONGO_CAT_MODEL_CAPABILITY_IMAGE_PATCH = 1u << 11
} BongoCatModelCapability;

typedef struct BongoCatModelEntry {
    char id[BONGO_CAT_ID_CAP];
    char package_id[BONGO_CAT_ID_CAP];
    char content_digest[65];
    char family_id[BONGO_CAT_ID_CAP];
    char display_name[BONGO_CAT_ID_CAP];
    char directory[BONGO_CAT_PATH_CAP];
    char adapter_directory[BONGO_CAT_PATH_CAP];
    char storage_directory[BONGO_CAT_PATH_CAP];
    char setting_file[BONGO_CAT_PATH_CAP];
    BongoCatModelMode mode;
    BongoCatModelSourceFormat source_format;
    uint32_t capabilities;
    int adapter_schema;
    int adapter_generator;
    bool preset;
    bool managed;
} BongoCatModelEntry;

typedef struct BongoCatModelCatalog {
    BongoCatModelEntry entries[BONGO_CAT_MODEL_CAP];
    size_t count;
} BongoCatModelCatalog;

typedef enum BongoCatBehaviorKind {
    BONGO_CAT_BEHAVIOR_MOTION,
    BONGO_CAT_BEHAVIOR_EXPRESSION,
    BONGO_CAT_BEHAVIOR_SOUND,
    BONGO_CAT_BEHAVIOR_EFFECT
} BongoCatBehaviorKind;

typedef struct BongoCatBehaviorEntry {
    char id[BONGO_CAT_PATH_CAP];
    char label[BONGO_CAT_ID_CAP];
    char group[BONGO_CAT_ID_CAP];
    char sound[BONGO_CAT_PATH_CAP];
    char effect[BONGO_CAT_PATH_CAP];
    int index;
    BongoCatBehaviorKind kind;
    bool momentary;
    bool sound_overlap;
    bool sound_clear;
    bool shortcut_active, audio_playing; /* Runtime state owned by this entry. */
} BongoCatBehaviorEntry;

typedef struct BongoCatBehaviorCatalog {
    BongoCatBehaviorEntry *entries;
    size_t count, capacity;
} BongoCatBehaviorCatalog;

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_models_init(BongoCatModelCatalog *catalog);
BongoCatResult bongo_cat_models_scan(BongoCatModelCatalog *catalog, const char *root,
    bool preset, BongoCatError *error);
const BongoCatModelEntry *bongo_cat_models_find(const BongoCatModelCatalog *catalog,
    const char *id);
bool bongo_cat_model_adapter_metadata_path(const char *directory,
    char *path, size_t capacity);
const char *bongo_cat_model_default_name(const BongoCatModelEntry *entry);
const char *bongo_cat_model_name(const BongoCatSettings *settings,
    const BongoCatModelEntry *entry);
/* Zero-initialize catalogs before first use. Copy/move preserve ownership. */
bool bongo_cat_behaviors_reserve(BongoCatBehaviorCatalog *catalog, size_t capacity,
    BongoCatError *error);
void bongo_cat_behaviors_clear(BongoCatBehaviorCatalog *catalog);
bool bongo_cat_behaviors_copy(BongoCatBehaviorCatalog *target,
    const BongoCatBehaviorCatalog *source, BongoCatError *error);
void bongo_cat_behaviors_move(BongoCatBehaviorCatalog *target, BongoCatBehaviorCatalog *source);
BongoCatResult bongo_cat_behaviors_load(BongoCatBehaviorCatalog *catalog,
    const BongoCatModelEntry *model, BongoCatError *error);

typedef struct BongoCatLive2D BongoCatLive2D;
typedef struct BongoCatParameterRange { float minimum, maximum, value; } BongoCatParameterRange;
typedef void (*BongoCatLive2DLoadProgress)(void *userdata, float progress);
typedef struct BongoCatLive2DVisualState {
    float fit_scale, fit_translate_x, fit_translate_y;
    float visible_min_x, visible_min_y, visible_max_x, visible_max_y;
    int drawable_count, drawable_visible, drawable_vertex_changed;
    int offscreen_count, offscreen_positive, part_count, part_positive;
    bool fitted, visible, mver_projection;
} BongoCatLive2DVisualState;

/* Extra transparent space around the authored model canvas, expressed as a
   fraction of that canvas dimension.  For example, top=0.5 reserves half a
   canvas height above the normal composition. */
typedef struct BongoCatLive2DFrame {
    float left, top, right, bottom;
} BongoCatLive2DFrame;

typedef struct BongoCatLive2DRenderOptions {
    bool mver_projection;
    bool auto_frame;
    bool source_mirror;
    bool custom_pointer_bounds;
    bool pointer_left_handed;
    bool mouse_force_move;
    float mouse_speed;
    float projection_scale;
    float offset_x;
    float offset_y;
    int reference_width;
    int reference_height;
    int pointer_left;
    int pointer_top;
    int pointer_right;
    int pointer_bottom;
} BongoCatLive2DRenderOptions;

/* Runtime texture policy.  This is deliberately separate from model package
   rendering metadata: it is a user preference and can be changed without
   modifying an imported model. */
typedef bool (*BongoCatLive2DTextureDisplaySize)(void *userdata,
    const BongoCatLive2DRenderOptions *options, int canvas_width,
    int canvas_height, int *display_width, int *display_height);
typedef struct BongoCatLive2DTextureOptions {
    bool dynamic_resolution;
    /* Approximate texture-memory budget: 0.1, 1, then 10 to 100 percent. */
    float render_quality_percent;
    /* Synchronous planner, called after reading the incoming canvas and before
       allocating its atlases. Returns content pixels, excluding transparent
       frame padding. It must not change the live model or window. */
    BongoCatLive2DTextureDisplaySize display_size;
    void *display_size_userdata;
} BongoCatLive2DTextureOptions;

BongoCatLive2D *bongo_cat_live2d_create(const char *asset_root, BongoCatError *error);
void bongo_cat_live2d_destroy(BongoCatLive2D *live2d);
BongoCatResult bongo_cat_live2d_load(BongoCatLive2D *live2d, const char *model_dir,
    const char *setting_file, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error);
BongoCatResult bongo_cat_live2d_load_ex(BongoCatLive2D *live2d,
    const char *model_dir, const char *setting_file, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    const BongoCatLive2DTextureOptions *texture_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error);
bool bongo_cat_live2d_ready(const BongoCatLive2D *live2d);
/* Returns the authored pixel canvas size of the loaded model. */
bool bongo_cat_live2d_canvas_size(const BongoCatLive2D *live2d,
    int *width, int *height);
bool bongo_cat_live2d_frame(const BongoCatLive2D *live2d,
    BongoCatLive2DFrame *frame);
/* Inspect current CPU geometry before drawing. Updates the retained envelope
   and fits it inside the allocated frame until the window can grow. No GL
   readback, animation advancement, or native window operations. */
bool bongo_cat_live2d_measure_frame(BongoCatLive2D *live2d,
    BongoCatLive2DFrame *required);
/* Commit only the transparent margins that the native window can allocate. */
void bongo_cat_live2d_set_frame(BongoCatLive2D *live2d,
    const BongoCatLive2DFrame *frame);
bool bongo_cat_live2d_viewport(const BongoCatLive2D *live2d,
    int *x, int *y, int *width, int *height);
void bongo_cat_live2d_resize(BongoCatLive2D *live2d, int width, int height);
void bongo_cat_live2d_reshape(BongoCatLive2D *live2d, int width, int height);
/* Main-thread fast path with no GL calls. Commit the new quality only when
   every current atlas already has the required pixels. False leaves the
   model unchanged; the caller can use the normal reload/rollback path. */
bool bongo_cat_live2d_try_reuse_texture_quality(BongoCatLive2D *live2d,
    float quality_percent);
bool bongo_cat_live2d_texture_refresh_pending(const BongoCatLive2D *live2d, bool active);
/* Main-thread query without GL calls. Unlike pending, excludes debounce,
   cancellation cooldown and paused jobs that do not yet need retirement. */
bool bongo_cat_live2d_texture_refresh_due(const BongoCatLive2D *live2d,
    bool active, bool allow_start);
/* True only while upload/cleanup can make progress, so low playback FPS does
   not delay reclamation. Paused jobs do not request frequent wakeups. */
bool bongo_cat_live2d_texture_refresh_busy(const BongoCatLive2D *live2d);
/* Request cancellation without joining the worker. Continue polling cleanup.
   The displayed atlas remains valid; pending resolution work resumes on show. */
void bongo_cat_live2d_cancel_texture_refresh(BongoCatLive2D *live2d);
/* Call with the model's GL context current. False active pauses uploads during
   gestures/temporary hiding; obsolete work is still cleaned up. True permits
   one upload batch. Long pauses release the unfinished replacement.
   False allow_start services existing work and settled reclamation only. */
bool bongo_cat_live2d_refresh_textures(BongoCatLive2D *live2d,
    bool active, bool allow_start);
bool bongo_cat_live2d_update(BongoCatLive2D *live2d, float delta_seconds);
void bongo_cat_live2d_draw(BongoCatLive2D *live2d);
void bongo_cat_live2d_set_mirror(BongoCatLive2D *live2d, bool mirror);
void bongo_cat_live2d_set_vertical_flip(BongoCatLive2D *live2d, bool flipped);
void bongo_cat_live2d_set_render_options(BongoCatLive2D *live2d,
    const BongoCatLive2DRenderOptions *options);
void bongo_cat_live2d_set_dragging(BongoCatLive2D *live2d, float x, float y);
void bongo_cat_live2d_set_centered_dragging(BongoCatLive2D *live2d,
    float x, float y);
void bongo_cat_live2d_prepare_viewer_audit(BongoCatLive2D *live2d);
/* Synchronize drawables from the current state before a cover-only frame. */
bool bongo_cat_live2d_prepare_cover_capture(BongoCatLive2D *live2d);
bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value);
bool bongo_cat_live2d_parameter(BongoCatLive2D *live2d, const char *id,
    BongoCatParameterRange *range);
bool bongo_cat_live2d_start_motion(BongoCatLive2D *live2d, const char *group, int index);
bool bongo_cat_live2d_restore_motion_state(BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_preview_motion(BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_restore_motion_preview(BongoCatLive2D *live2d);
bool bongo_cat_live2d_commit_motion_preview(BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_motion_selected(const BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_motion_persistent(const BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_motion_visible(const BongoCatLive2D *live2d,
    const char *group, int index);
bool bongo_cat_live2d_motion_same_toggle(const BongoCatLive2D *live2d,
    const char *left_group, int left_index,
    const char *right_group, int right_index);
bool bongo_cat_live2d_set_expression(BongoCatLive2D *live2d, int index);
int bongo_cat_live2d_expression(const BongoCatLive2D *live2d);
bool bongo_cat_live2d_visual_state(const BongoCatLive2D *live2d,
    BongoCatLive2DVisualState *state);

#ifdef __cplusplus
}
#endif

#endif
