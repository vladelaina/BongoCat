#include "runtime.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/model_memory.h"
#include <SDL3/SDL_timer.h>

static unsigned pending_presented_frames;
static bool pending_ui_frame;
static bool pending_ui_release;
static uint64_t model_trim_deadline_ms;

static void trim_model(void) {
    pending_presented_frames = 0;
    model_trim_deadline_ms = 0;
    pending_ui_release = false;
    bongo_cat_platform_trim_memory();
    bongo_cat_model_memory_after_trim();
}

static void consume_pending_frame(void) {
    if (!pending_presented_frames) return;
    pending_presented_frames--;
    if (!pending_presented_frames) trim_model();
}

void bongo_cat_memory_policy_model_loaded(void) {
    pending_presented_frames = 2;
    model_trim_deadline_ms = SDL_GetTicks() + 500;
}

void bongo_cat_memory_policy_frame_presented(void) {
    consume_pending_frame();
}

void bongo_cat_memory_policy_idle(void) {
    /* Tray-only and minimized modes may not present a frame after loading. */
    consume_pending_frame();
}

void bongo_cat_memory_policy_ui_loaded(void) {
    pending_ui_frame = true;
}

void bongo_cat_memory_policy_ui_frame_presented(void) {
    if (!pending_ui_frame) return;
    pending_ui_frame = false;
    pending_ui_release = false;
    bongo_cat_platform_trim_memory();
}

void bongo_cat_memory_policy_ui_released(void) {
    pending_ui_frame = false;
    pending_ui_release = true;
}

void bongo_cat_memory_policy_poll(void) {
    /* A static PNG/model can present only once after a switch. Do not leave
       its unused heap pages waiting indefinitely for a second dirty frame. */
    bool model_due = model_trim_deadline_ms &&
        SDL_GetTicks() >= model_trim_deadline_ms;
    if (!model_due && !pending_ui_release) return;
    pending_ui_release = false;
    if (model_due) trim_model();
    else bongo_cat_platform_trim_memory();
}
