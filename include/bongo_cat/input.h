#ifndef BONGO_CAT_INPUT_H
#define BONGO_CAT_INPUT_H

#include "bongo_cat/common.h"
#include <stdatomic.h>

#define BONGO_CAT_INPUT_QUEUE_CAP 256u
#define BONGO_CAT_INPUT_RECOVERY_CAP 65u
#define BONGO_CAT_INPUT_KEY_STATE_CAP 256u

typedef enum BongoCatInputKind {
    BONGO_CAT_INPUT_NONE,
    BONGO_CAT_INPUT_KEY_DOWN,
    BONGO_CAT_INPUT_KEY_UP,
    BONGO_CAT_INPUT_MOUSE_DOWN,
    BONGO_CAT_INPUT_MOUSE_UP,
    BONGO_CAT_INPUT_MOUSE_MOVE,
    BONGO_CAT_INPUT_GAMEPAD_BUTTON,
    BONGO_CAT_INPUT_GAMEPAD_AXIS
} BongoCatInputKind;

typedef struct BongoCatInputEvent {
    BongoCatInputKind kind;
    uint64_t timestamp_ms;
    char name[BONGO_CAT_ID_CAP];
    float value;
    double x;
    double y;
    uint64_t sequence;
} BongoCatInputEvent;

typedef struct BongoCatScheduledRelease {
    uint64_t deadline_ms;
    char name[BONGO_CAT_ID_CAP];
} BongoCatScheduledRelease;

typedef struct BongoCatInputState {
    atomic_bool queue_busy;
    BongoCatInputEvent queue[BONGO_CAT_INPUT_QUEUE_CAP];
    atomic_uint_fast16_t head;
    atomic_uint_fast16_t tail;
    _Atomic double mouse_x;
    _Atomic double mouse_y;
    atomic_bool mouse_dirty;
    atomic_uint_fast8_t control;
    atomic_uint_fast8_t shift;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t next_sequence;
    BongoCatInputEvent recovery[BONGO_CAT_INPUT_RECOVERY_CAP];
    atomic_uint_fast8_t recovery_head;
    atomic_uint_fast8_t recovery_tail;
    BongoCatScheduledRelease scheduled_releases[
        BONGO_CAT_SCHEDULED_RELEASE_CAP];
    size_t scheduled_release_count;
} BongoCatInputState;

void bongo_cat_input_init(BongoCatInputState *state);
bool bongo_cat_input_edge(bool states[BONGO_CAT_INPUT_KEY_STATE_CAP],
    unsigned code, bool down);
bool bongo_cat_input_push(BongoCatInputState *state, const BongoCatInputEvent *event);
bool bongo_cat_input_pop(BongoCatInputState *state, BongoCatInputEvent *event);
bool bongo_cat_input_mouse(BongoCatInputState *state, double x, double y);
bool bongo_cat_input_take_mouse(BongoCatInputState *state, double *x, double *y);
bool bongo_cat_input_control_down(const BongoCatInputState *state);
bool bongo_cat_input_shift_down(const BongoCatInputState *state);
void bongo_cat_input_schedule_release(BongoCatInputState *state,
    const BongoCatInputEvent *event, uint64_t delay_ms);
bool bongo_cat_input_take_scheduled_release(BongoCatInputState *state,
    uint64_t now_ms, BongoCatInputEvent *event);

#endif
