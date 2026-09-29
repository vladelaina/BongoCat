#ifndef BONGO_CAT_MVER_CONFIG_KEYS_H
#define BONGO_CAT_MVER_CONFIG_KEYS_H

#include "mver_config.h"
#include "../import/model_import.h"

/* Asset import also uses these Windows-key and gamepad name mappings. */
typedef struct BongoCatMverKeyNames {
    const char *items[2];
    char generated[16];
    size_t count;
} BongoCatMverKeyNames;

BongoCatMverKeyNames bongo_cat_mver_device_names(int code,
    size_t occurrence, size_t total);
BongoCatMverKeyNames bongo_cat_mver_gamepad_names(int code);
int bongo_cat_mver_modifier_index(int code);
bool bongo_cat_mver_chord(const BongoCatImportCandidate *candidate,
    void *row, char *output, size_t capacity);
bool bongo_cat_mver_keyboard_chord(void *row, char *output, size_t capacity);
bool bongo_cat_mver_sound_chord(void *row, char *output, size_t capacity);
bool bongo_cat_mver_shortcut_codes(const char *shortcut, char *output, size_t capacity);
#endif
