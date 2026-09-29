#ifndef BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H

#include "model_import_mver.h"
#include "../../mver/mver_config_keys.h"

bool bongo_cat_mver_emit_pair(const char *hand, const char *keyboard,
    const char *directory, BongoCatMverKeyNames names, BongoCatError *error);
bool bongo_cat_mver_add_audio(void *output, void *items, void *config, void *rows,
    const BongoCatImportCandidate *candidate, const BongoCatMverLabels *labels,
    const char *target);
bool bongo_cat_mver_effects(void *output, void *items, void *root, void *mode,
    const BongoCatImportCandidate *candidate, const char *target);
bool bongo_cat_mver_add_behaviors(void *output, void *items, void *config,
    const BongoCatImportCandidate *candidate, const BongoCatMverLabels *labels,
    BongoCatError *error);
#endif
