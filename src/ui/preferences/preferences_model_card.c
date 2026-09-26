#include "preferences_model_card.h"
#include "preferences_model_card_paint.h"
#include "preferences_model_cover.h"
#include "preferences_notice.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_icons.h"
#include "ui_paint.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "bongo_cat/platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MODEL_SELECTION_FADE_MS 240.0f

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static void text(struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, struct nk_color color,
    const struct nk_user_font *font) {
    if (!font) font = context->style.font;
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

bool bongo_cat_preferences_model_import_card(BongoCatPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bool pointer_hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    bool hover = pointer_hover || value->import_drop_active;
    float lift = bongo_cat_ui_animate_eased(context, "model-import-hover",
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_STANDARD);
    bounds.y -= 2.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (hover && p.effects) bongo_cat_ui_paint_shadow(context, bounds, 14,
        0, 8, 24, 0, nk_rgba(p.pink.r, p.pink.g, p.pink.b, 89));
    nk_fill_rect(canvas, bounds, 14, hover ? p.hover_pink : p.hover);
    bongo_cat_ui_paint_dashed_rounded(context, bounds, 14, 2, 5, 5,
        hover ? p.pink : p.accent);
    uint64_t import_started = 0;
    bool importing = bongo_cat_preferences_import_status(value->import_dialog,
        &import_started, NULL, NULL);
    if (importing && import_started) {
        const double lap_ns = 5000000000.0;
        double elapsed = (double)(SDL_GetTicksNS() - import_started);
        float progress = (float)fmod(elapsed / lap_ns, 1.0);
        struct nk_rect outline =
            bongo_cat_preferences_model_card_outline_bounds(bounds, 2.0f);
        bongo_cat_preferences_model_card_draw_progress(canvas, outline,
            13.0f, 2.0f, progress, p.pink);
        value->render_dirty = true;
    }
    float cx = bounds.x + bounds.w * .5f, cy = bounds.y + 91;
    bongo_cat_preferences_icon_draw(value, canvas,
        BONGO_CAT_UI_ICON_UPLOAD, nk_rect(cx - 20, cy - 20, 40, 40), p.pink);
    const char *label = tr(value->app,
        "pages.preference.model.hints.clickOrDragToImport",
        "Click or drag a model folder");
    float width = value->ui.caption_font->width(value->ui.caption_font->userdata,
        value->ui.caption_font->height, label, nk_strlen(label));
    text(context, canvas, nk_rect(cx - width * .5f, cy + 25,
        NK_MIN(width + 1, bounds.w - 20), 24), label, p.accent,
        value->ui.caption_font);
    if (pointer_hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    return pointer_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

static void action_icon(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect bounds, int icon,
    struct nk_color color) {
    bongo_cat_preferences_icon_draw(value, canvas, icon,
        nk_rect(bounds.x + (bounds.w - 18) * .5f,
            bounds.y + (bounds.h - 18) * .5f, 18, 18), color);
}

void bongo_cat_preferences_model_select(BongoCatPreferences *value,
    const BongoCatModelEntry *entry) {
    if (!value || !entry) return;
    if (value->model_loading && !strcmp(entry->id, value->loading_model_id))
        return;
    bool multiple = value->app->settings.model.multiple_pets;
    bool selected = bongo_cat_app_model_active(value->app, entry->id);
    if (multiple && !selected && bongo_cat_app_active_model_count(value->app) >=
            BONGO_CAT_ACTIVE_MODEL_CAP) {
        bongo_cat_preferences_notice_show(value->app, tr(value->app,
            "native.multiplePetsLimit",
            "Up to 8 desktop pets can be displayed at once"), true);
        return;
    }
    snprintf(value->pending_model_id, sizeof(value->pending_model_id),
        "%s", entry->id);
    value->pending_model_multiple = multiple;
    value->pending_model_active = !selected;
    if (!multiple && strcmp(entry->id, value->app->session.active_model_id))
        bongo_cat_preferences_model_visual_begin(value, entry->id);
    value->model_selection_pending = true;
    value->model_load_progress = 0.0f;
    bongo_cat_preferences_invalidate(value);
}

static void open_model_directory(BongoCatPreferences *value,
    const BongoCatModelEntry *entry) {
    const char *directory = entry->storage_directory[0] ?
        entry->storage_directory : entry->directory;
    if (bongo_cat_path_is_dir(directory) &&
        bongo_cat_platform_open_directory(directory)) return;
    bongo_cat_preferences_notice_show(value->app, tr(value->app,
        "pages.preference.model.hints.openDirectoryFailed",
        "Unable to open model directory"), true);
}

static void draw_cover(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect preview,
    const BongoCatModelEntry *entry) {
    float raster_scale = value->ui.raster_scale > 0.0f ?
        value->ui.raster_scale : 1.0f;
    const BongoCatModelCover *cover = bongo_cat_preferences_model_cover(
        value->app, entry, NK_MAX(1, (int)lroundf(preview.w * raster_scale)),
        NK_MAX(1, (int)lroundf(preview.h * raster_scale)));
    if (!cover) return;
    float scale = NK_MIN(preview.w / cover->width, preview.h / cover->height);
    struct nk_rect image = nk_rect(
        preview.x + (preview.w - cover->width * scale) * .5f,
        preview.y + (preview.h - cover->height * scale) * .5f,
        cover->width * scale, cover->height * scale);
    struct nk_image texture = nk_image_id((int)cover->texture);
    nk_draw_image(canvas, image, &texture, nk_rgb(255, 255, 255));
}

#define MODEL_HIDE_TAB_RADIUS 40.0f
#define MODEL_CARD_ROUNDING 14.0f

/* Quarter-circle pocket in the top-right corner, clipped to the card's
   rounded corner: it follows the top edge, wraps around the card rounding
   and sweeps back along the right edge. Translucent gray to hide, pink to
   restore while viewing hidden models. Returns hover state. */
static bool draw_hide_tab(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const BongoCatModelEntry *entry,
    BongoCatUIPalette p, bool hidden_view) {
    const float pi = 3.14159265358979323846f;
    const int arc_steps = 8;
    float radius = MODEL_HIDE_TAB_RADIUS;
    float cx = bounds.x + bounds.w;
    float cy = bounds.y;
    float ox = cx - MODEL_CARD_ROUNDING;
    float oy = cy + MODEL_CARD_ROUNDING;
    struct nk_rect hit = nk_rect(cx - radius, cy, radius, radius);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, hit);
    char animation_id[BONGO_CAT_ID_CAP + 32];
    snprintf(animation_id, sizeof(animation_id), "model-hide-tab-%s",
        entry->id);
    float amount = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 150.0f, BONGO_CAT_UI_EASE_STANDARD);
    struct nk_color fill = hidden_view ?
        bongo_cat_ui_color_alpha(p.pink, .55f + .2f * amount) :
        bongo_cat_ui_color_alpha(p.muted, .35f + .2f * amount);
    float pocket[2 * 22];
    int count = 0;
    pocket[count++] = cx - radius; /* top edge */
    pocket[count++] = cy;
    pocket[count++] = ox;          /* card rounding start */
    pocket[count++] = cy;
    for (int i = 1; i < arc_steps; ++i) {
        float t = -pi * .5f + pi * .5f * (float)i / (float)arc_steps;
        pocket[count++] = ox + cosf(t) * MODEL_CARD_ROUNDING;
        pocket[count++] = oy + sinf(t) * MODEL_CARD_ROUNDING;
    }
    pocket[count++] = cx;          /* card rounding end, on the right edge */
    pocket[count++] = oy;
    pocket[count++] = cx;          /* right edge */
    pocket[count++] = cy + radius;
    for (int i = 1; i <= arc_steps; ++i) { /* pocket arc back to the start */
        float t = pi * .5f + pi * .5f * (float)i / (float)arc_steps;
        pocket[count++] = cx + cosf(t) * radius;
        pocket[count++] = cy + sinf(t) * radius;
    }
    nk_fill_polygon(canvas, pocket, count / 2, fill);
    int icon = hidden_view ? BONGO_CAT_UI_ICON_SYNC : BONGO_CAT_UI_ICON_CLOSE;
    float icon_size = radius * .4f;
    float inset = radius * .38f;
    bongo_cat_preferences_icon_draw(value, canvas, icon,
        nk_rect(cx - inset - icon_size * .5f, cy + inset - icon_size * .5f,
            icon_size, icon_size),
        bongo_cat_ui_color_alpha(nk_rgb(255, 255, 255), .8f + .2f * amount));
    if (hover && nk_input_is_mouse_click_in_rect(&context->input,
            NK_BUTTON_LEFT, hit)) {
        bool hidden = bongo_cat_settings_model_hidden(
            &value->app->settings, entry->id);
        bongo_cat_settings_set_model_hidden(&value->app->settings,
            entry->id, !hidden);
        value->render_dirty = true;
    }
    if (hover) {
        bongo_cat_ui_cursor_hover_rect(context, hit,
            BONGO_CAT_UI_CURSOR_POINTER);
        value->render_dirty = true;
    }
    return hover;
}

static void draw_actions(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const BongoCatModelEntry *entry,
    BongoCatUIPalette p, bool storage_busy,
    bool *action_hover) {
    struct nk_rect actions = nk_rect(bounds.x + 1, bounds.y + bounds.h - 39,
        bounds.w - 2, 38);
    nk_fill_rect(canvas, actions, 0, p.surface);
    bool deletable = !entry->managed;
    bool delete_enabled = deletable && !storage_busy;
    float width = actions.w / (deletable ? 3.0f : 2.0f);
    struct nk_rect items[3] = {
        nk_rect(actions.x, actions.y, width, actions.h),
        nk_rect(actions.x + width, actions.y, width, actions.h),
        nk_rect(actions.x + width * 2, actions.y, width, actions.h)};
    bool region_hover[3] = {
        nk_input_is_mouse_hovering_rect(&context->input, items[0]),
        nk_input_is_mouse_hovering_rect(&context->input, items[1]),
        deletable && nk_input_is_mouse_hovering_rect(
            &context->input, items[2])};
    bool hover[3] = {region_hover[0],
        region_hover[1], delete_enabled && region_hover[2]};
    for (int i = 1; i < (deletable ? 3 : 2); ++i)
        nk_stroke_line(canvas, items[i].x, items[i].y + 10,
            items[i].x, items[i].y + items[i].h - 10, 1, p.border_subtle);
    for (int i = 0; i < 3; ++i)
        if (hover[i]) nk_fill_rect(canvas, items[i], 0, p.hover_pink);
    action_icon(value, canvas, items[0], BONGO_CAT_UI_ICON_SMILE,
        hover[0] ? p.pink : p.muted);
    action_icon(value, canvas, items[1], BONGO_CAT_UI_ICON_FOLDER,
        hover[1] ? p.pink : p.muted);
    if (deletable) action_icon(value, canvas, items[2], BONGO_CAT_UI_ICON_TRASH,
        hover[2] ? p.danger : p.muted);
    *action_hover = region_hover[0] || region_hover[1] || region_hover[2];
    if (hover[0] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[0])) {
        bongo_cat_preferences_behavior_dialog_open_model(value, entry);
    } else if (hover[1] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[1])) open_model_directory(value, entry);
    else if (hover[2] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[2]))
        bongo_cat_preferences_remove_dialog_open(value->app, entry->id);
}

void bongo_cat_preferences_model_card(BongoCatPreferences *value,
    struct nk_context *context, const BongoCatModelEntry *entry,
    bool storage_busy) {
    BongoCatApp *app = value->app;
    bool selected = bongo_cat_app_model_active(app, entry->id);
    bool visual_target = value->model_load_visual_active &&
        !strcmp(entry->id, value->model_load_visual_id);
    bool selected_visual = selected && !visual_target;
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char animation_id[BONGO_CAT_ID_CAP + 24];
    snprintf(animation_id, sizeof(animation_id), "model-hover-%s", entry->id);
    float lift = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_SWIFT);
    char selection_animation_id[BONGO_CAT_ID_CAP + 28];
    snprintf(selection_animation_id, sizeof(selection_animation_id),
        "model-selected-%s", entry->id);
    float selection_amount = bongo_cat_ui_animate_eased(context,
        selection_animation_id, selected_visual ? 1.0f : 0.0f,
        MODEL_SELECTION_FADE_MS, BONGO_CAT_UI_EASE_STANDARD);
    bounds.y -= 3.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (selection_amount > .001f && p.effects)
        bongo_cat_ui_paint_shadow(context, bounds, 14, 0, 8, 20, 0,
            bongo_cat_ui_color_alpha(nk_rgba(p.pink.r, p.pink.g,
                p.pink.b, 89), selection_amount));
    else if (!selected_visual && hover && p.effects)
        bongo_cat_ui_paint_shadow(context, bounds, 14, 0, 10, 28, 0,
            nk_rgba(p.accent.r, p.accent.g, p.accent.b, 46));
    nk_fill_rect(canvas, bounds, 14, p.surface);
    float preview_height = NK_MIN(128.0f, bounds.w * 354.0f / 612.0f);
    struct nk_rect preview = nk_rect(bounds.x + 1, bounds.y + 1,
        bounds.w - 2, preview_height);
    nk_fill_rect(canvas, preview, 12, p.surface);
    draw_cover(value, canvas, preview, entry);
    if (app->settings.model.multiple_pets)
        bongo_cat_preferences_model_card_draw_selection_badge(
            value, canvas, preview, p, selection_amount);
    float info_y = preview.y + preview.h + 8;
    struct nk_rect name_bounds = nk_rect(bounds.x + 9, info_y + 1,
        bounds.w - 18, 30);
    bool name_hover = bongo_cat_preferences_model_name_draw(value,
        context, canvas, entry, name_bounds, p);
    bool action_hover = false;
    draw_actions(value, context, canvas, bounds, entry, p,
        storage_busy, &action_hover);
    float outline_width = 1.0f + selection_amount;
    struct nk_rect outline = bongo_cat_preferences_model_card_outline_bounds(
        bounds, outline_width);
    float progress = visual_target ?
        bongo_cat_preferences_model_visual_progress(value, entry->id) :
        selected_visual ? 1.0f : 0.0f;
    struct nk_color outline_color = progress >= .999f ? p.pink :
        bongo_cat_ui_color_mix(
            bongo_cat_ui_color_mix(p.border_subtle, p.accent, lift),
            p.pink, selection_amount);
    /* The pocket is drawn before the outline so the border line stays
       visible along its flush edges. */
    bool tab_hover = draw_hide_tab(value, context, canvas, bounds, entry, p,
        value->model_show_hidden);
    nk_stroke_rect(canvas, outline, 14.0f - outline_width * .5f,
        outline_width, outline_color);
    bongo_cat_preferences_model_card_draw_progress(canvas, outline,
        14.0f - outline_width * .5f, 2.0f, progress, p.pink);
    if (!name_hover && (action_hover || hover))
        bongo_cat_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_UI_CURSOR_POINTER);
    if (!name_hover && !action_hover && !tab_hover && hover &&
        nk_input_is_mouse_click_in_rect(&context->input,
            NK_BUTTON_LEFT, bounds))
        bongo_cat_preferences_model_select(value, entry);
}
