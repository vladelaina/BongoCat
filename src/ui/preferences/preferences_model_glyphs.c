#include "preferences_state.h"
#include "preferences_model_glyphs.h"

#include <stdio.h>
#include <string.h>

static bool glyph_covered(const uint32_t *ranges, size_t used, uint32_t rune) {
    for (size_t i = 0; i + 1 < used; i += 2)
        if (rune >= ranges[i] && rune <= ranges[i + 1]) return true;
    return false;
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
        ranges[(*used)++] = rune;
        ranges[(*used)++] = rune;
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

void bongo_cat_preferences_model_glyphs(const BongoCatApp *app,
    uint32_t *ranges, size_t capacity) {
    if (!app || !ranges) return;
    size_t used = 0;
    while (used < capacity && ranges[used]) used++;
    if (used >= capacity || (used & 1)) return;
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
    if (app->preferences) {
        const BongoCatPreferences *preferences = app->preferences;
        for (size_t i = 0; i < sizeof(preferences->notices) /
            sizeof(preferences->notices[0]); ++i)
            add_text(ranges, capacity, &used, preferences->notices[i].message);
        for (size_t i = 0; i < preferences->pending_import_name_count; ++i)
            add_text(ranges, capacity, &used,
                preferences->pending_import_names[i]);
    }
}
