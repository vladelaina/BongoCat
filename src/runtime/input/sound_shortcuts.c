#include "runtime.h"

#include <string.h>

bool bongo_cat_app_sound_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event,
    bool *changed) {
    *changed = bongo_cat_sound_shortcut_update(&app->sound_shortcut_state, event);
    if (!*changed) return false;
    bool handled = false;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != BONGO_CAT_BEHAVIOR_SOUND) continue;
        const BongoCatBehaviorShortcut *binding = bongo_cat_app_behavior_binding(app, entry->id);
        bool down = binding && !binding->shortcut_disabled &&
            bongo_cat_sound_shortcut_down(&app->sound_shortcut_state, binding->shortcut);
        handled = handled || down;
        bool pressed = down && !app->behaviors.entries[i].shortcut_active;
        app->behaviors.entries[i].shortcut_active = down;
        if (pressed) bongo_cat_app_run_behavior(app, entry);
    }
    return handled;
}
