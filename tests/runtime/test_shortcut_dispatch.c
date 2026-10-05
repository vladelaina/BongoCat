#include "shortcut_test_cases.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
BongoCatApp *shortcut_app;
static BongoCatApp *reference_app;
static ShortcutTrace actual_trace, expected_trace;
static unsigned events_checked;
static size_t minimum_behaviors;

void shortcut_reset(size_t behaviors, size_t bindings) {
    shortcut_fixture(shortcut_app,
        behaviors < minimum_behaviors ? minimum_behaviors : behaviors, bindings);
    memset(&actual_trace, 0, sizeof(actual_trace));
    actual_trace.expression = -1;
    expected_trace = actual_trace;
}

void shortcut_check(BongoCatInputKind kind, const char *name, float value,
    const char *expected) {
    BongoCatInputEvent event = shortcut_event(kind, name, value);
    /* Copy before every event: test callers may modify bindings between them. */
    memcpy(reference_app, shortcut_app, sizeof(*reference_app));
    actual_trace.count = expected_trace.count = 0;
    memset(actual_trace.actions, 0, sizeof(actual_trace.actions));
    memset(expected_trace.actions, 0, sizeof(expected_trace.actions));
    expected_trace = actual_trace;
    shortcut_trace = &actual_trace;
    bongo_cat_app_shortcuts(shortcut_app, &event);
    shortcut_trace = &expected_trace;
    reference_app_shortcuts(reference_app, &event);
    CHECK(memcmp(shortcut_app, reference_app, sizeof(*shortcut_app)) == 0);
    CHECK(memcmp(&actual_trace, &expected_trace, sizeof(actual_trace)) == 0);
    if (expected) {
        CHECK(actual_trace.count == strlen(expected));
        for (size_t i = 0; i < actual_trace.count && i < strlen(expected); ++i)
            CHECK(actual_trace.actions[i].kind == expected[i]);
    }
    ++events_checked;
}

static void ordered_actions(void) {
    shortcut_reset(4, 5);
    shortcut_app->behaviors.entries[0].kind = BONGO_CAT_BEHAVIOR_SOUND;
    shortcut_app->behaviors.entries[1].kind = BONGO_CAT_BEHAVIOR_EFFECT;
    shortcut_app->behaviors.entries[2].kind = BONGO_CAT_BEHAVIOR_EXPRESSION;
    shortcut_app->behaviors.entries[3].kind = BONGO_CAT_BEHAVIOR_MOTION;
    const size_t order[] = {2, 0, 3, 1, 0};
    for (size_t i = 0; i < 5; ++i)
        shortcut_binding(shortcut_app, i,
            shortcut_app->behaviors.entries[order[i]].id, "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "QSMSES");
    CHECK(actual_trace.actions[0].value == 2);
    CHECK(strcmp(actual_trace.actions[1].argument, "sound-000.wav") == 0);
    /* Repeated down events still dispatch behavior bindings upstream. */
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "QSMSES");
    CHECK(actual_trace.actions[0].value == 2);
    strcpy(shortcut_app->behaviors.entries[1].id, shortcut_app->behaviors.entries[0].id);
    shortcut_app->config.behavior_shortcut_count = 1;
    shortcut_binding(shortcut_app, 0, shortcut_app->behaviors.entries[0].id, "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "SE");
    actual_trace.fail_effect = true;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "SE");
    shortcut_binding(shortcut_app, 0, "missing-id", "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "");
}

static void releases_and_gamepad(void) {
    shortcut_reset(3, 3);
    for (size_t i = 0; i < 3; ++i) {
        shortcut_app->behaviors.entries[i].momentary = true;
        shortcut_binding(shortcut_app, i, shortcut_app->behaviors.entries[i].id, "Ctrl+A");
    }
    shortcut_app->behaviors.entries[0].kind = BONGO_CAT_BEHAVIOR_EFFECT;
    shortcut_app->behaviors.entries[1].kind = BONGO_CAT_BEHAVIOR_SOUND;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ESS");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "ControlLeft", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "EXX");
    /* Release matching deliberately ignores modifier state and supports a
       modifier-named primary even though its down event is not primary. */
    shortcut_binding(shortcut_app, 0, shortcut_app->behaviors.entries[0].id, "ControlLeft");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "ControlLeft", 0, "E");
    for (size_t i = 0; i < 3; ++i)
        shortcut_binding(shortcut_app, i, shortcut_app->behaviors.entries[i].id, "Gamepad:South");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "south", 1, "ESS");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 0.5f, "EXX");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 0.50001f, "ESS");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", NAN, "");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", -INFINITY, "EXX");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", INFINITY, "ESS");
    shortcut_app->behaviors.entries[0].momentary = false;
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 0, "XX");
}

static void globals_and_fallback(void) {
    shortcut_reset(10, 1);
    BongoCatShortcutOptions *keys = &shortcut_app->config.shortcuts;
    strcpy(keys->visible_cat, "A");
    strcpy(keys->visible_preferences, "A");
    strcpy(keys->mirror, "A");
    strcpy(keys->pass_through, "A");
    strcpy(keys->always_on_top, "A");
    shortcut_binding(shortcut_app, 0, shortcut_app->behaviors.entries[2].id, "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "V");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "S");
    keys->visible_cat[0] = '\0';
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "X");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "P");
    keys->visible_preferences[0] = '\0';
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "X");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "I");
    keys->mirror[0] = '\0';
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "X");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "HTI");
    keys->pass_through[0] = '\0';
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "X");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "AHTI");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Alt", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "MS");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num0", 0, "Q");
    shortcut_binding(shortcut_app, 0, shortcut_app->behaviors.entries[3].id, "Alt+1");
    actual_trace.fail_effect = true;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "EMS");
    actual_trace.fail_effect = false;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "E");
}

static void multiple_motions(void) {
    shortcut_reset(2, 2);
    shortcut_app->behaviors.entries[1].kind = BONGO_CAT_BEHAVIOR_MOTION;
    shortcut_binding(shortcut_app, 0, shortcut_app->behaviors.entries[1].id, "A");
    shortcut_binding(shortcut_app, 1, shortcut_app->behaviors.entries[0].id, "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "MSMS");
    CHECK(actual_trace.actions[0].value == 1);
    CHECK(actual_trace.actions[2].value == 0);
    /* Current upstream dispatches both motions in binding order. */
    strcpy(shortcut_app->config.behavior_shortcuts[1].shortcut, "a");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "MSMS");
    shortcut_app->config.behavior_shortcut_count = 1;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "MS");
    actual_trace.fail_motion = true;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "M");
}

static void empty_sound_and_failed_expression(void) {
    shortcut_reset(2, 2);
    shortcut_app->behaviors.entries[0].kind = BONGO_CAT_BEHAVIOR_SOUND;
    shortcut_app->behaviors.entries[0].sound[0] = '\0';
    for (size_t i = 0; i < 2; ++i)
        shortcut_binding(shortcut_app, i, shortcut_app->behaviors.entries[i].id, "A");
    actual_trace.fail_expression = true;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "XQ");
    CHECK(!shortcut_app->dirty);
    actual_trace.fail_expression = false;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "XQ");
    CHECK(shortcut_app->dirty);
    /* Empty IDs also compare equal upstream; do not silently validate them. */
    shortcut_app->behaviors.entries[0].id[0] = '\0';
    shortcut_binding(shortcut_app, 0, "", "A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "XQ");
}

int main(void) {
    shortcut_app = calloc(1, sizeof(*shortcut_app));
    reference_app = calloc(1, sizeof(*reference_app));
    if (!shortcut_app || !reference_app) return 2;
    /* Include duplicate/stale IDs immediately around the strategy boundary. */
    const size_t sizes[] = {0, 31, 32, 33};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        minimum_behaviors = sizes[i];
        ordered_actions();
        releases_and_gamepad();
        globals_and_fallback();
        multiple_motions();
        empty_sound_and_failed_expression();
    }
    minimum_behaviors = 0;
    shortcut_random_cases();
    printf("Shortcut differential: %u ordered-action/state comparisons, %d failures\n",
        events_checked, bongo_cat_test_failures);
    free(reference_app);
    free(shortcut_app);
    return bongo_cat_test_failures ? 1 : 0;
}
