#include "windows_input_internal.h"

#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void publish(WindowsInputState *state, WindowsRawHeld *held, bool mouse) {
    bool down = held->references != 0;
    if (down == held->emitted) return;
    BongoCatInputKind kind = mouse ? (down ? BONGO_CAT_INPUT_MOUSE_DOWN :
        BONGO_CAT_INPUT_MOUSE_UP) : (down ? BONGO_CAT_INPUT_KEY_DOWN :
        BONGO_CAT_INPUT_KEY_UP);
    if (!bongo_cat_windows_input_push_event(state, kind, held->name,
        down ? 1.0f : 0.0f)) {
        state->retry_events = true;
        return;
    }
    held->emitted = down;
}

void bongo_cat_windows_input_flush(WindowsInputState *state) {
    if (!state->retry_events) return;
    state->retry_events = false;
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_RAW_HELD_LIMIT; ++i)
        publish(state, &state->keys[i], false);
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT; ++i)
        publish(state, &state->buttons[i], true);
}

WindowsRawDevice *bongo_cat_windows_input_device(WindowsInputState *state,
    HANDLE handle) {
    for (WindowsRawDevice *device = state->devices; device; device = device->next)
        if (device->handle == handle) return device;
    if (state->device_count >= BONGO_CAT_WINDOWS_RAW_DEVICE_LIMIT) return NULL;
    WindowsRawDevice *device = calloc(1, sizeof(*device));
    if (!device) return NULL;
    device->handle = handle;
    device->next = state->devices;
    state->devices = device;
    state->device_count++;
    return device;
}

static int held_slot(WindowsInputState *state, const char *name) {
    int available = -1;
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_RAW_HELD_LIMIT; ++i) {
        WindowsRawHeld *held = &state->keys[i];
        if (held->references || held->emitted) {
            if (strcmp(held->name, name) == 0) return (int)i;
        } else if (available < 0) available = (int)i;
    }
    if (available >= 0) snprintf(state->keys[available].name,
        sizeof(state->keys[available].name), "%s", name);
    return available;
}

void bongo_cat_windows_input_key(WindowsInputState *state,
    WindowsRawDevice *device, const RAWKEYBOARD *key) {
    if ((key->Flags & RI_KEY_E0) &&
        (key->MakeCode == 0x2a || key->MakeCode == 0x36)) {
        state->diagnostic_filtered_keys++;
        return;
    }
    if ((key->Flags & RI_KEY_E1) && key->MakeCode == 0x1d) {
        state->diagnostic_filtered_keys++;
        device->pending_e1 = true;
        return;
    }
    RAWKEYBOARD normalized = *key;
    if (device->pending_e1) {
        device->pending_e1 = false;
        if (key->MakeCode == 0x45) normalized.VKey = VK_PAUSE;
    }
    key = &normalized;
    char buffer[16];
    const char *name = bongo_cat_windows_key_name(key, buffer);
    if (!name) { state->diagnostic_filtered_keys++; return; }
    unsigned index = bongo_cat_windows_key_index(key);
    if (index >= BONGO_CAT_WINDOWS_RAW_KEY_COUNT) {
        state->diagnostic_filtered_keys++;
        return;
    }
    unsigned slot = device->keys[index];
    bool down = !(key->Flags & RI_KEY_BREAK);
    if (down == (slot != 0)) { state->diagnostic_duplicate_keys++; return; }
    if (down) {
        int found = held_slot(state, name);
        if (found < 0) { state->diagnostic_filtered_keys++; return; }
        slot = (unsigned)found + 1;
        device->keys[index] = (unsigned short)slot;
        state->keys[slot - 1].references++;
    } else {
        device->keys[index] = 0;
        state->keys[slot - 1].references--;
    }
    publish(state, &state->keys[slot - 1], false);
    /* Pause has no hardware break packet on standard keyboards. */
    if (down && key->VKey == VK_PAUSE) {
        device->keys[index] = 0;
        state->keys[slot - 1].references--;
        publish(state, &state->keys[slot - 1], false);
    }
}

void bongo_cat_windows_input_buttons(WindowsInputState *state,
    WindowsRawDevice *device, USHORT flags) {
    if (!flags) return;
    static const USHORT presses[] = {RI_MOUSE_LEFT_BUTTON_DOWN,
        RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_DOWN,
        RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
    static const USHORT releases[] = {RI_MOUSE_LEFT_BUTTON_UP,
        RI_MOUSE_RIGHT_BUTTON_UP, RI_MOUSE_MIDDLE_BUTTON_UP,
        RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};
    static const char *names[] = {"Left", "Right", "Middle", "Back", "Forward"};
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT; ++i) {
        WindowsRawHeld *held = &state->buttons[i];
        snprintf(held->name, sizeof(held->name), "%s", names[i]);
        if ((flags & presses[i]) && !device->buttons[i]) {
            device->buttons[i] = true;
            held->references++;
            publish(state, held, true);
        }
        if ((flags & releases[i]) && device->buttons[i]) {
            device->buttons[i] = false;
            held->references--;
            publish(state, held, true);
        }
    }
}

void bongo_cat_windows_input_remove_device(WindowsInputState *state, HANDLE handle) {
    WindowsRawDevice **link = &state->devices;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    if (!*link) return;
    WindowsRawDevice *device = *link;
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_RAW_KEY_COUNT; ++i)
        if (device->keys[i]) state->keys[device->keys[i] - 1].references--;
    for (unsigned i = 0; i < BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT; ++i)
        if (device->buttons[i]) state->buttons[i].references--;
    *link = device->next;
    free(device);
    state->device_count--;
    state->retry_events = true;
    bongo_cat_windows_input_flush(state);
}

void bongo_cat_windows_input_clear_devices(WindowsInputState *state) {
    while (state->devices)
        bongo_cat_windows_input_remove_device(state, state->devices->handle);
    bongo_cat_windows_input_clear_motion(state);
}
#endif
