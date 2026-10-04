#include "bongo_cat/input.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define PRODUCERS 4
#define EVENTS 20000
int bongo_cat_test_failures;
static BongoCatInputState input;
static atomic_int finished;
static atomic_uint accepted;

static int SDLCALL produce(void *userdata) {
    unsigned id = *(unsigned *)userdata;
    for (unsigned i = 0; i < EVENTS; ++i) {
        BongoCatInputEvent event = {
            .kind = i % 2 ? BONGO_CAT_INPUT_KEY_UP : BONGO_CAT_INPUT_KEY_DOWN,
            .timestamp_ms = i, .x = id, .y = i, .value = (float)id
        };
        snprintf(event.name, sizeof(event.name), "producer%u", id);
        if (bongo_cat_input_push(&input, &event)) atomic_fetch_add(&accepted, 1);
        if (i % 128 == 0) SDL_Delay(1);
    }
    atomic_fetch_add(&finished, 1);
    return 0;
}

int main(void) {
    bongo_cat_input_init(&input);
    atomic_init(&finished, 0);
    atomic_init(&accepted, 0);
    SDL_Thread *threads[PRODUCERS];
    unsigned ids[PRODUCERS], last[PRODUCERS] = {0};
    bool seen[PRODUCERS] = {0};
    for (unsigned i = 0; i < PRODUCERS; ++i) {
        ids[i] = i;
        threads[i] = SDL_CreateThread(produce, "input-producer", &ids[i]);
        CHECK(threads[i] != NULL);
        if (!threads[i]) return 1;
    }
    unsigned received = 0;
    uint64_t sequence = 0;
    for (;;) {
        BongoCatInputEvent event;
        bool done = atomic_load(&finished) == PRODUCERS;
        if (!bongo_cat_input_pop(&input, &event)) {
            if (done) break;
            SDL_Delay(1);
            continue;
        }
        unsigned id = (unsigned)event.x;
        CHECK(id < PRODUCERS);
        if (id >= PRODUCERS) continue;
        char expected[32];
        snprintf(expected, sizeof(expected), "producer%u", id);
        CHECK(strcmp(expected, event.name) == 0);
        CHECK(event.value == (float)id);
        CHECK(event.y == (double)event.timestamp_ms);
        CHECK(event.kind == (event.timestamp_ms % 2 ?
            BONGO_CAT_INPUT_KEY_UP : BONGO_CAT_INPUT_KEY_DOWN));
        CHECK(!seen[id] || event.timestamp_ms > last[id]);
        CHECK(!received || event.sequence > sequence);
        seen[id] = true;
        last[id] = (unsigned)event.timestamp_ms;
        sequence = event.sequence;
        ++received;
    }
    for (unsigned i = 0; i < PRODUCERS; ++i) SDL_WaitThread(threads[i], NULL);
    CHECK(received == atomic_load(&accepted));
    CHECK(received + atomic_load(&input.dropped) == PRODUCERS * EVENTS);
    for (unsigned i = 0; i < PRODUCERS; ++i) CHECK(seen[i]);
    return bongo_cat_test_failures ? 1 : 0;
}
