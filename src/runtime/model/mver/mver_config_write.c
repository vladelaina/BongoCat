#include "mver_config_text.h"
#include "bongo_cat/file.h"

#include <SDL3/SDL.h>
#include <yyjson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool splice(char **text, size_t *length, size_t first, size_t last,
    const char *replacement) {
    size_t size = strlen(replacement);
    if (first > last || last > *length || size > MVER_CONFIG_MAX_BYTES ||
        *length - (last - first) > MVER_CONFIG_MAX_BYTES - size) return false;
    size_t total = *length - (last - first) + size;
    char *result = malloc(total + 1);
    if (!result) return false;
    memcpy(result, *text, first);
    memcpy(result + first, replacement, size);
    memcpy(result + first + size, *text + last, *length - last);
    result[total] = '\0';
    free(*text); *text = result; *length = total;
    return true;
}

/* Keep authored comments inside the changed row, including its display name. */
static char *row_with_comments(TextSpan old, const char *row) {
    size_t row_length = strlen(row), capacity = row_length +
        2 * (size_t)(old.end - old.begin) + 4;
    char *result = malloc(capacity);
    if (!result) return NULL;
    memcpy(result, row, row_length - 1);
    size_t used = row_length - 1;
    const char *cursor = old.begin;
    while (cursor < old.end) {
        if (*cursor == '"' || *cursor == '\'') cursor = mver_text_skip_string(cursor, old.end);
        else if (cursor + 1 < old.end && cursor[0] == '/' &&
            (cursor[1] == '/' || cursor[1] == '*')) {
            const char *begin = cursor;
            bool line = cursor[1] == '/';
            cursor += 2;
            if (line) {
                while (cursor < old.end && *cursor != '\r' && *cursor != '\n') cursor++;
            } else {
                while (cursor + 1 < old.end && !(cursor[0] == '*' && cursor[1] == '/')) cursor++;
                if (cursor + 1 < old.end) cursor += 2;
            }
            result[used++] = ' ';
            size_t count = (size_t)(cursor - begin);
            memcpy(result + used, begin, count); used += count;
            result[used++] = '\n';
        } else cursor++;
    }
    result[used++] = ']'; result[used] = '\0';
    return result;
}

static bool insert_member(char **text, size_t *length, TextSpan object,
    const char *name, const char *value) {
    /* Insert first so trailing commas and comments need no rewriting. */
    const char *begin = mver_text_skip_space(object.begin, object.end);
    if (begin >= object.end || *begin != '{') return false;
    const char *next = mver_text_skip_space(begin + 1, object.end);
    size_t capacity = strlen(name) + strlen(value) + 16;
    char *entry = malloc(capacity);
    if (!entry) return false;
    snprintf(entry, capacity, "\"%s\":%s%s", name, value,
        next < object.end && *next != '}' ? "," : "");
    size_t offset = (size_t)(begin + 1 - *text);
    bool ok = splice(text, length, offset, offset, entry);
    free(entry); return ok;
}

static bool save_text(const char *path, char *text, size_t length,
    bool ok, BongoCatError *error) {
    yyjson_doc *valid = ok ? yyjson_read(text, length,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE) : NULL;
    ok = ok && valid;
    yyjson_doc_free(valid);
    char temporary[BONGO_CAT_PATH_CAP] = {0};
    if (ok) {
        int n = snprintf(temporary, sizeof(temporary), "%s.mver-%llu.tmp", path,
            (unsigned long long)SDL_GetTicksNS());
        ok = n > 0 && (size_t)n < sizeof(temporary);
        FILE *file = ok ? bongo_cat_file_open(temporary, "wb") : NULL;
        ok = file && fwrite(text, 1, length, file) == length;
        if (file && fclose(file) != 0) ok = false;
        if (ok) ok = bongo_cat_file_replace(temporary, path, true);
        if (!ok && temporary[0]) bongo_cat_file_remove(temporary);
    }
    free(text);
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot save Mver configuration: %s", path);
    return ok;
}

bool bongo_cat_mver_config_write_row(const char *path, const char *mode,
    const char *field, int index, const char *row, BongoCatError *error) {
    yyjson_doc *replacement_doc = row ? yyjson_read(row, strlen(row), YYJSON_READ_JSON5) : NULL;
    bool replacement_valid = replacement_doc &&
        yyjson_is_arr(yyjson_doc_get_root(replacement_doc)) && row[0] == '[' &&
        row[strlen(row) - 1] == ']';
    yyjson_doc_free(replacement_doc);
    if (!path || !mode || !field || !replacement_valid) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Invalid Mver shortcut row");
        return false;
    }
    size_t length;
    char *text = mver_text_read(path, &length);
    bool ok = text != NULL;
    yyjson_doc *valid = ok ? yyjson_read(text, length,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE) : NULL;
    ok = valid && yyjson_is_obj(yyjson_doc_get_root(valid)) && index >= -1 &&
        index < BONGO_CAT_BEHAVIOR_LIMIT;
    yyjson_doc_free(valid);
    if (ok) {
        TextSpan root = mver_text_root(text, length);
        TextSpan section = {0}, rows = {0};
        if (!mver_text_member(root, mode, &section)) {
            ok = insert_member(&text, &length, root, mode, "{}");
            root = mver_text_root(text, length);
        }
        ok = ok && mver_text_member(root, mode, &section);
        if (ok && !mver_text_member(section, field, &rows)) {
            ok = insert_member(&text, &length, section, field, "[]");
            root = mver_text_root(text, length);
            ok = ok && mver_text_member(root, mode, &section);
        }
        ok = ok && mver_text_member(section, field, &rows);
        if (ok && rows.end - rows.begin == 4 && !memcmp(rows.begin, "null", 4)) {
            ok = splice(&text, &length, (size_t)(rows.begin - text),
                (size_t)(rows.end - text), "[]");
            root = mver_text_root(text, length);
            ok = ok && mver_text_member(root, mode, &section) && mver_text_member(section, field, &rows);
        }
        if (ok && index < 0) {
            char *replacement = row_with_comments(rows, row);
            ok = replacement && splice(&text, &length,
                (size_t)(rows.begin - text), (size_t)(rows.end - text), replacement);
            free(replacement);
        } else if (ok) {
            const char *cursor = mver_text_skip_space(rows.begin, rows.end);
            ok = cursor < rows.end && *cursor == '[';
            if (ok) cursor++;
            int current = 0;
            bool replaced = false, comma = false;
            while (ok && (cursor = mver_text_skip_space(cursor, rows.end)) < rows.end && *cursor != ']') {
                const char *after = mver_text_skip_value(cursor, rows.end);
                if (current == index) {
                    char *replacement = row_with_comments((TextSpan){cursor, after}, row);
                    ok = replacement && splice(&text, &length,
                        (size_t)(cursor - text), (size_t)(after - text), replacement);
                    free(replacement); replaced = true; break;
                }
                current++; cursor = mver_text_skip_space(after, rows.end);
                comma = cursor < rows.end && *cursor == ',';
                if (comma) cursor++;
            }
            if (ok && !replaced) {
                size_t capacity = (size_t)(index - current + 1) * 6 + strlen(row) + 4;
                char *tail = malloc(capacity);
                ok = tail != NULL && cursor < rows.end && *cursor == ']';
                if (ok) {
                    size_t used = (size_t)snprintf(tail, capacity, "%s", current && !comma ? "," : "");
                    for (; current < index; ++current) {
                        memcpy(tail + used, "[255],", 6); used += 6;
                    }
                    snprintf(tail + used, capacity - used, "%s", row);
                    size_t offset = (size_t)(cursor - text);
                    ok = splice(&text, &length, offset, offset, tail);
                }
                free(tail);
            }
        }
    }
    return save_text(path, text, length, ok, error);
}
