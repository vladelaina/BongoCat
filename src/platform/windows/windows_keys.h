#ifndef BONGO_CAT_WINDOWS_KEYS_H
#define BONGO_CAT_WINDOWS_KEYS_H

#ifdef _WIN32
#include <stdbool.h>
#include <windows.h>

#define BONGO_CAT_WINDOWS_RAW_KEY_COUNT 1024

const char *bongo_cat_windows_key_name(const RAWKEYBOARD *key, char output[16]);
unsigned bongo_cat_windows_key_index(const RAWKEYBOARD *key);
bool bongo_cat_windows_right_button_down(void);
#endif
#endif
