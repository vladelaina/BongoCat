#ifndef BONGO_CAT_RUNTIME_INTERNAL_H
#define BONGO_CAT_RUNTIME_INTERNAL_H

#include "bongo_cat/app.h"
#include "bongo_cat/memory_policy.h"
#include <SDL3/SDL.h>

BongoCatResult bongo_cat_window_create(BongoCatApp *app, BongoCatError *error);
void bongo_cat_window_set_icon(BongoCatApp *app, SDL_Window *window);
BongoCatResult bongo_cat_app_locate_assets(BongoCatApp *app, BongoCatError *error);
bool bongo_cat_startup_prepare(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error);
void bongo_cat_startup_stage(BongoCatApp *app, const char *stage);
void bongo_cat_startup_ready(BongoCatApp *app);
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
bool bongo_cat_app_map_mver_pointer(BongoCatApp *app, double absolute_x,
    double absolute_y, double *x, double *y, bool *changed);
void bongo_cat_app_apply_mouse_coordinates(BongoCatApp *app, double hand_x,
    double hand_y, double gaze_x, double gaze_y);
void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts);
void bongo_cat_app_apply_mouse_position(BongoCatApp *app, double x, double y,
    float elapsed_seconds);
bool bongo_cat_app_audit_screen_pointer(BongoCatApp *app);
bool bongo_cat_app_audit_display_pointer(BongoCatApp *app);
void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y);
void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now);
BongoCatResult bongo_cat_copy_directory(const char *source, const char *target,
    BongoCatError *error);
bool bongo_cat_app_shortcuts_self_test(BongoCatApp *app);
void bongo_cat_window_menu_action(BongoCatApp *app, BongoCatMenuAction action);
bool bongo_cat_window_menu_self_test(BongoCatApp *app);
bool bongo_cat_window_geometry_self_test(BongoCatApp *app);
void bongo_cat_window_show_context_menu(BongoCatApp *app);
void bongo_cat_live2d_audit_run(BongoCatApp *app);
bool bongo_cat_live2d_visual_audit_run(BongoCatApp *app);
bool bongo_cat_live2d_viewer_audit_run(BongoCatApp *app);
void bongo_cat_frame_audit(BongoCatApp *app, int width, int height);
void bongo_cat_frame_capture_pending(BongoCatApp *app, int width, int height);
void bongo_cat_app_render_now(BongoCatApp *app);
void bongo_cat_runtime_flow_update(BongoCatApp *app, uint64_t now);
void bongo_cat_config_store_initialize(BongoCatApp *app);
void bongo_cat_config_store_update(BongoCatApp *app, uint64_t now);
void bongo_cat_config_store_flush(BongoCatApp *app);

#endif
