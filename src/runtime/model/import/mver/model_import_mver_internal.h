#ifndef BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H

#include "model_import_mver.h"

typedef struct BongoCatMverKeyNames {
    const char *items[2];
    char generated[16];
    size_t count;
} BongoCatMverKeyNames;

typedef struct BongoCatMverLabelEntry {
    char field[32];
    char label[BONGO_CAT_ID_CAP];
    size_t index;
} BongoCatMverLabelEntry;

typedef struct BongoCatMverLabels {
    BongoCatMverLabelEntry entries[BONGO_CAT_BEHAVIOR_CAP];
    size_t count;
} BongoCatMverLabels;

bool bongo_cat_mver_emit_pair(const char *hand, const char *keyboard,
    const char *directory, BongoCatMverKeyNames names, BongoCatError *error);
BongoCatMverKeyNames bongo_cat_mver_device_names(int code,
    size_t occurrence, size_t total);
BongoCatMverKeyNames bongo_cat_mver_gamepad_names(int code);
int bongo_cat_mver_modifier_index(int code);
bool bongo_cat_mver_chord(const BongoCatImportCandidate *candidate,
    void *row, char *output, size_t capacity);
bool bongo_cat_mver_sound_chord(void *row, char *output, size_t capacity);
bool bongo_cat_mver_add_audio(void *output, void *items, void *config, void *rows,
    const BongoCatImportCandidate *candidate, const BongoCatMverLabels *labels,
    const char *target);
bool bongo_cat_mver_effects(void *output, void *items, void *root, void *mode,
    const BongoCatImportCandidate *candidate, const char *target);
bool bongo_cat_mver_add_behaviors(void *output, void *items, void *mode,
    const BongoCatImportCandidate *candidate, const BongoCatMverLabels *labels,
    BongoCatError *error);
bool bongo_cat_mver_labels_load(const char *path, const char *mode,
    BongoCatMverLabels *labels);
const char *bongo_cat_mver_label(const BongoCatMverLabels *labels,
    const char *field, size_t index);
#endif
