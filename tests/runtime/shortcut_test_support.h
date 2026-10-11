#ifndef BONGO_CAT_SHORTCUT_TEST_SUPPORT_H
#define BONGO_CAT_SHORTCUT_TEST_SUPPORT_H

#include "bongo_cat/app.h"

typedef struct ShortcutAction {
    char kind;
    int value;
    char argument[64];
} ShortcutAction;

typedef struct ShortcutTrace {
    ShortcutAction actions[4096];
    size_t count;
    int expression;
    bool preferences_visible;
    bool fail_effect, fail_motion, fail_expression;
} ShortcutTrace;

extern ShortcutTrace *shortcut_trace;
extern bool shortcut_record_actions;
void reference_app_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event);
void shortcut_fixture(BongoCatApp *app, size_t behaviors, size_t bindings);
void shortcut_binding(BongoCatApp *app, size_t slot, const char *id,
    const char *shortcut);
BongoCatInputEvent shortcut_event(BongoCatInputKind kind, const char *name,
    float value);

#endif
