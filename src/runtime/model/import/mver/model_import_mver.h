#ifndef BONGO_CAT_MODEL_IMPORT_MVER_H
#define BONGO_CAT_MODEL_IMPORT_MVER_H

#include "../model_import.h"
#include "../../mver/mver_config.h"

/* Canonical package discovery and runtime-adapter generation. */
int bongo_cat_import_mver_discover(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_patch_discover(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_patch_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
bool bongo_cat_import_mver_assets(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error);

#endif
