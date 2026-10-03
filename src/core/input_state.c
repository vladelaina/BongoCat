#include "bongo_cat/input.h"

#include <stdio.h>
#include <string.h>

void bongo_cat_input_init(BongoCatInputState *state) {
    if (!state) return;
    atomic_init(&state->queue_busy, false);
    memset(state->queue, 0, sizeof(state->queue));
    atomic_init(&state->head, 0);
    atomic_init(&state->tail, 0);
    atomic_init(&state->mouse_x, 0.0);
    atomic_init(&state->mouse_y, 0.0);
    atomic_init(&state->mouse_dirty, false);
    atomic_init(&state->control, 0);
    atomic_init(&state->shift, 0);
    atomic_init(&state->dropped, 0);
    atomic_init(&state->next_sequence, 0);
    memset(state->recovery, 0, sizeof(state->recovery));
    atomic_init(&state->recovery_head, 0);
    atomic_init(&state->recovery_tail, 0);
    memset(state->scheduled_releases, 0, sizeof(state->scheduled_releases));
    state->scheduled_release_count = 0;
}

bool bongo_cat_input_edge(bool states[BONGO_CAT_INPUT_KEY_STATE_CAP],
    unsigned code, bool down) {
    if (!states || code >= BONGO_CAT_INPUT_KEY_STATE_CAP) return true;
    if (states[code] == down) return false;
    states[code] = down;
    return true;
}

static void update_modifiers(BongoCatInputState *state, const BongoCatInputEvent *event) {
    if (event->kind != BONGO_CAT_INPUT_KEY_DOWN && event->kind != BONGO_CAT_INPUT_KEY_UP) return;
    atomic_uint_fast8_t *value = &state->control;
    uint8_t mask = strcmp(event->name, "ControlLeft") == 0 ? 1 :
        strcmp(event->name, "ControlRight") == 0 ? 2 : 0;
    if (!mask) {
        value = &state->shift;
        mask = strcmp(event->name, "ShiftLeft") == 0 ? 1 :
            strcmp(event->name, "ShiftRight") == 0 ? 2 : 0;
    }
    if (!mask) return;
    if (event->kind == BONGO_CAT_INPUT_KEY_DOWN)
        atomic_fetch_or_explicit(value, mask, memory_order_release);
    else atomic_fetch_and_explicit(value, (uint8_t)~mask, memory_order_release);
}

static bool push_locked(BongoCatInputState *state, const BongoCatInputEvent *event) {
    update_modifiers(state, event);
    uint64_t sequence = atomic_fetch_add_explicit(&state->next_sequence, 1,
        memory_order_relaxed);
    uint16_t head = (uint16_t)atomic_load_explicit(&state->head, memory_order_relaxed);
    uint16_t next = (uint16_t)((head + 1u) % BONGO_CAT_INPUT_QUEUE_CAP);
    if (next == atomic_load_explicit(&state->tail, memory_order_acquire)) {
        if (event->kind == BONGO_CAT_INPUT_KEY_UP || event->kind == BONGO_CAT_INPUT_MOUSE_UP) {
            uint8_t recovery_head = (uint8_t)atomic_load_explicit(
                &state->recovery_head, memory_order_relaxed);
            uint8_t recovery_next = (uint8_t)((recovery_head + 1u) %
                BONGO_CAT_INPUT_RECOVERY_CAP);
            if (recovery_next != atomic_load_explicit(&state->recovery_tail,
                memory_order_acquire)) {
                state->recovery[recovery_head] = *event;
                state->recovery[recovery_head].sequence = sequence;
                atomic_store_explicit(&state->recovery_head, recovery_next,
                    memory_order_release);
                return true;
            }
        }
        atomic_fetch_add_explicit(&state->dropped, 1, memory_order_relaxed);
        return false;
    }
    state->queue[head] = *event;
    state->queue[head].sequence = sequence;
    atomic_store_explicit(&state->head, next, memory_order_release);
    return true;
}

static bool pop_locked(BongoCatInputState *state, BongoCatInputEvent *event) {
    uint16_t tail = (uint16_t)atomic_load_explicit(&state->tail, memory_order_relaxed);
    uint16_t head = (uint16_t)atomic_load_explicit(&state->head, memory_order_acquire);
    uint8_t recovery_tail = (uint8_t)atomic_load_explicit(
        &state->recovery_tail, memory_order_relaxed);
    uint8_t recovery_head = (uint8_t)atomic_load_explicit(
        &state->recovery_head, memory_order_acquire);
    bool main_ready = tail != head;
    bool recovery_ready = recovery_tail != recovery_head;
    if (!main_ready && !recovery_ready) return false;
    if (recovery_ready && (!main_ready || state->recovery[recovery_tail].sequence <
        state->queue[tail].sequence)) {
        *event = state->recovery[recovery_tail];
        atomic_store_explicit(&state->recovery_tail,
            (uint8_t)((recovery_tail + 1u) % BONGO_CAT_INPUT_RECOVERY_CAP),
            memory_order_release);
        return true;
    }
    *event = state->queue[tail];
    atomic_store_explicit(&state->tail,
        (tail + 1u) % BONGO_CAT_INPUT_QUEUE_CAP, memory_order_release);
    return true;
}

/* Platform listeners and SDL/gamepad events can produce concurrently. Protect
   both rings together so recovery releases remain ordered with normal events.
   The critical section only copies fixed-size events; it never waits on I/O. */
static void lock_queue(BongoCatInputState *state) {
    while (atomic_exchange_explicit(&state->queue_busy, true, memory_order_acquire)) {}
}

bool bongo_cat_input_push(BongoCatInputState *state, const BongoCatInputEvent *event) {
    if (!state || !event) return false;
    lock_queue(state);
    bool pushed = push_locked(state, event);
    atomic_store_explicit(&state->queue_busy, false, memory_order_release);
    return pushed;
}

bool bongo_cat_input_pop(BongoCatInputState *state, BongoCatInputEvent *event) {
    if (!state || !event) return false;
    lock_queue(state);
    bool popped = pop_locked(state, event);
    atomic_store_explicit(&state->queue_busy, false, memory_order_release);
    return popped;
}

bool bongo_cat_input_mouse(BongoCatInputState *state, double x, double y) {
    if (!state) return false;
    atomic_store_explicit(&state->mouse_x, x, memory_order_relaxed);
    atomic_store_explicit(&state->mouse_y, y, memory_order_relaxed);
    return !atomic_exchange_explicit(&state->mouse_dirty, true,
        memory_order_acq_rel);
}

bool bongo_cat_input_take_mouse(BongoCatInputState *state, double *x, double *y) {
    if (!state || !x || !y ||
        !atomic_exchange_explicit(&state->mouse_dirty, false, memory_order_acquire)) return false;
    *x = atomic_load_explicit(&state->mouse_x, memory_order_relaxed);
    *y = atomic_load_explicit(&state->mouse_y, memory_order_relaxed);
    return true;
}

bool bongo_cat_input_control_down(const BongoCatInputState *state) {
    return state && atomic_load_explicit(&state->control, memory_order_acquire) != 0;
}

bool bongo_cat_input_shift_down(const BongoCatInputState *state) {
    return state && atomic_load_explicit(&state->shift, memory_order_acquire) != 0;
}

static size_t release_index(const BongoCatInputState *state, const char *name) {
    for (size_t i = 0; i < state->scheduled_release_count; ++i)
        if (strcmp(state->scheduled_releases[i].name, name) == 0) return i;
    return state->scheduled_release_count;
}

static void remove_release(BongoCatInputState *state, size_t index) {
    if (index >= state->scheduled_release_count) return;
    state->scheduled_releases[index] =
        state->scheduled_releases[state->scheduled_release_count - 1];
    memset(&state->scheduled_releases[--state->scheduled_release_count], 0,
        sizeof(state->scheduled_releases[0]));
}

void bongo_cat_input_schedule_release(BongoCatInputState *state,
    const BongoCatInputEvent *event, uint64_t delay_ms) {
    if (!state || !event || !event->name[0] ||
        (event->kind != BONGO_CAT_INPUT_KEY_DOWN && event->kind != BONGO_CAT_INPUT_KEY_UP)) return;
    size_t index = release_index(state, event->name);
    if (event->kind == BONGO_CAT_INPUT_KEY_UP || !delay_ms) {
        remove_release(state, index);
        return;
    }
    if (index == state->scheduled_release_count) {
        if (state->scheduled_release_count >=
            BONGO_CAT_SCHEDULED_RELEASE_CAP) return;
        index = state->scheduled_release_count++;
        snprintf(state->scheduled_releases[index].name,
            sizeof(state->scheduled_releases[index].name), "%s", event->name);
    }
    state->scheduled_releases[index].deadline_ms =
        event->timestamp_ms + delay_ms;
}

bool bongo_cat_input_take_scheduled_release(BongoCatInputState *state,
    uint64_t now_ms, BongoCatInputEvent *event) {
    if (!state || !event) return false;
    for (size_t i = 0; i < state->scheduled_release_count; ++i) {
        if (state->scheduled_releases[i].deadline_ms > now_ms) continue;
        memset(event, 0, sizeof(*event));
        event->kind = BONGO_CAT_INPUT_KEY_UP;
        event->timestamp_ms = now_ms;
        snprintf(event->name, sizeof(event->name), "%s",
            state->scheduled_releases[i].name);
        remove_release(state, i);
        return true;
    }
    return false;
}
