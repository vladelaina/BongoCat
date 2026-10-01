#ifndef BONGO_CAT_MODEL_IMPORT_MVER_MANIFEST_H
#define BONGO_CAT_MODEL_IMPORT_MVER_MANIFEST_H

#include "bongo_cat/common.h"
#include <yyjson.h>

/* Compatibility is applied when reading, without modifying model files. */
yyjson_doc *bongo_cat_import_mver_manifest_read(const char *path,
    bool *repaired);
bool bongo_cat_import_mver_manifest_valid(const char *root,
    const char *setting);

#endif
