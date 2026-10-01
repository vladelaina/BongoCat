#include "bongo_cat/config.h"
#include "bongo_cat/utf8.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf_or(float value, float low, float high, float fallback) {
    if (!isfinite(value)) return fallback;
    return value < low ? low : value > high ? high : value;
}

static bool normalize_text(char *text, size_t capacity) {
    text[capacity - 1] = '\0';
    if (!bongo_cat_utf8_valid(text)) {
        memset(text, 0, capacity);
        return false;
    }
    size_t length = strlen(text);
    memset(text + length + 1, 0, capacity - length - 1);
    return true;
}

void bongo_cat_session_defaults(BongoCatSessionState *session) {
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->window.visible = true;
    session->window.scale_percent = BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT;
    session->window.opacity_percent =
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT;
    session->window.width = 612;
    session->window.height = 354;
    session->window.content_width = 612;
    session->window.content_height = 354;
    memcpy(session->active_model_id, "standard", sizeof("standard"));
}

static void compact_active_behaviors(BongoCatSessionState *session) {
    size_t count = session->active_behavior_count;
    if (count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    size_t output = 0;
    for (size_t i = 0; i < count; ++i) {
        BongoCatActiveBehavior entry = session->active_behaviors[i];
        if (!normalize_text(entry.model_id, sizeof(entry.model_id)) ||
            !normalize_text(entry.behavior_id, sizeof(entry.behavior_id)) ||
            !entry.model_id[0] || !entry.behavior_id[0]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < output; ++j)
            if (!strcmp(session->active_behaviors[j].model_id,
                    entry.model_id) &&
                !strcmp(session->active_behaviors[j].behavior_id,
                    entry.behavior_id)) {
                duplicate = true;
                break;
            }
        if (!duplicate) session->active_behaviors[output++] = entry;
    }
    if (output < BONGO_CAT_BEHAVIOR_BINDING_CAP)
        memset(&session->active_behaviors[output], 0,
            (BONGO_CAT_BEHAVIOR_BINDING_CAP - output) *
            sizeof(session->active_behaviors[0]));
    session->active_behavior_count = output;
}

static void compact_additional_models(BongoCatSessionState *session) {
    size_t count = session->additional_model_count;
    if (count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    size_t output = 0;
    for (size_t i = 0; i < count; ++i) {
        char model_id[BONGO_CAT_ID_CAP];
        memcpy(model_id, session->additional_model_ids[i], sizeof(model_id));
        if (!normalize_text(model_id, sizeof(model_id)) || !model_id[0] ||
            !strcmp(model_id, session->active_model_id)) continue;
        bool duplicate = false;
        for (size_t j = 0; j < output; ++j)
            if (!strcmp(session->additional_model_ids[j], model_id)) {
                duplicate = true;
                break;
            }
        if (!duplicate) {
            memset(session->additional_model_ids[output], 0,
                sizeof(session->additional_model_ids[0]));
            snprintf(session->additional_model_ids[output++],
                sizeof(session->additional_model_ids[0]), "%s", model_id);
        }
    }
    memset(&session->additional_model_ids[output], 0,
        (BONGO_CAT_ADDITIONAL_MODEL_CAP - output) *
        sizeof(session->additional_model_ids[0]));
    session->additional_model_count = output;
}

void bongo_cat_session_validate(BongoCatSessionState *session) {
    if (!session) return;
    session->window.scale_percent = clampf_or(session->window.scale_percent,
        10.0f, 500.0f, BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT);
    session->window.opacity_percent = clampf_or(
        session->window.opacity_percent, 10.0f, 100.0f,
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    if (session->window.width < 64) session->window.width = 64;
    if (session->window.height < 64) session->window.height = 64;
    if (session->window.width > 8192) session->window.width = 8192;
    if (session->window.height > 8192) session->window.height = 8192;
    if (session->window.content_width < 64)
        session->window.content_width = session->window.width;
    if (session->window.content_height < 64)
        session->window.content_height = session->window.height;
    if (session->window.content_width > 8192)
        session->window.content_width = 8192;
    if (session->window.content_height > 8192)
        session->window.content_height = 8192;
    if (!normalize_text(session->active_model_id,
            sizeof(session->active_model_id)) ||
        !session->active_model_id[0])
        memcpy(session->active_model_id, "standard", sizeof("standard"));
    if (session->last_update_check_day < 0 ||
        session->last_update_check_day > 99991231)
        session->last_update_check_day = 0;
    normalize_text(session->last_update_check_version,
        sizeof(session->last_update_check_version));
    normalize_text(session->available_update_version,
        sizeof(session->available_update_version));
    compact_additional_models(session);
    compact_active_behaviors(session);
}

bool bongo_cat_session_model_active(const BongoCatSessionState *session,
    const char *model_id) {
    if (!session || !model_id || !model_id[0]) return false;
    if (!strcmp(session->active_model_id, model_id)) return true;
    size_t count = session->additional_model_count;
    if (count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(session->additional_model_ids[i], model_id)) return true;
    return false;
}

bool bongo_cat_session_add_model(BongoCatSessionState *session,
    const char *model_id) {
    if (!session || !model_id || !model_id[0] ||
        strlen(model_id) >= BONGO_CAT_ID_CAP ||
        !bongo_cat_utf8_valid(model_id)) return false;
    bongo_cat_session_validate(session);
    if (bongo_cat_session_model_active(session, model_id)) return true;
    if (session->additional_model_count >= BONGO_CAT_ADDITIONAL_MODEL_CAP)
        return false;
    snprintf(session->additional_model_ids[session->additional_model_count++],
        sizeof(session->additional_model_ids[0]), "%s", model_id);
    return true;
}

bool bongo_cat_session_remove_model(BongoCatSessionState *session,
    const char *model_id) {
    if (!session || !model_id) return false;
    for (size_t i = 0; i < session->additional_model_count; ++i) {
        if (strcmp(session->additional_model_ids[i], model_id)) continue;
        if (i + 1 < session->additional_model_count)
            memmove(&session->additional_model_ids[i],
                &session->additional_model_ids[i + 1],
                (session->additional_model_count - i - 1) *
                sizeof(session->additional_model_ids[0]));
        memset(&session->additional_model_ids[
            --session->additional_model_count], 0,
            sizeof(session->additional_model_ids[0]));
        return true;
    }
    return false;
}

void bongo_cat_session_clear_additional_models(BongoCatSessionState *session) {
    if (!session) return;
    memset(session->additional_model_ids, 0,
        sizeof(session->additional_model_ids));
    session->additional_model_count = 0;
}
