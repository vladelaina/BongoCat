#include "preferences_state.h"
#include "preferences_model_card.h"
#include "preferences_model_cover.h"
#include "preferences_notice.h"
#include "preferences_widgets.h"
#include "model_import.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/log.h"

#include <stdio.h>
#include <string.h>

#define MODEL_CARD_HEIGHT 214
#define MODEL_LOAD_RENDER_INTERVAL_NS 100000000ull

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static float visual_wait_progress(uint64_t elapsed) {
    if (elapsed <= BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS)
        return .8f * (float)((double)elapsed /
            BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS);
    uint64_t slow_elapsed = elapsed - BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    uint64_t slow_duration = BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS -
        BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    float slow = slow_duration ? (float)((double)slow_elapsed / slow_duration) : 1.0f;
    return .8f + (BONGO_CAT_MODEL_LOAD_VISUAL_WAIT_CAP - .8f) *
        NK_CLAMP(0.0f, slow, 1.0f);
}

void bongo_cat_preferences_model_visual_begin(BongoCatPreferences *value,
    const char *model_id) {
    if (!value || !model_id || !model_id[0]) return;
    value->model_load_visual_active = true;
    value->model_load_visual_started_ns = SDL_GetTicksNS();
    value->model_load_visual_completion_ns = 0;
    value->model_load_progress = 0.0f;
    snprintf(value->model_load_visual_id,
        sizeof(value->model_load_visual_id), "%s", model_id);
}

bool bongo_cat_preferences_model_texture_busy(const BongoCatPreferences *value) {
    return value && value->app && value->app->loading_model[0] &&
        value->app->model_load_runtime_stage == 2;
}

void bongo_cat_preferences_model_load_progress(BongoCatPreferences *value,
    float progress) {
    if (!value || !value->model_loading) return;
    uint64_t now = SDL_GetTicksNS();
    uint64_t elapsed = now - value->model_load_visual_started_ns;
    value->model_load_progress = visual_wait_progress(elapsed);
    value->render_dirty = true;
    /* Preserve the loading frame while transferring atlases. Switching to
       the shared settings context can serialize a software/virtual GPU and
       cost more than decoding. Native events still pump in the loader. */
    if (bongo_cat_preferences_model_texture_busy(value)) {
        ++value->model_load_render_deferred;
        return;
    }
    /* Slow software/virtual GPU presentation must not dominate model decode.
       Keep 10 Hz on fast drivers, backing off to at most 500 ms between
       loading frames; native events are pumped independently by the loader. */
    uint64_t interval = SDL_max(MODEL_LOAD_RENDER_INTERVAL_NS,
        SDL_min(value->model_load_render_cost_ns, 125000000ull) * 4);
    bool due = !value->model_load_render_ns ||
        now - value->model_load_render_ns >= interval;
    if (bongo_cat_preferences_visible(value) && due) {
        value->model_load_render_progress = progress;
        value->model_load_render_ns = now;
        bongo_cat_preferences_render(value);
        /* Expensive UI frames must not consume every decode callback. Count
           the interval from completion, including context swaps/presentation. */
        value->model_load_render_ns = SDL_GetTicksNS();
        value->model_load_render_cost_ns = value->model_load_render_ns - now;
        value->model_load_render_total_ns += value->model_load_render_cost_ns;
        ++value->model_load_render_count;
    }
}

float bongo_cat_preferences_model_visual_progress(BongoCatPreferences *value,
    const char *model_id) {
    if (!value || !model_id || !value->model_load_visual_active ||
        strcmp(value->model_load_visual_id, model_id)) return 0.0f;
    uint64_t now = SDL_GetTicksNS();
    uint64_t elapsed = now - value->model_load_visual_started_ns;
    if (!value->model_loading && !value->model_selection_pending) {
        if (!value->model_load_visual_completion_ns)
            value->model_load_visual_completion_ns = now;
        uint64_t completed = now - value->model_load_visual_completion_ns;
        float amount = NK_CLAMP(0.0f, (float)((double)completed /
            BONGO_CAT_MODEL_LOAD_VISUAL_COMPLETE_NS), 1.0f);
        float start = visual_wait_progress(elapsed);
        value->model_load_progress = start + (1.0f - start) * amount;
        if (amount >= 1.0f) {
            value->model_load_visual_active = false;
            value->model_load_visual_completion_ns = 0;
            value->model_load_progress = 1.0f;
        }
        value->render_dirty = true;
        return value->model_load_progress;
    }
    value->model_load_progress = visual_wait_progress(elapsed);
    value->render_dirty = true;
    return value->model_load_progress;
}

static void finish_model_load_progress(BongoCatPreferences *value) {
    if (!value) return;
    if (value->model_load_visual_active) {
        uint64_t now = SDL_GetTicksNS();
        value->model_load_visual_completion_ns = now;
        value->model_load_progress = visual_wait_progress(now -
            value->model_load_visual_started_ns);
    } else value->model_load_progress = 1.0f;
    value->render_dirty = true;
}

void bongo_cat_preferences_process_model_selection(BongoCatPreferences *value) {
    if (!value || !value->model_selection_pending || value->model_loading)
        return;
    char id[BONGO_CAT_ID_CAP];
    snprintf(id, sizeof(id), "%s", value->pending_model_id);
    bool multiple = value->pending_model_multiple;
    bool active = value->pending_model_active;
    value->pending_model_id[0] = '\0';
    value->model_selection_pending = false;
    value->pending_model_multiple = false;
    value->model_loading = true;
    value->model_load_progress = 0.0f;
    value->model_load_render_progress = 0.0f;
    value->model_load_render_ns = 0;
    value->model_load_render_cost_ns = 0;
    value->model_load_render_total_ns = 0;
    value->model_load_render_count = 0;
    value->model_load_render_deferred = 0;
    if (!value->model_load_visual_active || strcmp(value->model_load_visual_id, id))
        bongo_cat_preferences_model_visual_begin(value, id);
    snprintf(value->loading_model_id, sizeof(value->loading_model_id), "%s", id);
    BongoCatError error = {0};
    bongo_cat_preferences_resource_note(value, "before-model-switch");
    bool selected = multiple ? bongo_cat_app_set_model_active(
        value->app, id, active, &error) :
        bongo_cat_app_select_model_with_error(value->app, id, &error);
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[model-switch-ui] success=%d frames=%u total_ms=%.1f deferred=%u",
        selected, value->model_load_render_count,
        (double)value->model_load_render_total_ns / 1000000.0,
        value->model_load_render_deferred);
    if (selected) finish_model_load_progress(value);
    else {
        value->model_load_progress = 0.0f;
        value->model_load_visual_active = false;
        value->model_load_visual_id[0] = '\0';
    }
    value->model_loading = false;
    value->loading_model_id[0] = '\0';
    if (!selected) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "Model selection failed: id=%s error=%s", id,
        error.message[0] ? error.message : "Unable to display this model");
    bongo_cat_preferences_invalidate(value);
    /* Render on the next input frame. A nested render here would replay the
       card click that queued this selection and immediately toggle it back. */
}

static void smoke_model_behavior(BongoCatPreferences *value) {
    BongoCatApp *app = value->app;
    if (app->smoke_preference_model_select) {
        for (size_t i = 0; i < app->models.count; ++i) {
            const BongoCatModelEntry *entry = &app->models.entries[i];
            if (entry->preset || !strcmp(entry->id,
                app->session.active_model_id)) continue;
            app->smoke_preference_model_select = false;
            value->smoke_behavior_open_pending = true;
            SDL_Log("Preferences smoke selecting model %s", entry->id);
            bongo_cat_preferences_model_select(value, entry);
            return;
        }
    }
    if (value->smoke_behavior_open_pending && !value->font_reload_pending &&
        !value->model_selection_pending && !value->model_loading) {
        value->smoke_behavior_open_pending = false;
        bongo_cat_preferences_behavior_dialog_open(value);
    }
}

static bool model_hidden(const BongoCatPreferences *value, const char *id) {
    return bongo_cat_settings_model_hidden(&value->app->settings, id);
}

static size_t visible_model_count(const BongoCatPreferences *value,
    bool managed, bool hidden) {
    size_t count = 0;
    for (size_t i = 0; i < value->app->models.count; ++i) {
        const BongoCatModelEntry *entry = &value->app->models.entries[i];
        if (entry->managed == managed &&
            model_hidden(value, entry->id) == hidden) count++;
    }
    return count;
}

static int preset_model_order(const BongoCatModelEntry *entry) {
    if (!entry || !entry->preset) return 3;
    if (!strcmp(entry->id, "standard")) return 0;
    if (!strcmp(entry->id, "keyboard")) return 1;
    if (!strcmp(entry->id, "gamepad")) return 2;
    return 3;
}

static void draw_models(BongoCatPreferences *value,
    struct nk_context *context, bool managed, bool storage_busy, bool hidden) {
    for (int order = 0; order <= 3; ++order) {
        for (size_t i = 0; i < value->app->models.count; ++i) {
            const BongoCatModelEntry *entry = &value->app->models.entries[i];
            if (entry->managed != managed || preset_model_order(entry) != order)
                continue;
            if (model_hidden(value, entry->id) != hidden) continue;
            bongo_cat_preferences_model_card(value, context, entry,
                storage_busy);
        }
    }
}

void bongo_cat_preferences_page_model(BongoCatPreferences *value,
    struct nk_context *context) {
    BongoCatApp *app = value->app;
    smoke_model_behavior(value);
    bool show_hidden = value->model_show_hidden &&
        (visible_model_count(value, false, true) ||
        visible_model_count(value, true, true));
    value->model_show_hidden = show_hidden;
    bongo_cat_preferences_model_covers_begin(app);
    bool multiple = app->settings.model.multiple_pets;
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MULTIPLE_MODELS);
    if (bongo_cat_pref_toggle(context, "multiple-pets", tr(app,
            "native.multiplePets",
            "Display multiple"), "", &multiple))
        bongo_cat_app_set_multiple_pets(app, multiple);
    if (bongo_cat_preferences_model_section(value, context) &&
        !SDL_OpenURL("https://bongocat.pet/models"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot open model library: %s", SDL_GetError());
    float width = nk_window_get_content_region(context).w;
    int columns = width >= 780 ? 4 : width >= 620 ? 3 : width >= 400 ? 2 : 1;
    struct nk_vec2 old_spacing = context->style.window.spacing;
    context->style.window.spacing = nk_vec2(14, 17);
    nk_layout_row_dynamic(context, MODEL_CARD_HEIGHT, columns);
    if (bongo_cat_preferences_model_import_card(value, context))
        bongo_cat_preferences_request_model_import(app->preferences);
    bool storage_busy = bongo_cat_preferences_import_status(
        value->import_dialog, NULL, NULL, NULL) ||
        bongo_cat_app_model_refresh_busy(app);
    draw_models(value, context, false, storage_busy, show_hidden);
    if (visible_model_count(value, true, show_hidden)) {
        bongo_cat_pref_section(context,
            tr(app, "pages.preference.model.nearbyTitle", "Nearby models"));
        nk_layout_row_dynamic(context, MODEL_CARD_HEIGHT, columns);
        draw_models(value, context, true, storage_busy, show_hidden);
    }
    context->style.window.spacing = old_spacing;
    bongo_cat_preferences_model_covers_prune(app);
}
