#ifndef BONGO_CAT_MVER_CONFIG_TEXT_H
#define BONGO_CAT_MVER_CONFIG_TEXT_H

#include "mver_config.h"

typedef struct TextSpan { const char *begin, *end; } TextSpan;
#define MVER_CONFIG_MAX_BYTES (16 * 1024 * 1024)
const char *mver_text_skip_string(const char *cursor, const char *end);
const char *mver_text_skip_space(const char *cursor, const char *end);
const char *mver_text_skip_value(const char *cursor, const char *end);
bool mver_text_member(TextSpan object, const char *name, TextSpan *value);
char *mver_text_read(const char *path, size_t *length);
TextSpan mver_text_root(const char *text, size_t length);
#endif
