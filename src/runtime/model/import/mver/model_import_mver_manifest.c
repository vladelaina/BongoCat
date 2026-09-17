#include "model_import_mver_manifest.h"
#include "../model_import_manifest.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

yyjson_doc *bongo_cat_import_mver_manifest_read(const char *path,
    bool *repaired) {
    return bongo_cat_model_json_read(path, repaired);
}

bool bongo_cat_import_mver_manifest_valid(const char *root,
    const char *setting) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), root, setting)) return false;
    yyjson_doc *document = bongo_cat_import_mver_manifest_read(path, NULL);
    bool valid = bongo_cat_import_manifest_document_valid(root, document, true);
    yyjson_doc_free(document);
    return valid;
}
