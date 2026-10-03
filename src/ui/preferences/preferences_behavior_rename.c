#include "preferences_state.h"
#include "preferences_text_edit.h"
#include "preferences_notice.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static BongoCatBehaviorShortcut *binding_for(BongoCatSettings *config,
    const char *id) {
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (!strcmp(config->behavior_shortcuts[i].id, id))
            return &config->behavior_shortcuts[i];
    if (config->behavior_shortcut_count >= BONGO_CAT_BEHAVIOR_BINDING_CAP)
        return NULL;
    BongoCatBehaviorShortcut *binding =
        &config->behavior_shortcuts[config->behavior_shortcut_count++];
    memset(binding, 0, sizeof(*binding));
    snprintf(binding->id, sizeof(binding->id), "%s", id);
    return binding;
}

void bongo_cat_preferences_behavior_rename_finish(
    BongoCatPreferences *value, bool save) {
    if (!value || !bongo_cat_preferences_text_session_active(
        &value->behavior_rename)) return;
    BongoCatPreferencesTextSession *session = &value->behavior_rename;
    bool label_changed = false;
    if (save) {
        bongo_cat_text_edit_trim(session->text);
        const BongoCatBehaviorEntry *entry = NULL;
        const BongoCatBehaviorCatalog *catalog =
            bongo_cat_preferences_behavior_catalog(value);
        for (size_t i = 0; i < catalog->count; ++i)
            if (!strcmp(catalog->entries[i].id, session->id)) {
                entry = &catalog->entries[i];
                break;
            }
        BongoCatBehaviorShortcut *binding = binding_for(&value->app->settings,
            session->id);
        if (binding) {
            const BongoCatBehaviorShortcut *source = bongo_cat_app_behavior_binding(value->app, session->id);
            if (source && source->shortcut_external) binding->shortcut_external = true;
            const char *label = entry && !strcmp(session->text, entry->label) ?
                "" : session->text;
            label_changed = strcmp(binding->label, label) != 0;
            snprintf(binding->label, sizeof(binding->label), "%s", label);
        } else bongo_cat_preferences_notice_show(value->app,
            bongo_cat_i18n_get(value->app->i18n,
                "pages.preference.model.hints.behaviorRenameLimit",
                "Cannot save another custom action name: user override limit reached"), true);
    }
    bongo_cat_preferences_text_session_reset(session);
    SDL_StopTextInput(value->window);
    if (label_changed) bongo_cat_preferences_reload_fonts(value);
    value->render_dirty = true;
}

void bongo_cat_preferences_behavior_rename_begin(BongoCatPreferences *value,
    const BongoCatBehaviorEntry *entry, BongoCatBehaviorShortcut *binding,
    struct nk_rect bounds) {
    if (!value || !entry) return;
    bongo_cat_preferences_shortcut_cancel(value);
    (void)binding;
    const char *label = bongo_cat_app_behavior_label(value->app, entry->id);
    bongo_cat_preferences_text_session_begin(&value->behavior_rename,
        entry->id, label ? label : entry->label, bounds);
    SDL_StartTextInput(value->window);
    value->render_dirty = true;
}

bool bongo_cat_preferences_behavior_rename_event(
    BongoCatPreferences *value, const SDL_Event *event) {
    if (!value) return false;
    BongoCatPreferencesTextSessionEvent result =
        bongo_cat_preferences_text_session_event(&value->behavior_rename,
            event, value->ui.layout_scale, value->ui.caption_font, 8.0f);
    if (result.finish)
        bongo_cat_preferences_behavior_rename_finish(value, result.save);
    if (result.handled) value->render_dirty = true;
    return result.handled;
}
