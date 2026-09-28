#ifndef BONGO_CAT_LINUX_EVDEV_INTERNAL_H
#define BONGO_CAT_LINUX_EVDEV_INTERNAL_H

#include "linux_internal.h"
#include "linux_evdev_probe.h"
#include <SDL3/SDL.h>
#include <linux/input.h>

#define EVDEV_MAX_DEVICES 32
#define EVDEV_POLL_MS 250

typedef struct EvdevDevice {
    int fd;
    bool keyboard;
    bool pointer;
    bool relative;
    bool desynchronized;
    bool down[KEY_CNT];
    char node[EVDEV_NODE_CAP];
} EvdevDevice;

struct LinuxEvdevState {
    BongoCatPlatform *platform;
    SDL_Thread *thread;
    SDL_Mutex *motion_lock;
    atomic_bool running;
    atomic_bool pointer_active;
    int epoll_fd;
    EvdevDevice devices[EVDEV_MAX_DEVICES];
    size_t device_count;
    size_t reported_count;
    unsigned held[KEY_CNT];
    double relative_x;
    double relative_y;
};

const char *bongo_cat_evdev_key_name(unsigned code);
unsigned bongo_cat_evdev_key_index(unsigned code);
void bongo_cat_evdev_event(LinuxEvdevState *state, EvdevDevice *device,
    const struct input_event *event);
void bongo_cat_evdev_release(LinuxEvdevState *state, EvdevDevice *device);
bool bongo_cat_evdev_read(LinuxEvdevState *state, EvdevDevice *device);
bool bongo_cat_evdev_import(LinuxEvdevState *state, const char *descriptors);
void bongo_cat_evdev_discard(const char *descriptors);
void bongo_cat_evdev_remove(LinuxEvdevState *state, size_t index);
void bongo_cat_evdev_motion_reset(LinuxEvdevState *state);

#endif
