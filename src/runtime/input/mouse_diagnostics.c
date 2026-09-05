#include "mouse_diagnostics.h"

#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>

/* Diagnostics are deliberately isolated from input decisions so logging and
   smoke audits cannot obscure the runtime pointer state machine. */
void bongo_cat_mouse_audit(BongoCatApp *app, double x, double y) {
    if (!app->smoke_input_audit) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->state_root,
        "input-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "ab");
    if (!file) return;
    fprintf(file, "mouse x=%.2f y=%.2f\n", x, y);
    fclose(file);
}

void bongo_cat_mouse_log_diagnostics(BongoCatApp *app, uint64_t now,
    bool received, double target_x, double target_y,
    float global_x, float global_y, SDL_MouseButtonFlags buttons,
    bool cursor_locked, bool relative_requested) {
    bool hook_active = app->mouse_last_ns != 0;
    bool mismatch = hook_active &&
        (target_x != global_x || target_y != global_y);
    if (app->mouse_diagnostic_due_ns &&
        now < app->mouse_diagnostic_due_ns) return;
    app->mouse_diagnostic_due_ns = now +
        (mismatch || relative_requested ? 30000000000ull : 60000000000ull);
    uint64_t hook_age_ms = app->mouse_last_ns && now >= app->mouse_last_ns
        ? (now - app->mouse_last_ns) / 1000000ull : 0;
    SDL_Log("[input] Pointer pipeline: source=%s sample_now=%d "
        "samples=%llu age_ms=%llu selected=%.1f,%.1f sdl=%.1f,%.1f "
        "mismatch=%d sdl_buttons=0x%x model_buttons=%d,%d,%d "
        "pointer_known=%d cursor_locked=%d relative_requested=%d relative_device=%d "
        "dropped=%llu model=%s mode=%d ignore=%d centered=%d",
        hook_active ? "native-hook" : "sdl-fallback", received,
        (unsigned long long)app->mouse_hook_samples,
        (unsigned long long)hook_age_ms, target_x, target_y,
        global_x, global_y, mismatch, (unsigned)buttons,
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down,
        app->pointer_known, cursor_locked, relative_requested,
        app->platform.relative_pointer != NULL,
        (unsigned long long)atomic_load_explicit(&app->input.dropped,
            memory_order_relaxed),
        app->loaded_model[0] ? app->loaded_model : "none",
        (int)app->loaded_mode, app->settings.model.ignore_mouse,
        app->settings.model.mouse_centered);
}

void bongo_cat_mouse_log_mode_change(const BongoCatApp *app,
    bool cursor_locked, bool profile_relative, bool relative_requested,
    bool map_requested, bool map_ok, double target_x, double target_y,
    double model_x, double model_y, bool model_moved) {
    const char *reason = relative_requested
        ? (cursor_locked ? "cursor-locked" : "model-profile")
        : (cursor_locked ? "mouse-disabled" : "cursor-free");
    SDL_Log("[input] Pointer mode changed: mode=%s reason=%s lock=%d "
        "profile=%d map_requested=%d map_ok=%d relative_device=%d "
        "target=%.1f,%.1f virtual=%.1f,%.1f "
        "moved=%d model=%s",
        relative_requested ? "relative" : "absolute", reason,
        cursor_locked, profile_relative, map_requested, map_ok,
        app->platform.relative_pointer != NULL, target_x, target_y,
        model_x, model_y, model_moved,
        app->loaded_model[0] ? app->loaded_model : "none");
}
