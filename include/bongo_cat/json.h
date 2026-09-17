#ifndef BONGO_CAT_JSON_H
#define BONGO_CAT_JSON_H

#include "bongo_cat/common.h"
#include <yyjson.h>

#ifdef __cplusplus
extern "C" {
#endif

yyjson_doc *bongo_cat_json_read_file(const char *path,
    yyjson_read_flag flags, yyjson_read_err *error);
/* Model compatibility is applied in memory; authored files are never rewritten. */
yyjson_doc *bongo_cat_model_json_parse(const char *data, size_t size,
    bool *normalized);
yyjson_doc *bongo_cat_model_json_read(const char *path, bool *normalized);
bool bongo_cat_json_write_file(const char *path,
    const yyjson_mut_doc *document, yyjson_write_flag flags,
    yyjson_write_err *error);

#ifdef __cplusplus
}
#endif

#endif
