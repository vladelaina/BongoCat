#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "linux_evdev_internal.h"
#include "bongo_cat/log.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/major.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define EVDEV_DEVICE_DIRECTORY "/dev/input"

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

static bool import_device(LinuxEvdevState *state, int fd, const char *node) {
    if (state->device_count >= EVDEV_MAX_DEVICES || fd < 3 ||
        !bongo_cat_evdev_node_valid(node)) return false;
    int descriptor_flags = fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) return false;
    struct stat descriptor_info, node_info;
    char path[BONGO_CAT_PATH_CAP];
    int length = snprintf(path, sizeof(path), "%s/%s", EVDEV_DEVICE_DIRECTORY, node);
    if (length <= 0 || (size_t)length >= sizeof(path) || fstat(fd, &descriptor_info) ||
        lstat(path, &node_info) || !S_ISCHR(descriptor_info.st_mode) ||
        !S_ISCHR(node_info.st_mode) || descriptor_info.st_rdev != node_info.st_rdev ||
        major(descriptor_info.st_rdev) != INPUT_MAJOR) return false;
    int status = fcntl(fd, F_GETFL);
    if (status < 0 || (status & O_ACCMODE) != O_RDONLY || !(status & O_NONBLOCK))
        return false;
    BongoCatEvdevCapabilities capabilities;
    if (!bongo_cat_evdev_probe(node, &capabilities)) return false;
    EvdevDevice device = {.fd = fd, .keyboard = capabilities.keyboard,
        .pointer = capabilities.pointer, .relative = capabilities.relative};
    snprintf(device.node, sizeof(device.node), "%s", node);
    struct epoll_event interest = {.events = EPOLLIN, .data.fd = fd};
    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, fd, &interest)) return false;
    state->devices[state->device_count++] = device;
    return true;
}

bool bongo_cat_evdev_import(LinuxEvdevState *state, const char *descriptors) {
    if (!state || !descriptors || !descriptors[0]) return false;
    const char *cursor = descriptors;
    while (*cursor && state->device_count < EVDEV_MAX_DEVICES) {
        errno = 0;
        char *separator = NULL;
        long fd = strtol(cursor, &separator, 10);
        if (errno || separator == cursor || fd < 3 || fd > INT_MAX || *separator != ':')
            break;
        const char *node = separator + 1;
        const char *comma = strchr(node, ',');
        size_t node_length = comma ? (size_t)(comma - node) : strlen(node);
        char copied[EVDEV_NODE_CAP];
        if (!node_length || node_length >= sizeof(copied)) break;
        memcpy(copied, node, node_length);
        copied[node_length] = '\0';
        if (!import_device(state, (int)fd, copied)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Rejected inherited evdev descriptor %ld", fd);
            close((int)fd);
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    report_devices(state);
    return state->device_count > 0;
}

void bongo_cat_evdev_discard(const char *descriptors) {
    const char *cursor = descriptors;
    while (cursor && *cursor) {
        errno = 0;
        char *separator = NULL;
        long fd = strtol(cursor, &separator, 10);
        if (errno || separator == cursor || fd < 3 || fd > INT_MAX || *separator != ':')
            break;
        close((int)fd);
        const char *comma = strchr(separator + 1, ',');
        if (!comma) break;
        cursor = comma + 1;
    }
}
