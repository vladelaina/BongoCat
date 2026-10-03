#ifndef BONGO_CAT_SOUND_SHORTCUT_H
#define BONGO_CAT_SOUND_SHORTCUT_H

#include "bongo_cat/input.h"

/* Stores physical keys separately so releasing one Ctrl does not release
   the other. Only changed input edges require reevaluating bindings. */
typedef struct BongoCatSoundShortcutState {
    char held[BONGO_CAT_INPUT_KEY_STATE_CAP][BONGO_CAT_ID_CAP];
    size_t count;
} BongoCatSoundShortcutState;

bool bongo_cat_sound_shortcut_update(BongoCatSoundShortcutState *state,
    const BongoCatInputEvent *event);
bool bongo_cat_sound_shortcut_down(const BongoCatSoundShortcutState *state,
    const char *shortcut);
/* Compare complete chords, ignoring token order and supported key aliases. */
bool bongo_cat_shortcut_equal(const char *left, const char *right);
/* The event must have just been added to state by update(). */
bool bongo_cat_sound_shortcut_pressed(const BongoCatSoundShortcutState *state,
    const BongoCatInputEvent *event, const char *shortcut);

#endif
