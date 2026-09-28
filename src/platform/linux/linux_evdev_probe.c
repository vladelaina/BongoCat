#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "linux_evdev_probe.h"
#include <errno.h>
#include <limits.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVDEV_SYSFS_CLASS "/sys/class/input"

bool bongo_cat_evdev_node_valid(const char *node) {
    if (!node || strncmp(node, "event", 5) || !node[5] ||
        strlen(node) >= EVDEV_NODE_CAP) return false;
    for (const char *p = node + 5; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    return true;
}

bool bongo_cat_evdev_mask_bit(const char *text, unsigned word_bits, unsigned code) {
    if (!text || (word_bits != 32 && word_bits != 64)) return false;
    unsigned long long words[KEY_CNT / 32 + 1];
    size_t count = 0;
    while (*text) {
        if (*text == ' ' || *text == '\n' || *text == '\t') { ++text; continue; }
        if (count == sizeof(words) / sizeof(words[0]) ||
            !((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'f') ||
                (*text >= 'A' && *text <= 'F'))) return false;
        char *end = NULL;
        errno = 0;
        unsigned long long word = strtoull(text, &end, 16);
        if (errno || end == text || (*end && *end != ' ' && *end != '\n' &&
            *end != '\t') || (word_bits == 32 && word > UINT32_MAX)) return false;
        words[count++] = word;
        text = end;
    }
    unsigned block = code / word_bits, bit = code % word_bits;
    return block < count && ((words[count - block - 1] >> bit) & 1u);
}

static bool read_mask(const char *node, const char *name, char text[512]) {
    char path[512];
    int length = snprintf(path, sizeof(path), "%s/%s/device/capabilities/%s",
        EVDEV_SYSFS_CLASS, node, name);
    if (length <= 0 || (size_t)length >= sizeof(path)) return false;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = fgets(text, 512, file) != NULL && !ferror(file);
    if (ok && !strchr(text, '\n') && !feof(file)) ok = false;
    fclose(file);
    return ok;
}

bool bongo_cat_evdev_probe(const char *node, BongoCatEvdevCapabilities *capabilities) {
    if (!bongo_cat_evdev_node_valid(node) || !capabilities) return false;
    char keys[512], relative[512];
    if (!read_mask(node, "key", keys)) return false;
    unsigned bits = sizeof(unsigned long) * CHAR_BIT;
    bool left = bongo_cat_evdev_mask_bit(keys, bits, BTN_LEFT);
    *capabilities = (BongoCatEvdevCapabilities){
        .pointer = left,
        .keyboard = !left && bongo_cat_evdev_mask_bit(keys, bits, KEY_A) &&
            bongo_cat_evdev_mask_bit(keys, bits, KEY_Z) &&
            bongo_cat_evdev_mask_bit(keys, bits, KEY_ENTER) &&
            bongo_cat_evdev_mask_bit(keys, bits, KEY_SPACE),
        .relative = left && read_mask(node, "rel", relative) &&
            bongo_cat_evdev_mask_bit(relative, bits, REL_X) &&
            bongo_cat_evdev_mask_bit(relative, bits, REL_Y)
    };
    return capabilities->keyboard || capabilities->pointer;
}
