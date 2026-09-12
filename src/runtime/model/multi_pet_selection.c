#include "runtime.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

static bool load_pet_window(const BongoCatApp *app, const char *model_id,
    BongoCatWindowState *window) {
    char directory[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    BongoCatSessionState session;
    BongoCatError error = {0};
    bongo_cat_session_defaults(&session);
    if (!bongo_cat_multi_pet_state_directory(directory, sizeof(directory),
            app->primary_state_root, model_id) ||
        !bongo_cat_path_join(path, sizeof(path), directory, "session.json") ||
        bongo_cat_session_load(path, &session, &error) != BONGO_CAT_OK ||
        !session.window.position_known) return false;
    *window = session.window;
    return true;
}

static void adopt_pet_window(BongoCatApp *app,
    const BongoCatWindowState *window) {
    if (!bongo_cat_window_apply_geometry(app, window->x, window->y,
            window->scale_percent, window->width, window->height)) return;
    app->session.window.opacity_percent = window->opacity_percent;
    bongo_cat_app_cancel_hover_fade(app);
    bongo_cat_platform_set_opacity(&app->platform,
        window->opacity_percent / 100.0f);
}

bool bongo_cat_app_model_active(const BongoCatApp *app, const char *id) {
    if (!app || !id) return false;
    if (!strcmp(app->session.active_model_id, id)) return true;
    return app->settings.model.multiple_pets &&
        bongo_cat_session_model_active(&app->session, id);
}

size_t bongo_cat_app_active_model_count(const BongoCatApp *app) {
    return app && app->settings.model.multiple_pets
        ? 1 + app->session.additional_model_count : app ? 1 : 0;
}

bool bongo_cat_app_set_model_active(BongoCatApp *app, const char *id,
    bool active, BongoCatError *error) {
    if (!app || !id || !bongo_cat_models_find(&app->models, id)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Model is not installed: %s", id ? id : "");
        return false;
    }
    if (!app->settings.model.multiple_pets)
        return active ? bongo_cat_app_select_model_with_error(app, id, error)
            : true;
    bool primary = !strcmp(app->session.active_model_id, id);
    if (active) {
        if (!bongo_cat_session_add_model(&app->session, id)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
                "At most %d desktop pets can be displayed",
                BONGO_CAT_ACTIVE_MODEL_CAP);
            return false;
        }
    } else if (primary) {
        if (!app->session.additional_model_count) return true;
        char replacement[BONGO_CAT_ID_CAP];
        BongoCatWindowState replacement_window;
        snprintf(replacement, sizeof(replacement), "%s",
            app->session.additional_model_ids[0]);
        bool preserve_window = load_pet_window(app, replacement,
            &replacement_window);
        if (!bongo_cat_app_select_model_with_error(app, replacement, error))
            return false;
        bongo_cat_session_remove_model(&app->session, replacement);
        if (preserve_window) adopt_pet_window(app, &replacement_window);
    } else bongo_cat_session_remove_model(&app->session, id);
    bongo_cat_multi_pet_primary_update(app, SDL_GetTicksNS());
    return true;
}

void bongo_cat_app_set_multiple_pets(BongoCatApp *app, bool enabled) {
    if (!app) return;
    app->settings.model.multiple_pets = enabled;
    if (!enabled) bongo_cat_session_clear_additional_models(&app->session);
    if (!app->secondary_pet)
        bongo_cat_multi_pet_primary_update(app, SDL_GetTicksNS());
}

void bongo_cat_multi_pet_prune_selection(BongoCatApp *app) {
    bongo_cat_session_validate(&app->session);
    for (size_t i = app->session.additional_model_count; i > 0; --i) {
        const char *id = app->session.additional_model_ids[i - 1];
        if (!bongo_cat_models_find(&app->models, id))
            bongo_cat_session_remove_model(&app->session, id);
    }
}
