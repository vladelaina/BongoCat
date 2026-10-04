#include "mver_config_text.h"
#include "bongo_cat/utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool line_label(TextSpan row, char *output, size_t capacity) {
    const char *cursor = row.begin;
    while (cursor + 1 < row.end) {
        if (*cursor == '"' || *cursor == '\'') cursor = mver_text_skip_string(cursor, row.end);
        else if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < row.end && !(cursor[0] == '*' && cursor[1] == '/'))
                cursor++;
            if (cursor + 1 < row.end) cursor += 2;
        } else if (cursor[0] == '/' && cursor[1] == '/') {
            const char *begin = cursor + 2, *end = begin;
            while (end < row.end && *end != '\n' && *end != '\r') end++;
            while (begin < end && isspace((unsigned char)*begin)) begin++;
            while (end > begin && isspace((unsigned char)end[-1])) end--;
            size_t length = (size_t)(end - begin);
            if (!length) return false;
            if (length >= capacity) {
                length = capacity - 1;
                while (length &&
                    ((unsigned char)begin[length] & 0xc0) == 0x80) length--;
            }
            memcpy(output, begin, length); output[length] = '\0'; return true;
        } else cursor++;
    }
    return false;
}

static bool collect_field(BongoCatMverLabels *labels, TextSpan mode,
    const char *field) {
    TextSpan rows;
    if (!mver_text_member(mode, field, &rows)) return true;
    const char *cursor = mver_text_skip_space(rows.begin, rows.end);
    if (cursor >= rows.end || *cursor++ != '[') return true;
    size_t index = 0;
    while ((cursor = mver_text_skip_space(cursor, rows.end)) < rows.end && *cursor != ']') {
        const char *after = mver_text_skip_value(cursor, rows.end);
        TextSpan row = {cursor, after};
        char label[BONGO_CAT_ID_CAP], normalized[BONGO_CAT_ID_CAP];
        if (line_label(row, label, sizeof(label)) &&
            bongo_cat_utf8_normalize_mver(label, normalized,
                sizeof(normalized))) {
            if (labels->count == BONGO_CAT_BEHAVIOR_LIMIT) return false;
            if (labels->count == labels->capacity) {
                size_t capacity = labels->capacity ? labels->capacity * 2 : 16;
                if (capacity > BONGO_CAT_BEHAVIOR_LIMIT) capacity = BONGO_CAT_BEHAVIOR_LIMIT;
                BongoCatMverLabelEntry *entries = realloc(labels->entries, capacity * sizeof(*entries));
                if (!entries) return false;
                labels->entries = entries; labels->capacity = capacity;
            }
            BongoCatMverLabelEntry *entry = &labels->entries[labels->count++];
            snprintf(entry->field, sizeof(entry->field), "%s", field);
            snprintf(entry->label, sizeof(entry->label), "%s", normalized);
            entry->index = index;
        }
        index++; cursor = mver_text_skip_space(after, rows.end);
        if (cursor < rows.end && *cursor == ',') cursor++;
    }
    return true;
}

void bongo_cat_mver_labels_clear(BongoCatMverLabels *labels) {
    if (!labels) return;
    free(labels->entries);
    *labels = (BongoCatMverLabels){0};
}

bool bongo_cat_mver_labels_load(const char *path, const char *mode,
    BongoCatMverLabels *labels) {
    if (!path || !mode || !labels) return false;
    bongo_cat_mver_labels_clear(labels);
    size_t length;
    char *text = mver_text_read(path, &length);
    if (!text) return false;
    TextSpan root = mver_text_root(text, length), selected;
    bool found = mver_text_member(root, mode, &selected);
    if (found) {
        static const char *fields[] = {
            "l2d_expression", "l2d_motion", "l2d_motion_lockhand", "sounds"
        };
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
            TextSpan source;
            if (mver_text_member(root, bongo_cat_mver_binding_mode(mode, fields[i]), &source) &&
                !collect_field(labels, source, fields[i])) {
                found = false; break;
            }
        }
    }
    free(text);
    if (!found) bongo_cat_mver_labels_clear(labels);
    return found;
}

const char *bongo_cat_mver_label(const BongoCatMverLabels *labels,
    const char *field, size_t index) {
    if (!labels || !field) return NULL;
    for (size_t i = 0; i < labels->count; ++i)
        if (labels->entries[i].index == index &&
            strcmp(labels->entries[i].field, field) == 0)
            return labels->entries[i].label;
    return NULL;
}

