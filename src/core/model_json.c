#include "bongo_cat/json.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdlib.h>
#include <string.h>

static size_t skip_space(const char *data, size_t size, size_t pos) {
    while (pos < size && (data[pos] == ' ' || data[pos] == '\t' ||
        data[pos] == '\r' || data[pos] == '\n')) pos++;
    return pos;
}

yyjson_doc *bongo_cat_model_json_parse(const char *data, size_t size,
    bool *normalized) {
    if (normalized) *normalized = false;
    if (!data || !size || size > 4u * 1024u * 1024u) return NULL;
    yyjson_doc *document = yyjson_read(data, size, 0);
    if (document) return document;
    yyjson_read_flag flags = YYJSON_READ_ALLOW_COMMENTS |
        YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_BOM;
    yyjson_read_err error = {0};
    document = yyjson_read_opts((char *)data, size, flags, NULL, &error);
    if (!document && error.pos < size && data[error.pos] == ']') {
        size_t second = skip_space(data, size, error.pos + 1);
        size_t third = skip_space(data, size, second + 1);
        /* Some hand-edited Mver manifests have one extra '] }' pair. */
        if (second < size && third < size && data[second] == '}' && data[third] == '}') {
            char *copy = malloc(size);
            if (!copy) return NULL;
            memcpy(copy, data, size);
            copy[error.pos] = copy[second] = ' ';
            document = yyjson_read_opts(copy, size, flags, NULL, NULL);
            free(copy);
            yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
            if (!yyjson_is_obj(root) ||
                !yyjson_is_obj(yyjson_obj_get(root, "FileReferences"))) {
                yyjson_doc_free(document);
                document = NULL;
            }
        }
    }
    if (normalized) *normalized = document != NULL;
    return document;
}

yyjson_doc *bongo_cat_model_json_read(const char *path, bool *normalized) {
    if (normalized) *normalized = false;
    uint64_t size;
    if (!bongo_cat_path_file_size(path, &size) || !size ||
        size > 4u * 1024u * 1024u) return NULL;
    char *data = malloc((size_t)size);
    if (!data) return NULL;
    FILE *file = bongo_cat_file_open(path, "rb");
    bool loaded = file && fread(data, 1, (size_t)size, file) == (size_t)size;
    if (file && fclose(file) != 0) loaded = false;
    yyjson_doc *document = loaded
        ? bongo_cat_model_json_parse(data, (size_t)size, normalized) : NULL;
    free(data);
    return document;
}
