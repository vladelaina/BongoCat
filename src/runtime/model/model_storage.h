#ifndef BONGO_CAT_MODEL_STORAGE_H
#define BONGO_CAT_MODEL_STORAGE_H

#include "bongo_cat/common.h"

bool bongo_cat_model_remove_tree(const char *path, BongoCatError *error);
bool bongo_cat_model_cleanup_imports(const char *root, BongoCatError *error);
BongoCatResult bongo_cat_model_copy_directory(const char *source,
    const char *target, BongoCatError *error);
BongoCatResult bongo_cat_model_install_builtins(const char *asset_root,
    const char *models_root, bool first_run, BongoCatError *error);

#endif
