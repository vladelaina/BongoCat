#ifndef BONGO_CAT_LINUX_EVDEV_PROBE_H
#define BONGO_CAT_LINUX_EVDEV_PROBE_H

#include <stdbool.h>

#define EVDEV_NODE_CAP 32

typedef struct BongoCatEvdevCapabilities {
    bool keyboard;
    bool pointer;
    bool relative;
} BongoCatEvdevCapabilities;

bool bongo_cat_evdev_node_valid(const char *node);
bool bongo_cat_evdev_mask_bit(const char *text, unsigned word_bits, unsigned code);
bool bongo_cat_evdev_probe(const char *node, BongoCatEvdevCapabilities *capabilities);

#endif
