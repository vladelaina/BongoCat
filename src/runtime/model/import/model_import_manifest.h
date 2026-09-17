#ifndef BONGO_CAT_MODEL_IMPORT_MANIFEST_H
#define BONGO_CAT_MODEL_IMPORT_MANIFEST_H

#include "bongo_cat/common.h"
#include <yyjson.h>

bool bongo_cat_import_manifest_document_valid(const char *root,
    yyjson_doc *document, bool allow_missing_optional);

/* Validates a Live2D manifest and every referenced local asset. */
bool bongo_cat_import_manifest_valid(const char *root, const char *setting,
    BongoCatError *error);

#endif
