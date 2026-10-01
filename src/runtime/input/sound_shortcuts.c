#include "runtime.h"

#include <string.h>

bool bongo_cat_app_sound_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event,
    bool *changed) {
    *changed = bongo_cat_sound_shortcut_update(&app->sound_shortcut_state, event);
    if (!*changed) return false;
    bool handled = false;
    const BongoCatBehaviorEntry *pending[BONGO_CAT_BEHAVIOR_BINDING_CAP];
    size_t pending_count = 0;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding = &app->settings.behavior_shortcuts[i];
        for (size_t j = 0; j < app->behaviors.count; ++j) {
            const BongoCatBehaviorEntry *entry = &app->behaviors.entries[j];
            if (entry->kind != BONGO_CAT_BEHAVIOR_SOUND || strcmp(binding->id, entry->id)) continue;
            bool down = bongo_cat_sound_shortcut_down(&app->sound_shortcut_state, binding->shortcut);
            handled = handled || down;
            if (down && !app->sound_shortcut_active[i] &&
                pending_count < BONGO_CAT_BEHAVIOR_BINDING_CAP) pending[pending_count++] = entry;
            app->sound_shortcut_active[i] = down;
        }
    }
    for (size_t i = 0; i < pending_count; ++i) bongo_cat_app_run_behavior(app, pending[i]);
    return handled;
}
