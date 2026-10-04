#include "preferences_state.h"
#include "preferences_model_glyphs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool glyph_covered(const uint32_t *ranges, size_t used, uint32_t rune) {
    size_t first = 0, last = used / 2;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (rune < ranges[middle * 2]) last = middle;
        else if (rune > ranges[middle * 2 + 1]) first = middle + 1;
        else return true;
    }
    return false;
}

static int compare_ranges(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return a < b ? -1 : a > b;
}

static void add_text(uint32_t *ranges, size_t capacity,
    size_t *used, const char *text) {
    const char *cursor = text;
    size_t remaining = text ? strlen(text) : 0;
    while (cursor && *cursor) {
        nk_rune rune = 0;
        int decoded = nk_utf_decode(cursor, &rune, (int)remaining);
        if (decoded <= 0) {
            cursor++;
            if (remaining) remaining--;
            continue;
        }
        cursor += decoded; remaining -= (size_t)decoded;
        if (rune <= 0x7e || glyph_covered(ranges, *used, rune)) continue;
        if (*used + 2 >= capacity) return;
        size_t position = 0;
        while (position < *used && ranges[position] < rune) position += 2;
        memmove(ranges + position + 2, ranges + position,
            (*used - position) * sizeof(*ranges));
        ranges[position] = ranges[position + 1] = rune;
        *used += 2;
        ranges[*used] = 0;
    }
}

static bool text_covered(const uint32_t *ranges, size_t capacity,
    const char *text) {
    if (!ranges || !capacity || !text || !text[0]) return false;
    size_t used = 0;
    while (used < capacity && ranges[used]) used++;
    const char *cursor = text;
    size_t remaining = text ? strlen(text) : 0;
    while (cursor && *cursor) {
        nk_rune rune = 0;
        int decoded = nk_utf_decode(cursor, &rune, (int)remaining);
        if (decoded <= 0) return false;
        if (rune > 0x7e && !glyph_covered(ranges, used, rune)) return false;
        cursor += decoded;
        remaining -= (size_t)decoded;
    }
    return true;
}

void bongo_cat_preferences_model_glyphs_note(
    BongoCatPreferences *preferences, const char *name) {
    if (!preferences || !name || !name[0]) return;
    for (size_t i = 0; i < preferences->pending_import_name_count; ++i)
        if (!strcmp(preferences->pending_import_names[i], name)) return;
    if (preferences->pending_import_name_count >= BONGO_CAT_MODEL_CAP) return;
    snprintf(preferences->pending_import_names[
        preferences->pending_import_name_count++], BONGO_CAT_ID_CAP, "%s", name);
}

bool bongo_cat_preferences_model_glyphs_ready(
    const BongoCatPreferences *preferences, const char *name) {
    return preferences && text_covered(preferences->glyph_ranges,
        sizeof(preferences->glyph_ranges) /
            sizeof(preferences->glyph_ranges[0]), name);
}

void bongo_cat_preferences_model_glyphs_clear_pending(
    BongoCatPreferences *preferences) {
    if (preferences) preferences->pending_import_name_count = 0;
}

bool bongo_cat_preferences_behavior_glyphs_ready(
    const BongoCatPreferences *preferences) {
    if (!preferences || !preferences->app) return false;
    const BongoCatApp *app = preferences->app;
    const BongoCatBehaviorCatalog *catalog =
        bongo_cat_preferences_behavior_catalog(preferences);
    for (size_t i = 0; i < catalog->count; ++i) {
        const char *label = catalog->entries[i].label;
        if (*label && !bongo_cat_preferences_model_glyphs_ready(preferences, label))
            return false;
    }
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const char *label = app->settings.behavior_shortcuts[i].label;
        if (*label && !bongo_cat_preferences_model_glyphs_ready(preferences, label))
            return false;
    }
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next)
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next)
            if (node->binding.label[0] &&
                !bongo_cat_preferences_model_glyphs_ready(preferences, node->binding.label)) return false;
    return true;
}

void bongo_cat_preferences_model_glyphs(const BongoCatApp *app,
    uint32_t *ranges, size_t capacity) {
    if (!app || !ranges) return;
    size_t used = 0;
    while (used < capacity && ranges[used]) used++;
    if (used >= capacity || (used & 1)) return;
    qsort(ranges, used / 2, sizeof(*ranges) * 2, compare_ranges);
    size_t written = 0;
    for (size_t i = 0; i < used; i += 2) {
        if (written && ranges[i] <= ranges[written - 1] + 1) {
            if (ranges[i + 1] > ranges[written - 1])
                ranges[written - 1] = ranges[i + 1];
        } else {
            ranges[written++] = ranges[i];
            ranges[written++] = ranges[i + 1];
        }
    }
    used = written;
    ranges[used] = 0;
    /* Symbols used by the HTML reference's contribution category badges. */
    add_text(ranges, capacity, &used, "〈/〉▤♧文");
    for (size_t i = 0; i < app->models.count; ++i) {
        const BongoCatModelEntry *entry = &app->models.entries[i];
        add_text(ranges, capacity, &used,
            bongo_cat_model_name(&app->settings, entry));
    }
    for (size_t i = 0; i < app->behaviors.count; ++i)
        add_text(ranges, capacity, &used, app->behaviors.entries[i].label);
    for (size_t i = 0;
        i < app->settings.behavior_shortcut_count; ++i)
        add_text(ranges, capacity, &used,
            app->settings.behavior_shortcuts[i].label);
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next)
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next)
            add_text(ranges, capacity, &used, node->binding.label);
    if (app->preferences) {
        const BongoCatPreferences *preferences = app->preferences;
        const BongoCatBehaviorCatalog *catalog =
            bongo_cat_preferences_behavior_catalog(preferences);
        for (size_t i = 0; i < catalog->count; ++i)
            add_text(ranges, capacity, &used, catalog->entries[i].label);
        for (size_t i = 0; i < sizeof(preferences->notices) /
            sizeof(preferences->notices[0]); ++i)
            add_text(ranges, capacity, &used, preferences->notices[i].message);
        for (size_t i = 0; i < preferences->pending_import_name_count; ++i)
            add_text(ranges, capacity, &used,
                preferences->pending_import_names[i]);
    }
}
