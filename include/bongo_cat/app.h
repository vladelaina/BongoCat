#ifndef BONGO_CAT_APP_H
#define BONGO_CAT_APP_H

#include "bongo_cat/config.h"
#include "bongo_cat/input.h"
#include "bongo_cat/model.h"
#include "bongo_cat/mver_pointer.h"
#include "bongo_cat/mouse.h"
#include "bongo_cat/platform.h"
#include "bongo_cat/shortcut.h"
#include "bongo_cat/sound_shortcut.h"

typedef struct BongoCatAudio BongoCatAudio;
typedef struct BongoCatTray BongoCatTray;
typedef struct BongoCatOverlay BongoCatOverlay;
typedef struct BongoCatPreferences BongoCatPreferences;
typedef struct BongoCatI18n BongoCatI18n;
typedef struct BongoCatMultiPetRuntime BongoCatMultiPetRuntime;
typedef struct BongoCatModelRefresh BongoCatModelRefresh;
typedef struct BongoCatUpdateService BongoCatUpdateService;

#define BONGO_CAT_MODEL_COVER_PENDING_CAP 8

/* File-backed bindings are runtime data, independent of user override limits.
   Nodes keep editor pointers stable while a sibling model is refreshed. */
typedef struct BongoCatModelShortcutNode {
    BongoCatBehaviorShortcut binding;
    struct BongoCatModelShortcutNode *next;
} BongoCatModelShortcutNode;
typedef struct BongoCatModelShortcutCache {
    char model_id[BONGO_CAT_ID_CAP];
    BongoCatModelShortcutNode *bindings;
    struct BongoCatModelShortcutCache *next;
} BongoCatModelShortcutCache;

typedef struct BongoCatApp {
    BongoCatSettings settings;
    BongoCatModelShortcutCache *model_shortcuts;
    BongoCatSessionState session;
    BongoCatInputState input;
    /* Main-thread-only cumulative input diagnostics. */
    struct {
        uint64_t log_ms, drained_keys, ignored_keys, mapped_keys;
        uint64_t unmapped_keys, no_model_keys, presented_frames;
        uint64_t received_keys, mouse_buttons, gamepad_buttons, gamepad_axes;
        uint64_t stick_deadzone_events;
        uint64_t mode_blocked_keys, ignored_gamepad, keyboard_overlays, gamepad_overlays;
        uint64_t replays, replay_blocked_keys, visual_actions;
        unsigned hands_seen; /* Left/right bits observed since the last report. */
        bool pending;
        char last_gamepad[BONGO_CAT_ID_CAP];
        float last_gamepad_value;
        char last_visual_action[BONGO_CAT_BEHAVIOR_ID_CAP];
    } input_diagnostics;
    BongoCatShortcutState shortcut_state;
    BongoCatSoundShortcutState sound_shortcut_state;
    BongoCatModelCatalog models;
    BongoCatBehaviorCatalog behaviors;
    /* One immutable installed package's behavior catalog can be reused when
       the user toggles back to it. Nearby source models are excluded because
       their files may change outside the application. Allocate it lazily so
       single-model child processes do not reserve the catalog. */
    BongoCatBehaviorCatalog *behavior_cache;
    char behavior_cache_model_id[BONGO_CAT_ID_CAP];
    char behavior_cache_digest[65];
    bool behavior_cache_valid;
    bool behavior_catalog_valid;
    BongoCatLive2DRenderOptions model_render_options;
    BongoCatI18n *i18n;
    BongoCatPlatform platform;
    BongoCatLive2D *live2d;
    BongoCatAudio *audio;
    BongoCatTray *tray;
    BongoCatOverlay *overlay;
    BongoCatPreferences *preferences;
    BongoCatMultiPetRuntime *multi_pet;
    BongoCatModelRefresh *model_refresh;
    BongoCatUpdateService *update;
    SDL_Window *window;
    void *gl_context;
    char settings_path[BONGO_CAT_PATH_CAP];
    char session_path[BONGO_CAT_PATH_CAP];
    char config_root[BONGO_CAT_PATH_CAP];
    char data_root[BONGO_CAT_PATH_CAP];
    char models_root[BONGO_CAT_PATH_CAP];
    char cache_root[BONGO_CAT_PATH_CAP];
    char state_root[BONGO_CAT_PATH_CAP];
    char log_root[BONGO_CAT_PATH_CAP];
    char storage_root[BONGO_CAT_PATH_CAP];
    char asset_root[BONGO_CAT_PATH_CAP];
    char locale_root[BONGO_CAT_PATH_CAP];
    char nearby_root[BONGO_CAT_PATH_CAP];
    char executable_path[BONGO_CAT_PATH_CAP];
    char primary_state_root[BONGO_CAT_PATH_CAP];
    char secondary_model_id[BONGO_CAT_ID_CAP];
    char smoke_import_path[BONGO_CAT_PATH_CAP];
    char smoke_model[BONGO_CAT_ID_CAP];
    char smoke_runtime_model[BONGO_CAT_ID_CAP];
    char smoke_live2d_scenario[BONGO_CAT_ID_CAP];
    char smoke_viewer_trace[BONGO_CAT_PATH_CAP];
    char pending_model_cover_ids[BONGO_CAT_MODEL_COVER_PENDING_CAP][BONGO_CAT_ID_CAP];
    char pending_model_cover_paths[BONGO_CAT_MODEL_COVER_PENDING_CAP][BONGO_CAT_PATH_CAP];
    uint64_t pending_model_cover_retry_ns[BONGO_CAT_MODEL_COVER_PENDING_CAP];
    unsigned pending_model_cover_attempts[BONGO_CAT_MODEL_COVER_PENDING_CAP];
    size_t pending_model_cover_count;
    char loaded_model[BONGO_CAT_ID_CAP];
    char loading_model[BONGO_CAT_ID_CAP];
    BongoCatModelMode loaded_mode;
    bool loaded_gamepad_keyboard; /* Mver gamepad.input_mode == 0. */
    bool running;
    bool settings_store_valid;
    bool session_store_valid;
    bool settings_store_blocked;
    bool session_store_blocked;
    bool autostart_launch;
    bool secondary_pet;
    bool secondary_origin_known;
    bool secondary_control_known;
    bool secondary_control_visible;
    bool secondary_control_pass_through;
    /* Keep the initial transparent window hidden until its first complete
       model frame is ready. This prevents a platform-dependent black flash. */
    bool startup_visibility_pending;
    int secondary_origin_x, secondary_origin_y;
    uint64_t secondary_control_check_ns;
    uint64_t secondary_control_failure_ns;
    uint64_t secondary_settings_check_ns;
    uint64_t startup_raise_due_ns;
    uint64_t model_load_last_frame_ns;
    bool smoke;
    bool smoke_preferences;
    bool smoke_preference_shortcut;
    bool smoke_preference_model_select;
    bool smoke_remove_imported;
    bool smoke_shortcuts;
    bool smoke_menu;
    bool smoke_ignore_global_input;
    bool smoke_pass_through;
    bool smoke_context_menu;
    bool smoke_frame_audited;
    bool smoke_frame_series;
    bool smoke_runtime_flow;
    bool smoke_freeze_model;
    unsigned smoke_runtime_stage;
    unsigned model_load_runtime_stage;
    unsigned model_selection_serial;
    uint64_t smoke_runtime_flow_ns;
    int smoke_language;
    int smoke_theme;
    int smoke_preference_page;
    int exit_code;
    bool dirty;
    uint64_t last_frame_ns;
    int startup_display_fps;
    uint64_t render_retry_ns;
    uint64_t frame_geometry_retry_ns;
    uint64_t smoke_deadline_ns;
    uint64_t hover_deadline_ns;
    uint64_t pointer_hit_deadline_ns;
    uint64_t display_recovery_due_ns;
    uint64_t mouse_last_ns;
    uint64_t frame_audit_bmp_ns;
    uint64_t random_expression_due_ns;
    float random_expression_interval_seconds;
    uint64_t random_motion_due_ns;
    float random_motion_interval_seconds;
    uint32_t random_behavior_state;
    uint64_t settings_saved_hash, settings_observed_hash;
    uint64_t session_saved_hash, session_observed_hash;
    uint64_t settings_save_due_ns, session_save_due_ns;
    BongoCatMouseTracking mouse_tracking;
    BongoCatMverPointerState mver_pointer;
    bool hover_inside;
    bool hover_hidden;
    bool hover_fade_active;
    float hover_fade_phase, hover_fade_target, hover_fade_opacity;
    uint64_t hover_fade_tick_ns, hover_fade_next_ns;
    bool pointer_known;
    bool pointer_hit_dirty;
    bool pointer_transparent;
    bool click_through_valid;
    bool click_through_applied;
    bool click_through_forced_applied;
    bool left_mouse_down;
    bool right_mouse_down;
    bool side_mouse_down;
    bool back_mouse_down, forward_mouse_down;
    bool mouse_button_event_pending;
    bool model_pointer_anchor_ready;
    float model_pointer_anchor_x, model_pointer_anchor_y;
    bool pointer_relative_active;
    bool pointer_cursor_locked;
    bool window_minimized;
    bool context_menu_requested;
    bool context_menu_active;
    bool context_menu_close_requested;
    double pointer_x, pointer_y;
    bool resize_gesture;
    bool resize_candidate;
    bool resize_menu_pending;
    bool resize_target_pending;
    uint64_t resize_next_ns;
    float resize_pointer_delta;
    float resize_scale_start, resize_scale_target;
    int resize_base_width, resize_base_height;
    bool resize_pending;
    bool resize_render_target_pending;
    /* Native preview ownership and gesture lifetime belong to window_snapshot.c. */
    void *window_snapshot;
    bool snapshot_blocked;
    uint64_t snapshot_deadline_ns;
    float snapshot_pointer_x, snapshot_pointer_y;
    bool wheel_animation_active;
    bool wheel_gesture_active;
    int resize_pixel_width, resize_pixel_height;
    uint64_t wheel_animation_ns, wheel_input_ns, wheel_event_ns;
    float wheel_opacity_target, wheel_scale_target;
    float wheel_center_x, wheel_center_y;
    float wheel_geometry_scale;
    int wheel_base_width, wheel_base_height;
    bool drag_candidate;
    bool window_drag_active;
    float drag_start_x, drag_start_y;
    int drag_window_x, drag_window_y;
    void *drag_display_bounds;
    int drag_display_count;
    float left_stick_x, left_stick_y;
    float right_stick_x, right_stick_y;
    bool left_stick_pressed, right_stick_pressed;
    BongoCatInputEvent active_inputs[
        BONGO_CAT_INPUT_KEY_STATE_CAP + BONGO_CAT_INPUT_RECOVERY_CAP];
    size_t active_input_count;
    uint32_t active_gamepad;
} BongoCatApp;

int bongo_cat_app_run(int argc, char **argv);
void bongo_cat_app_apply_input(BongoCatApp *app, const BongoCatInputEvent *event);
void bongo_cat_app_reapply_input(BongoCatApp *app);
/* Refresh visual hand policy without replaying held inputs or shortcuts. */
void bongo_cat_app_refresh_hands(BongoCatApp *app);
void bongo_cat_app_reset_gamepad(BongoCatApp *app);
void bongo_cat_gamepad_event(BongoCatApp *app, const void *sdl_event);
void bongo_cat_app_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event);
bool bongo_cat_app_select_model(BongoCatApp *app, const char *id);
bool bongo_cat_app_select_model_with_error(BongoCatApp *app,
    const char *id, BongoCatError *error);
bool bongo_cat_app_reload_model_with_error(BongoCatApp *app,
    BongoCatError *error);
bool bongo_cat_app_model_active(const BongoCatApp *app, const char *id);
size_t bongo_cat_app_active_model_count(const BongoCatApp *app);
bool bongo_cat_app_set_model_active(BongoCatApp *app, const char *id,
    bool active, BongoCatError *error);
void bongo_cat_app_set_multiple_pets(BongoCatApp *app, bool enabled);
bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior);
size_t bongo_cat_app_selected_motion_count(const BongoCatApp *app);
void bongo_cat_app_capture_behavior_state(BongoCatApp *app);
size_t bongo_cat_app_saved_behavior_count(const BongoCatApp *app,
    const char *model_id);
void bongo_cat_app_forget_behavior_state(BongoCatApp *app,
    const char *model_id);
void bongo_cat_app_restore_behavior_state(BongoCatApp *app,
    const char *model_id);
BongoCatResult bongo_cat_app_import_model(BongoCatApp *app, const char *source, BongoCatError *error);
BongoCatResult bongo_cat_app_remove_model(BongoCatApp *app, const char *id, BongoCatError *error);
void bongo_cat_app_rescan_models(BongoCatApp *app);
void bongo_cat_app_refresh_installed_models(BongoCatApp *app);
void bongo_cat_app_refresh_nearby_models(BongoCatApp *app);
void bongo_cat_app_request_model_refresh(BongoCatApp *app);
bool bongo_cat_app_model_refresh_busy(const BongoCatApp *app);
void bongo_cat_app_request_model_package_refresh(BongoCatApp *app,
    const char *package_id);
void bongo_cat_app_request_nearby_model_refresh(BongoCatApp *app);
void bongo_cat_config_store_flush(BongoCatApp *app);

const BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding(
    const BongoCatApp *app, const char *id);
BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding_mut(
    BongoCatApp *app, const char *id);
BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding_target(
    BongoCatApp *app, const char *target);
const char *bongo_cat_app_behavior_label(const BongoCatApp *app, const char *id);
void bongo_cat_app_reset_sound_bindings(BongoCatApp *app);
void bongo_cat_app_model_shortcuts_clear(BongoCatApp *app);
void bongo_cat_app_model_shortcuts_prune(BongoCatApp *app);
bool bongo_cat_app_shortcut_conflicts(BongoCatApp *app,
    const char *shortcut, const char *exclude);
#endif
