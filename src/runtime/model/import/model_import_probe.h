#ifndef BONGO_CAT_MODEL_IMPORT_PROBE_H
#define BONGO_CAT_MODEL_IMPORT_PROBE_H

#include "model_import.h"

typedef enum BongoCatImportProbePolicy {
    /* A malformed matching package is a user-visible import error. */
    BONGO_CAT_IMPORT_PROBE_STRICT,
    /* Container scans may continue trying later source adapters. */
    BONGO_CAT_IMPORT_PROBE_FALLBACK
} BongoCatImportProbePolicy;

/* Exact package-root dispatch shared by progressive, bounded, and queued
   scans. Broad Mver parent lookup remains in the Mver adapter. */
const char *bongo_cat_import_format_name(BongoCatImportFormat format);
int bongo_cat_import_probe_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatImportFormat *format,
    BongoCatImportProbePolicy policy, bool diagnostic, BongoCatError *error);

/* Resolve an existing Moc file to its owning package, checking ancestors
   only. Mver ownership takes precedence over standalone Tauri conversion;
   malformed matching packages stop lookup. Source validates the input file. */
BongoCatResult bongo_cat_import_probe_live2d_owner(const char *source,
    char *directory, size_t capacity, BongoCatError *error);

#endif
