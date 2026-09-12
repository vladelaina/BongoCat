#ifndef BONGO_CAT_RUNTIME_INTERNAL_H
#define BONGO_CAT_RUNTIME_INTERNAL_H

#include "bongo_cat/app.h"
#include "bongo_cat/memory_policy.h"
#include "update_service.h"
#include <SDL3/SDL.h>

BongoCatResult bongo_cat_window_create(BongoCatApp *app, BongoCatError *error);
bool bongo_cat_app_initialize(BongoCatApp *app, int argc,
    char **argv, BongoCatError *error);
void bongo_cat_app_loop(BongoCatApp *app);
BongoCatResult bongo_cat_model_catalog_scan(BongoCatApp *app, bool cleanup,
    const char *nearby_root);
void bongo_cat_model_catalog_finish(BongoCatApp *app);
bool bongo_cat_model_catalog_add_bundled(BongoCatApp *app, bool replace);
void bongo_cat_model_catalog_finish_package(BongoCatApp *app,
    const char *package_id);
void bongo_cat_model_refresh_invalidate(BongoCatApp *app);
bool bongo_cat_model_refresh_event(BongoCatApp *app,
    const SDL_Event *event);
void bongo_cat_model_refresh_update(BongoCatApp *app);
void bongo_cat_model_refresh_shutdown(BongoCatApp *app);
BongoCatResult bongo_cat_app_locate_assets(BongoCatApp *app, BongoCatError *error);
bool bongo_cat_startup_prepare(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error);
bool bongo_cat_startup_arguments(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error);
void bongo_cat_startup_stage(BongoCatApp *app, const char *stage);
void bongo_cat_startup_ready(BongoCatApp *app);
void bongo_cat_runtime_stage(BongoCatApp *app, const char *stage);
void bongo_cat_runtime_log_stop(void);
void bongo_cat_runtime_clean_shutdown(BongoCatApp *app, int exit_code);
void bongo_cat_app_shutdown(BongoCatApp *app, const char *stage,
    int exit_code);
void bongo_cat_startup_failure(BongoCatApp *app, const BongoCatError *error);
void bongo_cat_startup_ci_failure(BongoCatApp *app, const BongoCatError *error);
void bongo_cat_window_destroy(BongoCatApp *app);
void bongo_cat_window_apply(BongoCatApp *app);
bool bongo_cat_window_event(BongoCatApp *app, const SDL_Event *event);
bool bongo_cat_window_visible_at_pointer(BongoCatApp *app, float x, float y);
void bongo_cat_window_capture_pointer_hit(BongoCatApp *app);
void bongo_cat_window_mark_hit_dirty(BongoCatApp *app);
void bongo_cat_window_set_visible(BongoCatApp *app, bool visible);
void bongo_cat_window_raise_when_due(BongoCatApp *app, uint64_t now);
void bongo_cat_window_schedule_pointer_hit(BongoCatApp *app);
void bongo_cat_window_schedule_hit_check(BongoCatApp *app);
int bongo_cat_window_wait_timeout(const BongoCatApp *app, uint64_t now);
bool bongo_cat_window_wait_timeout_self_test(void);
bool bongo_cat_modal_frame_self_test(void);
bool bongo_cat_model_frame_due(const BongoCatApp *app, uint64_t now);
bool bongo_cat_wait_event(SDL_Event *event, int timeout_ms);
bool bongo_cat_app_step_live2d(BongoCatApp *app, float elapsed_seconds);
void bongo_cat_window_sync_click_through(BongoCatApp *app);
void bongo_cat_window_apply_pending_resize(BongoCatApp *app);
void bongo_cat_window_wheel(BongoCatApp *app, const SDL_MouseWheelEvent *event);
void bongo_cat_window_update_wheel_animation(BongoCatApp *app, uint64_t now);
void bongo_cat_window_cancel_wheel_animation(BongoCatApp *app);
bool bongo_cat_window_wheel_self_test(BongoCatApp *app);
bool bongo_cat_window_scaled_size(int base_width, int base_height, float base_scale,
    float requested_scale, float *actual_scale, int *width, int *height);
bool bongo_cat_window_frame_size(BongoCatApp *app,
    int content_width, int content_height, int *width, int *height,
    int *left, int *top);
bool bongo_cat_window_content_size(BongoCatApp *app,
    int width, int height, int *content_width, int *content_height);
bool bongo_cat_window_apply_geometry(BongoCatApp *app, int x, int y,
    float scale, int width, int height);
bool bongo_cat_window_set_scale(BongoCatApp *app, float scale);
void bongo_cat_window_clamp_to_display(BongoCatApp *app);
void bongo_cat_window_drag_to(BongoCatApp *app, int x, int y);
void bongo_cat_window_drag_bounds_refresh(BongoCatApp *app);
void bongo_cat_window_drag_bounds_clear(BongoCatApp *app);
bool bongo_cat_window_recover_to_display(BongoCatApp *app);
void bongo_cat_window_display_event(BongoCatApp *app, const SDL_Event *event);
void bongo_cat_window_update_display_recovery(BongoCatApp *app, uint64_t now);
bool bongo_cat_window_display_self_test(BongoCatApp *app);
void bongo_cat_window_drag_begin(BongoCatApp *app,
    const SDL_MouseButtonEvent *event);
void bongo_cat_window_drag_motion(BongoCatApp *app,
    const SDL_MouseMotionEvent *event);
void bongo_cat_window_drag_end(BongoCatApp *app);
void bongo_cat_window_resize_by_pointer(BongoCatApp *app, const SDL_Event *event);
const char *bongo_cat_gamepad_axis_name(Uint8 axis);
const char *bongo_cat_gamepad_button_name(Uint8 button);
void bongo_cat_gamepads_set_enabled(BongoCatApp *app, bool enabled);
void bongo_cat_app_reset_gamepad(BongoCatApp *app);
void bongo_cat_app_apply_mouse(BongoCatApp *app);
void bongo_cat_app_reset_pointer_tracking(BongoCatApp *app);
void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts);
void bongo_cat_app_apply_mouse_position(BongoCatApp *app, double x, double y,
    float elapsed_seconds);
bool bongo_cat_app_audit_screen_pointer(BongoCatApp *app);
bool bongo_cat_app_audit_display_pointer(BongoCatApp *app);
void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y);
void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now);
void bongo_cat_app_update_hover_fade(BongoCatApp *app, uint64_t now);
void bongo_cat_app_cancel_hover_fade(BongoCatApp *app);
bool bongo_cat_app_shortcuts_self_test(BongoCatApp *app);
bool bongo_cat_app_sound_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event,
    bool *changed);
void bongo_cat_window_menu_action(BongoCatApp *app, BongoCatMenuAction action);
bool bongo_cat_window_menu_self_test(BongoCatApp *app);
bool bongo_cat_window_geometry_self_test(BongoCatApp *app);
void bongo_cat_window_show_context_menu(BongoCatApp *app);
void bongo_cat_live2d_audit_run(BongoCatApp *app);
bool bongo_cat_live2d_visual_audit_run(BongoCatApp *app);
bool bongo_cat_live2d_viewer_audit_run(BongoCatApp *app);
void bongo_cat_frame_audit(BongoCatApp *app, int width, int height);
void bongo_cat_frame_presentation_prepare(BongoCatApp *app,
    const unsigned char *pixels, int width, int height, bool visible);
void bongo_cat_frame_presented_audit(BongoCatApp *app);
void bongo_cat_window_clear_background(BongoCatApp *app);
void bongo_cat_window_mask_corners(BongoCatApp *app, int width, int height);
void bongo_cat_window_destroy_corner_mask(void);
void bongo_cat_app_render_now(BongoCatApp *app);
bool bongo_cat_app_capture_pending_model_cover(BongoCatApp *app);
void bongo_cat_runtime_flow_update(BongoCatApp *app, uint64_t now);
void bongo_cat_random_expression_update(BongoCatApp *app, uint64_t now);
void bongo_cat_random_expression_reset(BongoCatApp *app);
bool bongo_cat_system_language(BongoCatLanguage *language);
void bongo_cat_config_store_load(BongoCatApp *app);
void bongo_cat_config_store_update(BongoCatApp *app, uint64_t now);
void bongo_cat_config_store_flush(BongoCatApp *app);
bool bongo_cat_multi_pet_secondary_argument(int argc, char **argv);
bool bongo_cat_multi_pet_state_directory(char *target, size_t capacity,
    const char *root, const char *model_id);
void bongo_cat_multi_pet_update(BongoCatApp *app, uint64_t now);
void bongo_cat_multi_pet_prune_selection(BongoCatApp *app);
void bongo_cat_multi_pet_primary_update(BongoCatApp *app, uint64_t now);
bool bongo_cat_multi_pet_request_remove(BongoCatApp *app);
bool bongo_cat_multi_pet_request_preferences(BongoCatApp *app);
bool bongo_cat_multi_pet_request_exit(BongoCatApp *app);
bool bongo_cat_multi_pet_request_pass_through(BongoCatApp *app,
    bool enabled);
void bongo_cat_multi_pet_primary_requests_update(BongoCatApp *app);
void bongo_cat_multi_pet_clear_primary_request(BongoCatApp *app,
    const char *model_id);
void bongo_cat_multi_pet_pass_through_requests_update(BongoCatApp *app);
void bongo_cat_multi_pet_clear_pass_through_request(BongoCatApp *app,
    const char *model_id);
bool bongo_cat_multi_pet_take_remove_request(BongoCatApp *app,
    const char *model_id);
void bongo_cat_multi_pet_clear_remove_request(BongoCatApp *app,
    const char *model_id);
void bongo_cat_multi_pet_shutdown(BongoCatApp *app);

#endif
