#include "model_catalog_selection.h"
#include "bongo_cat/overlay.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

BongoCatModelSelection bongo_cat_model_selection_capture(
    const BongoCatApp *app) {
    BongoCatModelSelection selection = {0};
    if (!app) return selection;
    selection.count = app->session.additional_model_count;
    if (selection.count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        selection.count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    memcpy(selection.additional, app->session.additional_model_ids,
        selection.count * sizeof(selection.additional[0]));
    return selection;
}

void bongo_cat_model_selection_restore(BongoCatApp *app,
    const BongoCatModelSelection *selection) {
    if (!app || !selection) return;
    bongo_cat_session_clear_additional_models(&app->session);
    for (size_t i = 0; i < selection->count; ++i)
        if (bongo_cat_models_find(&app->models, selection->additional[i]))
            bongo_cat_session_add_model(&app->session,
                selection->additional[i]);
    bongo_cat_multi_pet_primary_update(app, SDL_GetTicksNS());
}

static const BongoCatModelEntry *fallback_model(
    const BongoCatModelCatalog *models) {
    static const char *const ids[] = {"standard", "keyboard", "gamepad"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const BongoCatModelEntry *entry = bongo_cat_models_find(models, ids[i]);
        if (entry) return entry;
    }
    return models && models->count ? &models->entries[0] : NULL;
}

static bool selection_matches(const BongoCatApp *app,
    const BongoCatModelSelection *selection) {
    if (!app || !selection || app->session.additional_model_count !=
            selection->count) return false;
    return !memcmp(app->session.additional_model_ids, selection->additional,
        selection->count * sizeof(selection->additional[0]));
}

static void clear_loaded_model(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_window_snapshot_end(app);
    bongo_cat_overlay_clear(app->overlay);
    app->dirty = true;
    app->loaded_model[0] = '\0';
    app->loading_model[0] = '\0';
    app->loaded_mode = BONGO_CAT_MODE_STANDARD;
    app->loaded_gamepad_keyboard = false;
    bongo_cat_gamepads_set_enabled(app, false);
    if (app->window) bongo_cat_window_set_visible(app, false);
}

bool bongo_cat_model_catalog_reconcile(BongoCatApp *app) {
    if (!app) return false;
    BongoCatModelSelection selection =
        bongo_cat_model_selection_capture(app);
    const BongoCatModelEntry *active = bongo_cat_models_find(&app->models,
        app->session.active_model_id);
    const BongoCatModelEntry *loaded = bongo_cat_models_find(&app->models,
        app->loaded_model);
    bool changed = false;
    if (!active) {
        active = loaded ? loaded : fallback_model(&app->models);
        if (active) {
            if (strcmp(app->session.active_model_id, active->id) != 0) {
                snprintf(app->session.active_model_id,
                    sizeof(app->session.active_model_id), "%s", active->id);
                changed = true;
            }
        } else if (app->session.active_model_id[0]) {
            app->session.active_model_id[0] = '\0';
            changed = true;
        }
    }
    bongo_cat_model_selection_restore(app, &selection);
    if (!selection_matches(app, &selection)) changed = true;
    if (app->loaded_model[0] && !loaded) {
        if (active && app->live2d) {
            BongoCatError error = {0};
            if (!bongo_cat_app_select_model_with_error(app, active->id,
                    &error)) {
                if (error.message[0]) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to restore an available model after refresh: %s",
                    error.message);
                clear_loaded_model(app);
                changed = true;
            }
        } else {
            clear_loaded_model(app);
            changed = true;
        }
    }
    if (!active && app->session.additional_model_count) {
        bongo_cat_session_clear_additional_models(&app->session);
        changed = true;
    }
    return changed;
}
