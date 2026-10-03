#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONTROL_FILE "control.txt"
#define REMOVE_REQUEST_FILE "remove.request"
#define CONTROL_CHECK_NS 100000000ull
#define CONTROL_STALE_SECONDS 10
#define CONTROL_RETRY_GRACE_NS 5000000000ull
#define SETTINGS_CHECK_NS 2000000000ull

typedef enum ControlReadResult {
    CONTROL_READ_OK, CONTROL_READ_RETRY, CONTROL_READ_END
} ControlReadResult;

typedef struct ControlState {
    bool visible;
    bool pass_through;
} ControlState;

static uint64_t model_hash(const char *model_id) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)model_id; p && *p; ++p) {
        hash ^= *p;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool bongo_cat_multi_pet_state_directory(char *target, size_t capacity,
    const char *root, const char *model_id) {
    char pets[BONGO_CAT_PATH_CAP], name[24];
    snprintf(name, sizeof(name), "%016llx",
        (unsigned long long)model_hash(model_id));
    return root && model_id && model_id[0] &&
        bongo_cat_path_join(pets, sizeof(pets), root, "pets") &&
        bongo_cat_path_join(target, capacity, pets, name);
}

static bool primary_remove_request_path(char *target, size_t capacity,
    const BongoCatApp *app, const char *model_id) {
    char directory[BONGO_CAT_PATH_CAP];
    return app && bongo_cat_multi_pet_state_directory(directory,
        sizeof(directory), app->primary_state_root, model_id) &&
        bongo_cat_path_join(target, capacity, directory, REMOVE_REQUEST_FILE);
}

bool bongo_cat_multi_pet_take_remove_request(BongoCatApp *app,
    const char *model_id) {
    char path[BONGO_CAT_PATH_CAP];
    if (!primary_remove_request_path(path, sizeof(path), app, model_id) ||
        !bongo_cat_path_is_file(path)) return false;
    if (bongo_cat_file_remove(path)) return true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "[runtime] Multi-pet remove request could not be consumed: "
        "model=%s path=%s", model_id, path);
    return false;
}

void bongo_cat_multi_pet_clear_remove_request(BongoCatApp *app,
    const char *model_id) {
    char path[BONGO_CAT_PATH_CAP];
    if (primary_remove_request_path(path, sizeof(path), app, model_id) &&
        bongo_cat_path_is_file(path)) bongo_cat_file_remove(path);
}

bool bongo_cat_multi_pet_secondary_argument(int argc, char **argv) {
    static const char prefix[] = "--secondary-pet=";
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], prefix, sizeof(prefix) - 1) &&
            argv[i][sizeof(prefix) - 1]) return true;
    return false;
}

bool bongo_cat_multi_pet_request_remove(BongoCatApp *app) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    int length;
    if (!app || !app->secondary_pet ||
        !bongo_cat_path_join(path, sizeof(path), app->state_root,
            REMOVE_REQUEST_FILE) ||
        (length = snprintf(temporary, sizeof(temporary), "%s.tmp", path)) < 0 ||
        (size_t)length >= sizeof(temporary)) return false;
    FILE *file = bongo_cat_file_open(temporary, "wb");
    bool written = file && fprintf(file, "%s\n", app->secondary_model_id) > 0;
    if (file && fclose(file) != 0) written = false;
    if (!written || !bongo_cat_file_replace(temporary, path, true)) {
        bongo_cat_file_remove(temporary);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet remove request failed: model=%s path=%s",
            app->secondary_model_id, path);
        return false;
    }
    return true;
}

static ControlReadResult read_control(BongoCatApp *app,
    ControlState *control, const char **retry_reason) {
    char path[BONGO_CAT_PATH_CAP], content[64] = {0};
    if (!bongo_cat_path_join(path, sizeof(path), app->state_root,
            CONTROL_FILE)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet child control path is too long: model=%s",
            app->secondary_model_id);
        return CONTROL_READ_END;
    }
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) {
        *retry_reason = "missing";
        return CONTROL_READ_RETRY;
    }
    size_t length = file ? fread(content, 1, sizeof(content) - 1, file) : 0;
    if (file) fclose(file);
    content[length] = '\0';
    char state[8] = {0}, extra = 0;
    long long heartbeat = 0;
    int pass_through = app->settings.window.pass_through;
    int fields = sscanf(content, "%7s %lld %d %c", state, &heartbeat,
        &pass_through, &extra);
    bool valid = fields == 3 &&
        (pass_through == 0 || pass_through == 1);
    time_t now = time(NULL), then = (time_t)heartbeat;
    if (!valid) {
        *retry_reason = "invalid-format";
        return CONTROL_READ_RETRY;
    }
    if (now < then || now - then > CONTROL_STALE_SECONDS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet child control expired: model=%s "
            "reason=stale age=%lld path=%s", app->secondary_model_id,
            (long long)(now - then), path);
        return CONTROL_READ_END;
    }
    control->visible = !strcmp(state, "visible");
    if (!control->visible && strcmp(state, "hidden")) {
        *retry_reason = "invalid-state";
        return CONTROL_READ_RETRY;
    }
    control->pass_through = pass_through != 0;
    return CONTROL_READ_OK;
}

static void reload_secondary_settings(BongoCatApp *app) {
    BongoCatSettings settings = app->settings;
    BongoCatSettings previous_settings = app->settings;
    BongoCatError error = {0};
    BongoCatResult loaded = bongo_cat_settings_load(
        app->settings_path, &settings, &error);
    if (loaded != BONGO_CAT_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet child settings sync failed: model=%s "
            "result=%d error=%s", app->secondary_model_id, loaded,
            error.message[0] ? error.message : "unknown error");
        return;
    }
    if (app->secondary_control_known)
        settings.window.pass_through = app->secondary_control_pass_through;
    bool model_changed =
        settings.model.multiple_pets != app->settings.model.multiple_pets ||
        settings.model.mirror != app->settings.model.mirror ||
        settings.model.mouse_mirror != app->settings.model.mouse_mirror ||
        settings.model.mouse_centered != app->settings.model.mouse_centered ||
        settings.model.ignore_mouse != app->settings.model.ignore_mouse ||
        settings.model.gamepad_four_hands != app->settings.model.gamepad_four_hands ||
        settings.model.dynamic_texture_resolution !=
            app->settings.model.dynamic_texture_resolution ||
        settings.model.max_fps != app->settings.model.max_fps;
    bool texture_resolution_changed =
        settings.model.dynamic_texture_resolution !=
        app->settings.model.dynamic_texture_resolution;
    bool hands_changed = settings.model.gamepad_four_hands !=
        app->settings.model.gamepad_four_hands;
    bool window_changed =
        settings.window.pass_through != app->settings.window.pass_through ||
        settings.window.always_on_top != app->settings.window.always_on_top ||
        settings.window.hide_on_hover != app->settings.window.hide_on_hover ||
        settings.window.keep_in_screen != app->settings.window.keep_in_screen ||
        settings.window.obs_background != app->settings.window.obs_background ||
        settings.window.random_expression != app->settings.window.random_expression ||
        settings.window.random_motion != app->settings.window.random_motion ||
        settings.window.rounded_corners != app->settings.window.rounded_corners ||
        settings.window.obs_background_color !=
            app->settings.window.obs_background_color ||
        settings.window.hide_delay_seconds != app->settings.window.hide_delay_seconds ||
        settings.window.hide_fade_seconds != app->settings.window.hide_fade_seconds ||
        settings.window.random_expression_interval_seconds !=
            app->settings.window.random_expression_interval_seconds ||
        settings.window.random_motion_interval_seconds !=
            app->settings.window.random_motion_interval_seconds ||
        settings.window.corner_radius_percent !=
            app->settings.window.corner_radius_percent;
    app->settings = settings;
    if (hands_changed) bongo_cat_app_refresh_hands(app);
    if (texture_resolution_changed && app->loaded_model[0]) {
        BongoCatError reload_error = {0};
        if (!bongo_cat_app_reload_model_with_error(app, &reload_error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[runtime] Multi-pet texture resolution reload failed: %s",
                reload_error.message);
            /* Keep the setting aligned with the model that survived the
               failed transaction. Other settings from the control file may
               still be applied normally. */
            app->settings.model.dynamic_texture_resolution =
                previous_settings.model.dynamic_texture_resolution;
        }
    }
    const BongoCatModelEntry *active = bongo_cat_models_find(&app->models, app->loaded_model);
    if (active && !bongo_cat_mver_shortcuts_load(app, active, &error))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    if (model_changed) app->dirty = true;
    if (window_changed) {
        app->dirty = true;
        bongo_cat_platform_set_always_on_top(&app->platform,
            app->settings.window.always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    }
}

static void update_secondary(BongoCatApp *app, uint64_t now) {
    if (!app->secondary_control_check_ns ||
        now >= app->secondary_control_check_ns) {
        ControlState control = {0};
        const char *retry_reason = "unknown";
        ControlReadResult result = read_control(app, &control, &retry_reason);
        if (result == CONTROL_READ_END) app->running = false;
        else if (result == CONTROL_READ_RETRY) {
            if (!app->secondary_control_failure_ns)
                app->secondary_control_failure_ns = now;
            else if (now - app->secondary_control_failure_ns >=
                CONTROL_RETRY_GRACE_NS) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[runtime] Multi-pet child control unavailable: model=%s "
                    "reason=%s", app->secondary_model_id, retry_reason);
                app->running = false;
            }
        } else {
            bool visibility_changed = !app->secondary_control_known ||
                control.visible != app->session.window.visible;
            bool pass_changed = !app->secondary_control_known ||
                control.pass_through != app->settings.window.pass_through;
            app->secondary_control_failure_ns = 0;
            app->secondary_control_known = true;
            app->secondary_control_visible = control.visible;
            app->secondary_control_pass_through = control.pass_through;
            if (pass_changed) {
                app->settings.window.pass_through = control.pass_through;
                bongo_cat_window_mark_hit_dirty(app);
                bongo_cat_window_sync_click_through(app);
            }
            if (visibility_changed)
                bongo_cat_window_set_visible(app, control.visible);
        }
        app->secondary_control_check_ns = now + CONTROL_CHECK_NS;
    }
    if (!app->secondary_settings_check_ns ||
        now >= app->secondary_settings_check_ns) {
        reload_secondary_settings(app);
        app->secondary_settings_check_ns = now + SETTINGS_CHECK_NS;
    }
}

void bongo_cat_multi_pet_update(BongoCatApp *app, uint64_t now) {
    if (!app) return;
    if (app->secondary_pet) update_secondary(app, now);
    else bongo_cat_multi_pet_primary_update(app, now);
}
