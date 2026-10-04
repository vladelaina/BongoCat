#include "preferences_state.h"
#include "preferences_model_glyphs.h"
#include "preferences_text_edit.h"

#include <SDL3/SDL.h>
#include <string.h>

bool bongo_cat_preferences_model_name_draw(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    const BongoCatModelEntry *entry, struct nk_rect bounds,
    BongoCatUIPalette p) {
    BongoCatPreferencesTextSession *session = &value->model_rename;
    bool renaming = !strcmp(session->id, entry->id);
    bool hover = !renaming && nk_input_is_mouse_hovering_rect(
        &context->input, bounds);
    bool clicked = hover && nk_input_is_mouse_click_in_rect(
        &context->input, NK_BUTTON_LEFT, bounds);
    if (renaming || hover) {
        nk_fill_rect(canvas, bounds, 7, renaming ? p.hover_pink : p.hover);
        nk_stroke_rect(canvas, bounds, 7, 1, renaming ? p.pink : p.accent);
    }
    const char *shown = renaming ? session->text :
        bongo_cat_model_name(&value->app->settings, entry);
    /* Page changes and catalog refreshes can draw before the atlas reload. */
    bool name_pending = !renaming && value->font_reload_pending &&
        !bongo_cat_preferences_model_glyphs_ready(value, shown);
    if (name_pending)
        shown = value->import_render_active ?
            bongo_cat_i18n_get(value->app->i18n, "native.importing",
                "Importing...") : "...";
    struct nk_rect text_bounds = nk_rect(bounds.x + 5,
        bounds.y + 3, bounds.w - 10, 22);
    if (renaming && session->select_all)
        nk_fill_rect(canvas, text_bounds, 4, p.selection);
    nk_draw_text(canvas, text_bounds, shown, nk_strlen(shown),
        value->ui.label_font, nk_rgba(0, 0, 0, 0),
        renaming ? p.pink : p.text);
    if (renaming) {
        session->bounds = bounds;
        float caret = value->ui.label_font->width(
            value->ui.label_font->userdata, value->ui.label_font->height,
            shown, (int)session->cursor);
        caret = NK_MIN(caret, bounds.w - 12);
        if (!session->select_all)
            nk_stroke_line(canvas, bounds.x + 5 + caret, bounds.y + 3,
                bounds.x + 5 + caret, bounds.y + bounds.h - 3, 1, p.pink);
    } else if (clicked && !name_pending)
        bongo_cat_preferences_model_rename_begin(value, entry, bounds);
    if (hover || renaming) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_TEXT);
    return renaming || hover;
}

void bongo_cat_preferences_model_rename_finish(
    BongoCatPreferences *value, bool save) {
    if (!value || !bongo_cat_preferences_text_session_active(
        &value->model_rename)) return;
    BongoCatPreferencesTextSession *session = &value->model_rename;
    bool label_changed = false;
    if (save) {
        bongo_cat_text_edit_trim(session->text);
        const BongoCatModelEntry *entry = bongo_cat_models_find(
            &value->app->models, session->id);
        const char *label = entry && !strcmp(session->text,
            bongo_cat_model_default_name(entry)) ? "" : session->text;
        label_changed = bongo_cat_settings_set_model_label(&value->app->settings,
            session->id, label);
    }
    bongo_cat_preferences_text_session_reset(session);
    SDL_StopTextInput(value->window);
    if (label_changed) bongo_cat_preferences_reload_fonts(value);
    value->render_dirty = true;
}

void bongo_cat_preferences_model_rename_begin(BongoCatPreferences *value,
    const BongoCatModelEntry *entry, struct nk_rect bounds) {
    if (!value || !entry) return;
    bongo_cat_preferences_shortcut_cancel(value);
    bongo_cat_preferences_text_session_begin(&value->model_rename, entry->id,
        bongo_cat_model_name(&value->app->settings, entry), bounds);
    SDL_StartTextInput(value->window);
    value->render_dirty = true;
}

bool bongo_cat_preferences_model_rename_event(
    BongoCatPreferences *value, const SDL_Event *event) {
    if (!value) return false;
    BongoCatPreferencesTextSessionEvent result =
        bongo_cat_preferences_text_session_event(&value->model_rename,
            event, value->ui.layout_scale, value->ui.label_font, 5.0f);
    if (result.finish)
        bongo_cat_preferences_model_rename_finish(value, result.save);
    if (result.handled) value->render_dirty = true;
    return result.handled;
}
