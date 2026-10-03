#ifndef BONGO_CAT_MVER_CONFIG_H
#define BONGO_CAT_MVER_CONFIG_H

#include "bongo_cat/config.h"

typedef struct BongoCatMverLabelEntry {
    char field[32];
    char label[BONGO_CAT_ID_CAP];
    size_t index;
} BongoCatMverLabelEntry;

typedef struct BongoCatMverLabels {
    BongoCatMverLabelEntry *entries;
    size_t count, capacity;
} BongoCatMverLabels;

/* Locate an authored config; no cache or alternate shortcut store. */
bool bongo_cat_import_mver_config_path(const char *root, char *path, size_t capacity);
bool bongo_cat_mver_config_find(const char *model_directory, char *path, size_t capacity);
bool bongo_cat_mver_gamepad_keyboard(const char *model_directory);
/* -1 when absent, invalid, or unavailable. */
int bongo_cat_mver_gamepad_input_mode(const char *model_directory);

const char *bongo_cat_mver_binding_mode(const char *mode, const char *field);
void bongo_cat_mver_labels_clear(BongoCatMverLabels *labels);
bool bongo_cat_mver_labels_load(const char *path, const char *mode,
    BongoCatMverLabels *labels);
const char *bongo_cat_mver_label(const BongoCatMverLabels *labels,
    const char *field, size_t index);
bool bongo_cat_mver_config_write_row(const char *path, const char *mode,
    const char *field, int index, const char *row, BongoCatError *error);
#endif
