#include "runtime.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned triggered[2][3];

/* Observe dispatch without requiring a renderer or audio device. */
bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    triggered[app->secondary_pet ? 1 : 0][behavior->index]++;
    return true;
}

static void key(BongoCatApp *app, const char *name, bool down, bool allow) {
    BongoCatInputEvent event = {
        .kind = down ? BONGO_CAT_INPUT_KEY_DOWN : BONGO_CAT_INPUT_KEY_UP};
    snprintf(event.name, sizeof(event.name), "%s", name);
    CHECK(bongo_cat_input_push(&app->input, &event));
    bongo_cat_app_drain_input(app, allow);
}

static void chord(BongoCatApp *app, bool allow) {
    key(app, "ControlLeft", true, allow);
    key(app, "KeyJ", true, allow);
    key(app, "KeyJ", false, allow);
    key(app, "ControlLeft", false, allow);
}

int main(void) {
    BongoCatApp *pets = calloc(2, sizeof(*pets));
    if (!pets) return 1;
    const BongoCatBehaviorKind kinds[] = {BONGO_CAT_BEHAVIOR_MOTION,
        BONGO_CAT_BEHAVIOR_EXPRESSION, BONGO_CAT_BEHAVIOR_SOUND};
    for (unsigned p = 0; p < 2; ++p) {
        BongoCatApp *app = &pets[p];
        app->secondary_pet = p != 0;
        bongo_cat_input_init(&app->input);
        CHECK(bongo_cat_behaviors_reserve(&app->behaviors, 3, NULL));
        if (!app->behaviors.entries) return 1;
        app->behaviors.count = app->settings.behavior_shortcut_count = 3;
        for (unsigned i = 0; i < 3; ++i) {
            BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
            memset(entry, 0, sizeof(*entry));
            entry->kind = kinds[i];
            entry->index = (int)i;
            snprintf(entry->id, sizeof(entry->id), "pet%u:behavior%u", p, i);
            BongoCatBehaviorShortcut *binding = &app->settings.behavior_shortcuts[i];
            snprintf(binding->id, sizeof(binding->id), "%.*s",
                (int)sizeof(binding->id) - 1, entry->id);
            snprintf(binding->shortcut, sizeof(binding->shortcut), "Control+J");
        }
        chord(app, true);
        chord(app, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        /* Modal suppression must still apply to both primary and child pets. */
        chord(app, false);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        app->settings.behavior_shortcuts[1].shortcut_disabled = true;
        chord(app, true);
        CHECK(triggered[p][0] == 3 && triggered[p][1] == 2 && triggered[p][2] == 3);
        snprintf(app->settings.shortcuts.mirror,
            sizeof(app->settings.shortcuts.mirror), "Control+M");
        key(app, "ControlLeft", true, true);
        key(app, "KeyM", true, true);
        key(app, "KeyM", false, true);
        key(app, "ControlLeft", false, true);
        CHECK(app->settings.model.mirror == (p == 0));
        for (unsigned i = 0; i < 3; ++i) {
            app->settings.behavior_shortcuts[i].shortcut_disabled = false;
            snprintf(app->settings.behavior_shortcuts[i].shortcut,
                sizeof(app->settings.behavior_shortcuts[i].shortcut), "J+K");
            triggered[p][i] = 0;
        }
        snprintf(app->settings.shortcuts.mirror,
            sizeof(app->settings.shortcuts.mirror), "J+K");
        app->settings.model.mirror = false;
        key(app, "KeyK", true, true);
        key(app, "KeyK", false, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 0);
        CHECK(!app->settings.model.mirror);
        /* Test application commands separately from model dispatch priority. */
        key(app, "KeyJ", true, true);
        key(app, "KeyK", true, true);
        CHECK(app->settings.model.mirror == (p == 0));
        key(app, "KeyK", false, true);
        key(app, "KeyJ", false, true);
        app->settings.shortcuts.mirror[0] = '\0';
        for (unsigned i = 0; i < 3; ++i) triggered[p][i] = 0;
        for (int reverse = 0; reverse < 2; ++reverse) {
            const char *first = reverse ? "KeyK" : "KeyJ";
            const char *second = reverse ? "KeyJ" : "KeyK";
            key(app, first, true, true);
            for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == (unsigned)reverse);
            key(app, second, true, true);
            key(app, first, true, true);
            key(app, second, true, true);
            for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == (unsigned)reverse + 1);
            key(app, first, false, true);
            key(app, second, false, true);
        }
        /* Explicit bindings and disabled bindings suppress the old Alt+number aliases. */
        app->settings.behavior_shortcuts[1].shortcut_disabled = true;
        key(app, "Alt", true, true);
        key(app, "Num1", true, true);
        key(app, "Num1", false, true);
        key(app, "Num2", true, true);
        key(app, "Num2", false, true);
        key(app, "Alt", false, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        app->settings.behavior_shortcuts[1].shortcut_disabled = false;
        key(app, "KeyJ", true, false);
        key(app, "KeyK", true, false);
        key(app, "KeyX", true, true);
        key(app, "KeyX", false, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        key(app, "KeyK", false, false);
        key(app, "KeyJ", false, false);
        key(app, "KeyK", true, true);
        key(app, "KeyJ", true, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 3);
        key(app, "KeyK", false, true);
        key(app, "KeyJ", false, true);
        CHECK(bongo_cat_app_shortcut_conflicts(app, "K+J", app->settings.shortcuts.mirror));
        bongo_cat_behaviors_clear(&app->behaviors);
    }
    free(pets);
    return bongo_cat_test_failures ? 1 : 0;
}
