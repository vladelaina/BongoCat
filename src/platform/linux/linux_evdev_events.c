#include "linux_evdev_internal.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static void wake_consumer(LinuxEvdevState *state) {
    SDL_Event wake = {0};
    wake.type = state->platform->wake_event_type;
    SDL_PushEvent(&wake);
}

static const char *button_name(unsigned code) {
    switch (code) {
    case BTN_LEFT: return "Left";
    case BTN_MIDDLE: return "Middle";
    case BTN_RIGHT: return "Right";
    case BTN_SIDE: return "Back";
    case BTN_EXTRA: return "Forward";
    default: return NULL;
    }
}

static void key_event(LinuxEvdevState *state, EvdevDevice *device,
    unsigned code, bool down) {
    if (code >= KEY_CNT || device->down[code] == down) return;
    bool mouse = code >= BTN_MISC;
    const char *name = mouse ? button_name(code) : bongo_cat_evdev_key_name(code);
    if (!name || (mouse ? !device->pointer : !device->keyboard)) return;
    unsigned index = bongo_cat_evdev_key_index(code);
    device->down[code] = down;
    if (down) {
        if (state->held[index]++ > 0) return;
    } else {
        if (!state->held[index] || --state->held[index] > 0) return;
    }
    BongoCatInputEvent input = {
        .kind = mouse ? (down ? BONGO_CAT_INPUT_MOUSE_DOWN : BONGO_CAT_INPUT_MOUSE_UP) :
            (down ? BONGO_CAT_INPUT_KEY_DOWN : BONGO_CAT_INPUT_KEY_UP),
        .timestamp_ms = SDL_GetTicks(), .value = down ? 1.0f : 0.0f
    };
    snprintf(input.name, sizeof(input.name), "%s", name);
    if (bongo_cat_input_push(state->platform->input, &input)) wake_consumer(state);
}

void bongo_cat_evdev_release(LinuxEvdevState *state, EvdevDevice *device) {
    for (unsigned code = 0; code < KEY_CNT; ++code)
        if (device->down[code]) key_event(state, device, code, false);
}

void bongo_cat_evdev_motion_reset(LinuxEvdevState *state) {
    SDL_LockMutex(state->motion_lock);
    state->relative_x = state->relative_y = 0;
    SDL_UnlockMutex(state->motion_lock);
}

void bongo_cat_evdev_event(LinuxEvdevState *state, EvdevDevice *device,
    const struct input_event *event) {
    if (event->type == EV_SYN && event->code == SYN_DROPPED) {
        bongo_cat_evdev_release(state, device);
        bongo_cat_evdev_motion_reset(state);
        device->desynchronized = true;
        return;
    }
    /* No state-query ioctl is permitted. After overflow, release known keys
       and ignore the incomplete packet; held keys resume on their next press. */
    if (device->desynchronized) {
        if (event->type == EV_SYN && event->code == SYN_REPORT)
            device->desynchronized = false;
        return;
    }
    if (event->type == EV_KEY && (event->value == 0 || event->value == 1)) {
        key_event(state, device, event->code, event->value == 1);
    } else if (event->type == EV_REL && device->relative &&
        (event->code == REL_X || event->code == REL_Y) && event->value) {
        SDL_LockMutex(state->motion_lock);
        bool wake = state->relative_x == 0 && state->relative_y == 0;
        double *axis = event->code == REL_X ? &state->relative_x : &state->relative_y;
        *axis = SDL_clamp(*axis + (double)event->value, -32768.0, 32768.0);
        SDL_UnlockMutex(state->motion_lock);
        if (wake) wake_consumer(state);
    }
}

bool bongo_cat_evdev_read(LinuxEvdevState *state, EvdevDevice *device) {
    struct input_event events[64];
    ssize_t bytes = read(device->fd, events, sizeof(events));
    if (bytes < 0) return errno == EAGAIN || errno == EINTR;
    if (!bytes || (size_t)bytes % sizeof(events[0])) return false;
    size_t count = (size_t)bytes / sizeof(events[0]);
    for (size_t i = 0; i < count; ++i)
        bongo_cat_evdev_event(state, device, &events[i]);
    return true;
}
