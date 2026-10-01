#ifndef BONGO_CAT_MODEL_IMPORT_NEARBY_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_NEARBY_INTERNAL_H

#include "model_import.h"

#define BONGO_CAT_NEARBY_CACHE_MARKER ".bongo-cat-cache.json"
#define BONGO_CAT_NEARBY_CACHE_SCHEMA 7
#define BONGO_CAT_ADAPTER_CACHE_DIRECTORY "model-adapters"

bool bongo_cat_nearby_signature(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
bool bongo_cat_nearby_cached_inspection(const char *target,
    const char *source, const char *signature, char identity[65],
    bool *placeholder);
void bongo_cat_nearby_remember_inspection(const char *target,
    const char *source, const char *signature, const char *identity,
    bool placeholder, BongoCatModelMode mode);
bool bongo_cat_nearby_refresh_cache(
    const BongoCatImportCandidate *candidate, const char *cache_root,
    const char *id, const char *source, const char *signature,
    char identity[65], bool placeholder,
    char adapter[BONGO_CAT_PATH_CAP], bool *created, BongoCatError *error);

#endif
