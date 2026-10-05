#include "config_store_test.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static size_t steps, attempts, json_bytes;

typedef struct Pair {
    BongoCatApp *current, *reference;
    ConfigTrace actual, expected;
} Pair;

#define SET(pair, field, value) do { \
    (pair)->current->field = (pair)->reference->field = (value); \
} while (0)

static Pair create_pair(bool saturated, bool clean) {
    Pair pair = {0};
    pair.current = config_test_app(saturated, clean);
    pair.reference = malloc(sizeof(*pair.reference));
    if (!pair.reference) abort();
    memcpy(pair.reference, pair.current, sizeof(*pair.reference));
    return pair;
}

static size_t step(Pair *pair, uint64_t now, bool flush, unsigned failures) {
    config_test_clear(&pair->actual);
    config_test_clear(&pair->expected);
    pair->actual.time = pair->expected.time = now;
    pair->actual.failures = pair->expected.failures = failures;
    config_test_trace(&pair->actual);
    if (flush) bongo_cat_config_store_flush(pair->current);
    else bongo_cat_config_store_update(pair->current, now);
    config_test_trace(&pair->expected);
    if (flush) config_reference_flush(pair->reference);
    else config_reference_update(pair->reference, now);
    config_test_compare(pair->current, pair->reference);
    config_test_compare_trace(&pair->actual, &pair->expected);
    ++steps;
    attempts += pair->actual.count;
    for (size_t i = 0; i < pair->actual.count; ++i)
        json_bytes += pair->actual.saves[i].length;
    return pair->actual.count;
}

static void destroy_pair(Pair *pair) {
    config_test_clear(&pair->actual);
    config_test_clear(&pair->expected);
    free(pair->current);
    free(pair->reference);
}

static void exercise(bool saturated, unsigned mask) {
    Pair pair = create_pair(saturated, false);
    SET(&pair, secondary_pet, (mask & 1) != 0);
    SET(&pair, settings_store_blocked, (mask & 2) != 0);
    SET(&pair, session_store_blocked, (mask & 4) != 0);
    bool settings = !(mask & 3), session = !(mask & 4);
    size_t eligible = (size_t)settings + (size_t)session;
    /* A missing store writes once at exactly 300 ms, never one ns early. */
    CHECK(step(&pair, 0, false, 0) == 0);
    CHECK(pair.current->settings_save_due_ns == (settings ? 300000000 : 0));
    CHECK(pair.current->session_save_due_ns == (session ? 300000000 : 0));
    CHECK(step(&pair, 299999999, false, 0) == 0);
    CHECK(step(&pair, 300000000, false, 0) == eligible);
    CHECK(step(&pair, 900000000, false, 0) == 0);

    /* Reverting to saved values still preserves the baseline's queued save. */
    bool mirror = pair.current->settings.model.mirror;
    int x = pair.current->session.window.x;
    SET(&pair, settings.model.mirror, !mirror);
    SET(&pair, session.window.x, x + 1);
    CHECK(step(&pair, 1000000000, false, 0) == 0);
    CHECK(step(&pair, 1100000000, false, 0) == 0);
    SET(&pair, settings.model.mirror, mirror);
    SET(&pair, session.window.x, x);
    CHECK(step(&pair, 1200000000, false, 0) == 0);
    CHECK(step(&pair, 1499999999, false, 0) == 0);
    CHECK(step(&pair, 1500000000, false, 0) == eligible);

    /* Failed saves retain their exact one-second retry, independently. */
    SET(&pair, settings.model.mirror, !mirror);
    SET(&pair, session.window.x, x + 2);
    CHECK(step(&pair, 2000000000, false, 0) == 0);
    CHECK(step(&pair, 2300000000, false, 3) == eligible);
    CHECK(pair.current->settings_save_due_ns == (settings ? 3300000000ull : 0));
    CHECK(pair.current->session_save_due_ns == (session ? 3300000000ull : 0));
    CHECK(step(&pair, 3299999999, false, 0) == 0);
    CHECK(step(&pair, 3300000000, false, 0) == eligible);

    /* Validation during a failed save changes state; its new debounce wins. */
    SET(&pair, settings.model.max_fps, -7);
    SET(&pair, session.window.width, 1);
    CHECK(step(&pair, 4000000000, false, 0) == 0);
    CHECK(step(&pair, 4300000000, false, 3) == eligible);
    CHECK(step(&pair, 4300000001, false, 0) == 0);
    CHECK(step(&pair, 4600000000, false, 0) == 0);
    CHECK(step(&pair, 4600000001, false, 0) == eligible);

    /* Flush keeps observed hashes current even for ineligible stores. */
    SET(&pair, settings.model.mirror, mirror);
    SET(&pair, session.window.x, x + 3);
    CHECK(step(&pair, 5000000000, false, 0) == 0);
    CHECK(step(&pair, 5100000000, true, 0) == eligible);
    CHECK(!pair.current->settings_save_due_ns);
    CHECK(!pair.current->session_save_due_ns);
    uint64_t settings_observed = pair.current->settings_observed_hash;
    uint64_t session_observed = pair.current->session_observed_hash;
    CHECK(step(&pair, 5100000001, true, 0) == 0);
    CHECK(pair.current->settings_observed_hash == settings_observed);
    CHECK(pair.current->session_observed_hash == session_observed);

    /* Re-enable eligibility; any unsaved values retain the usual debounce. */
    SET(&pair, secondary_pet, false);
    SET(&pair, settings_store_blocked, false);
    SET(&pair, session_store_blocked, false);
    CHECK(step(&pair, 6000000000, false, 0) == 0);
    CHECK(step(&pair, 6299999999, false, 0) == 0);
    CHECK(step(&pair, 6300000000, false, 0) == 2 - eligible);
    destroy_pair(&pair);
}

static void guards_and_flush_failure(void) {
    Pair pair = create_pair(false, true);
    SET(&pair, settings.model.mirror, !pair.current->settings.model.mirror);
    SET(&pair, session.window.x, 17);
    CHECK(step(&pair, 1, false, 0) == 0);
    uint64_t due = pair.current->settings_save_due_ns;
    SET(&pair, smoke, true);
    CHECK(step(&pair, 9000000000, false, 0) == 0);
    CHECK(step(&pair, 9000000000, true, 0) == 0);
    CHECK(pair.current->settings_save_due_ns == due);
    SET(&pair, smoke, false);
    for (unsigned which = 0; which < 2; ++which) {
        char *a = which ? pair.current->session_path : pair.current->settings_path;
        char *b = which ? pair.reference->session_path : pair.reference->settings_path;
        char first = a[0];
        a[0] = b[0] = '\0';
        CHECK(step(&pair, 9000000000, false, 0) == 0);
        CHECK(step(&pair, 9000000000, true, 0) == 0);
        CHECK(pair.current->settings_save_due_ns == due);
        a[0] = b[0] = first;
    }
    CHECK(step(&pair, 9000000000, true, 3) == 2);
    CHECK(!pair.current->settings_save_due_ns && !pair.current->session_save_due_ns);
    CHECK(step(&pair, 9000000001, false, 0) == 0);
    CHECK(step(&pair, 9300000000, false, 0) == 0);
    CHECK(step(&pair, 9300000001, false, 1) == 2);
    CHECK(pair.current->settings_save_due_ns == 10300000001ull);
    CHECK(!pair.current->session_save_due_ns);
    /* Blocking an already-queued retry preserves its deadline until unblocked. */
    SET(&pair, settings_store_blocked, true);
    CHECK(step(&pair, 11000000000, false, 0) == 0);
    CHECK(pair.current->settings_save_due_ns == 10300000001ull);
    SET(&pair, settings_store_blocked, false);
    CHECK(step(&pair, 11000000001, false, 0) == 1);
    destroy_pair(&pair);
}

static void mixed_sequence(bool saturated) {
    Pair pair = create_pair(saturated, true);
    uint32_t random = 0x4f07cafe;
    uint64_t now = 0;
    for (unsigned i = 0; i < 512; ++i) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        switch (random % 12) {
        case 0:
            SET(&pair, settings.window.pass_through,
                !pair.current->settings.window.pass_through);
            break;
        case 1: SET(&pair, session.window.y, (int)(random % 1000)); break;
        case 2: SET(&pair, secondary_pet, (random & 64) != 0); break;
        case 3: SET(&pair, settings_store_blocked, (random & 128) != 0); break;
        case 4: SET(&pair, session_store_blocked, (random & 256) != 0); break;
        case 5:
            SET(&pair, settings.behavior_shortcut_count,
                saturated ? BONGO_CAT_BEHAVIOR_BINDING_CAP + 1 : 0);
            break;
        case 6:
            SET(&pair, session.active_behavior_count,
                saturated ? BONGO_CAT_BEHAVIOR_BINDING_CAP + 1 : 0);
            break;
        case 7:
            SET(&pair, settings.extensions_json[0], '{');
            SET(&pair, settings.extensions_json[1], '}');
            SET(&pair, settings.extensions_json[2], '\0');
            break;
        case 8:
            SET(&pair, session.window.opacity_percent, (float)(random % 150));
            break;
        case 9:
            if (saturated) {
                SET(&pair, settings.behavior_shortcuts[255].label[0], 'A' + i % 26);
                SET(&pair, session.active_behaviors[255].behavior_id[0], 'a' + i % 26);
            }
            break;
        default: break;
        }
        now += (random % 3 == 0) ? 1000000000ull : 17000000ull;
        (void)step(&pair, now, random % 11 == 0, (random >> 16) & 3);
    }
    SET(&pair, secondary_pet, false);
    SET(&pair, settings_store_blocked, false);
    SET(&pair, session_store_blocked, false);
    (void)step(&pair, now + 1, true, 0);
    /* Preserve unsigned clock-wrap behavior as well as ordinary deadlines. */
    SET(&pair, settings.model.mirror, !pair.current->settings.model.mirror);
    SET(&pair, session.window.x, pair.current->session.window.x + 1);
    CHECK(step(&pair, UINT64_MAX - 100, false, 0) == 2);
    destroy_pair(&pair);
}

int main(void) {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL);
    bongo_cat_config_store_update(NULL, 0);
    config_reference_update(NULL, 0);
    bongo_cat_config_store_flush(NULL);
    config_reference_flush(NULL);
    for (unsigned saturated = 0; saturated < 2; ++saturated)
        for (unsigned mask = 0; mask < 8; ++mask)
            exercise(saturated != 0, mask);
    guards_and_flush_failure();
    mixed_sequence(false);
    mixed_sequence(true);
    printf("config-store: %zu differential steps, %zu save attempts, "
        "%zu JSON bytes compared, %d failures\n", steps, attempts,
        json_bytes, bongo_cat_test_failures);
    return bongo_cat_test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
