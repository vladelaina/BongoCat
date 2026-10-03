#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "linux_evdev_internal.h"
#include "bongo_cat/log.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>

#define EVDEV_DEVICE_DIRECTORY "/dev/input"
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
    char path[BONGO_CAT_PATH_CAP];
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

static bool classify_device(const char *node, EvdevDevice *device) {
    char keys[512], relative[512];
    if (!read_mask(node, "key", keys)) return false;
    /* Sysfs uses the native kernel unsigned-long bitmap representation. */
    unsigned bits = sizeof(unsigned long) * CHAR_BIT;
    bool left = bongo_cat_evdev_mask_bit(keys, bits, BTN_LEFT);
    device->pointer = left;
    device->keyboard = !left && bongo_cat_evdev_mask_bit(keys, bits, KEY_A) &&
        bongo_cat_evdev_mask_bit(keys, bits, KEY_Z) &&
        bongo_cat_evdev_mask_bit(keys, bits, KEY_ENTER) &&
        bongo_cat_evdev_mask_bit(keys, bits, KEY_SPACE);
    device->relative = left && read_mask(node, "rel", relative) &&
        bongo_cat_evdev_mask_bit(relative, bits, REL_X) &&
        bongo_cat_evdev_mask_bit(relative, bits, REL_Y);
    return device->keyboard || device->pointer;
}

static void report_devices(LinuxEvdevState *state) {
    bool relative = false;
    for (size_t i = 0; i < state->device_count; ++i)
        relative |= state->devices[i].relative;
    atomic_store(&state->pointer_active, relative);
    if (!relative) bongo_cat_evdev_motion_reset(state);
    if (state->reported_count == state->device_count) return;
    state->reported_count = state->device_count;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[runtime] evdev listener: %zu readable input devices", state->device_count);
}

void bongo_cat_evdev_remove(LinuxEvdevState *state, size_t index) {
    if (index >= state->device_count) return;
    EvdevDevice *device = &state->devices[index];
    bongo_cat_evdev_release(state, device);
    epoll_ctl(state->epoll_fd, EPOLL_CTL_DEL, device->fd, NULL);
    close(device->fd);
    --state->device_count;
    if (index != state->device_count) *device = state->devices[state->device_count];
    memset(&state->devices[state->device_count], 0, sizeof(*device));
    report_devices(state);
}

void bongo_cat_evdev_scan(LinuxEvdevState *state) {
    DIR *directory = opendir(EVDEV_DEVICE_DIRECTORY);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) && state->device_count < EVDEV_MAX_DEVICES) {
        const char *node = entry->d_name;
        if (!bongo_cat_evdev_node_valid(node)) continue;
        size_t i = 0;
        for (; i < state->device_count; ++i)
            if (!strcmp(state->devices[i].node, node)) break;
        if (i < state->device_count) continue;
        EvdevDevice device = {0};
        if (!classify_device(node, &device)) continue;
        int fd = openat(dirfd(directory), node,
            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if ((errno == EACCES || errno == EPERM) && !state->denied_reported) {
                state->denied_reported = true;
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "evdev input permission denied; see SECURITY.md. "
                    "BongoCat does not change device permissions.");
            }
            continue;
        }
        struct stat info;
        if (fstat(fd, &info) || !S_ISCHR(info.st_mode)) { close(fd); continue; }
        struct epoll_event interest = {.events = EPOLLIN};
        interest.data.fd = fd;
        if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, fd, &interest)) {
            close(fd);
            continue;
        }
        device.fd = fd;
        memcpy(device.node, node, strlen(node) + 1);
        state->devices[state->device_count++] = device;
    }
    closedir(directory);
    report_devices(state);
}
