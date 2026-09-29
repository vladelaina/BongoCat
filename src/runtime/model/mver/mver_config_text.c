#include "mver_config_text.h"
#include "bongo_cat/file.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char *mver_text_skip_string(const char *cursor, const char *end) {
    char quote = *cursor++;
    while (cursor < end) {
        if (*cursor == '\\' && cursor + 1 < end) cursor += 2;
        else if (*cursor++ == quote) break;
    }
    return cursor;
}

const char *mver_text_skip_space(const char *cursor, const char *end) {
    for (;;) {
        while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
        if (cursor + 1 >= end || cursor[0] != '/') return cursor;
        if (cursor[1] == '/') {
            cursor += 2;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
        } else if (cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < end && !(cursor[0] == '*' && cursor[1] == '/'))
                cursor++;
            if (cursor + 1 < end) cursor += 2;
        } else return cursor;
    }
}

const char *mver_text_skip_value(const char *cursor, const char *end) {
    cursor = mver_text_skip_space(cursor, end);
    if (cursor >= end) return cursor;
    if (*cursor == '"' || *cursor == '\'') return mver_text_skip_string(cursor, end);
    if (*cursor != '{' && *cursor != '[') {
        while (cursor < end && *cursor != ',' && *cursor != '}' && *cursor != ']' &&
            !isspace((unsigned char)*cursor) && *cursor != '/')
            cursor++;
        return cursor;
    }
    char stack[64]; size_t depth = 0;
    stack[depth++] = *cursor++ == '{' ? '}' : ']';
    while (cursor < end && depth) {
        if (*cursor == '"' || *cursor == '\'') cursor = mver_text_skip_string(cursor, end);
        else if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
        } else if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < end && !(cursor[0] == '*' && cursor[1] == '/'))
                cursor++;
            if (cursor + 1 < end) cursor += 2;
        } else if (*cursor == '{' || *cursor == '[') {
            if (depth >= sizeof(stack)) return end;
            stack[depth++] = *cursor++ == '{' ? '}' : ']';
        } else if (*cursor == stack[depth - 1]) { depth--; cursor++; }
        else cursor++;
    }
    return cursor;
}

static bool key_token(const char **cursor, const char *end, TextSpan *key) {
    const char *start = mver_text_skip_space(*cursor, end);
    if (start >= end) return false;
    if (*start == '"' || *start == '\'') {
        const char *after = mver_text_skip_string(start, end);
        if (after <= start + 1 || after[-1] != *start) return false;
        key->begin = start + 1; key->end = after - 1; *cursor = after; return true;
    }
    const char *after = start;
    while (after < end && (isalnum((unsigned char)*after) || *after == '_' ||
        *after == '-')) after++;
    if (after == start) return false;
    key->begin = start; key->end = after; *cursor = after; return true;
}

static bool key_equals(TextSpan key, const char *name) {
    size_t length = (size_t)(key.end - key.begin);
    return strlen(name) == length && memcmp(key.begin, name, length) == 0;
}

bool mver_text_member(TextSpan object, const char *name, TextSpan *value) {
    const char *cursor = mver_text_skip_space(object.begin, object.end);
    if (cursor >= object.end || *cursor++ != '{') return false;
    while ((cursor = mver_text_skip_space(cursor, object.end)) < object.end && *cursor != '}') {
        TextSpan key;
        if (!key_token(&cursor, object.end, &key)) return false;
        cursor = mver_text_skip_space(cursor, object.end);
        if (cursor >= object.end || *cursor++ != ':') return false;
        const char *begin = mver_text_skip_space(cursor, object.end);
        const char *after = mver_text_skip_value(begin, object.end);
        if (key_equals(key, name)) {
            value->begin = begin; value->end = after; return true;
        }
        cursor = mver_text_skip_space(after, object.end);
        if (cursor < object.end && *cursor == ',') cursor++;
    }
    return false;
}


char *mver_text_read(const char *path, size_t *length) {
    *length = 0;
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    char *text = NULL;
    if (size >= 0 && size <= MVER_CONFIG_MAX_BYTES && fseek(file, 0, SEEK_SET) == 0) {
        text = malloc((size_t)size + 1);
        if (text && fread(text, 1, (size_t)size, file) != (size_t)size) {
            free(text); text = NULL;
        }
    }
    fclose(file);
    if (text) { *length = (size_t)size; text[*length] = '\0'; }
    return text;
}

TextSpan mver_text_root(const char *text, size_t length) {
    size_t bom = length >= 3 && (unsigned char)text[0] == 0xef &&
        (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf ? 3 : 0;
    return (TextSpan){text + bom, text + length};
}
