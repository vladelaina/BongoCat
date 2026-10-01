#ifndef BONGO_CAT_SHORTCUT_H
#define BONGO_CAT_SHORTCUT_H

#include "bongo_cat/input.h"

typedef struct BongoCatShortcutState {
    uint8_t control;
    uint8_t shift;
    uint8_t alt;
    uint8_t meta;
    char pressed[BONGO_CAT_ID_CAP];
} BongoCatShortcutState;

void bongo_cat_shortcut_init(BongoCatShortcutState *state);
/* Format UI labels without changing the stored binding. */
void bongo_cat_shortcut_format(const char *shortcut, char *output, size_t capacity);
bool bongo_cat_shortcut_update(BongoCatShortcutState *state, const BongoCatInputEvent *event);
bool bongo_cat_shortcut_matches(const BongoCatShortcutState *state,
    const BongoCatInputEvent *event, const char *shortcut);
bool bongo_cat_shortcut_release_matches(const BongoCatInputEvent *event,
    const char *shortcut);

#endif
