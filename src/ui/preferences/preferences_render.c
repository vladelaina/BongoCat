#include "preferences_state.h"
#include "preferences_render_internal.h"
#include "preferences_page_dispatch.h"
#include "preferences_controls.h"
#include "preferences_notice.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "ui_icons.h"
#include "ui_paint.h"
#include "bongo_cat/memory_policy.h"
#include "bongo_cat/tray.h"
#include "update_service.h"
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
typedef struct RootStyle { struct nk_vec2 padding, group_padding, spacing;
    struct nk_style_item fixed; struct nk_color background; float group_border; } RootStyle;
static const char *tr(const BongoCatPreferences *value, const char *key, const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}
static void draw_icon(void *userdata, struct nk_command_buffer *canvas, int icon,
    struct nk_rect bounds, struct nk_color color) {
    bongo_cat_preferences_icon_draw(userdata, canvas, icon, bounds, color); }
static RootStyle root_style_save(struct nk_context *context) {
    RootStyle saved;
    saved.padding = context->style.window.padding;
    saved.group_padding = context->style.window.group_padding;
    saved.spacing = context->style.window.spacing;
    saved.fixed = context->style.window.fixed_background;
    saved.background = context->style.window.background;
    saved.group_border = context->style.window.group_border;
    return saved;
}
static void root_style_apply(struct nk_context *context,
    BongoCatUIPalette palette, bool transparent) {
    context->style.window.padding = nk_vec2(BONGO_CAT_UI_MARGIN,
        BONGO_CAT_UI_MARGIN);
    context->style.window.group_padding = nk_vec2(0, 0);
    context->style.window.spacing = nk_vec2(0, 0);
    struct nk_color background = transparent ? nk_rgba(0, 0, 0, 0) :
        palette.background;
    context->style.window.fixed_background = nk_style_item_color(background);
    context->style.window.background = background;
    context->style.window.group_border = 0;
}
static void root_style_restore(struct nk_context *context,
    const RootStyle *saved) {
    context->style.window.padding = saved->padding;
    context->style.window.group_padding = saved->group_padding;
    context->style.window.spacing = saved->spacing;
    context->style.window.fixed_background = saved->fixed;
    context->style.window.background = saved->background;
    context->style.window.group_border = saved->group_border;
}
static bool draw_shell(BongoCatPreferences *value, struct nk_context *context,
    float width, float height, bool dark) {
    static const char *page_ids[] = {
        "page-settings", "page-model", "page-shortcuts", "page-about"};
    static const int menu_icons[] = {
        BONGO_CAT_UI_ICON_CAT, BONGO_CAT_UI_ICON_WAND,
        BONGO_CAT_UI_ICON_KEYBOARD, BONGO_CAT_UI_ICON_HEART};
    const char *menus[] = {
        tr(value, "pages.preference.cat.title", "Preferences"),
        tr(value, "pages.preference.model.title", "Model"),
        tr(value, "pages.preference.shortcut.title", "Shortcuts"),
        tr(value, "native.support.title", "Support the Developer")};
    bool modal = bongo_cat_preferences_remove_dialog_active(value->app) ||
        bongo_cat_preferences_behavior_dialog_active(value);
    const bool native_chrome = bongo_cat_ui_native_chrome(); BongoCatUIPalette p = bongo_cat_ui_palette(dark);
    bongo_cat_ui_shell_draw(context, width, height, dark,
        !value->transparent_window);
    float sidebar = bongo_cat_ui_sidebar_width(width);
    float interior_height = height - BONGO_CAT_UI_MARGIN * 2.0f;
    nk_layout_row_begin(context, NK_STATIC, interior_height, 2);
    nk_layout_row_push(context, sidebar);
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    context->style.window.fixed_background = nk_style_item_color(clear);
    context->style.window.background = clear;
    context->style.window.group_padding = nk_vec2(0, 0);
    bool title_clicked = false;
    if (nk_group_begin(context, "preferences-sidebar", NK_WINDOW_NO_SCROLLBAR)) {
    bongo_cat_ui_header(context, "BongoCat",
        value->ui.heading_font, value->logo_texture, &title_clicked, !modal, dark, native_chrome);
    if (title_clicked && !SDL_OpenURL("https://bongocat.pet"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot open website: %s", SDL_GetError());
    bongo_cat_ui_set_icons(draw_icon, value);
    BongoCatUpdateSnapshot update_snapshot;
    bongo_cat_update_snapshot(value->app->update, &update_snapshot);
    bool about_badge = update_snapshot.status == BONGO_CAT_UPDATE_AVAILABLE ||
        update_snapshot.status == BONGO_CAT_UPDATE_INSTALLED ||
        (update_snapshot.status == BONGO_CAT_UPDATE_ERROR &&
            update_snapshot.release.version[0]);
    bongo_cat_ui_tabs(context, menus, menu_icons, 4, &value->page,
        !modal, dark, interior_height, about_badge,
        draw_icon, value);
    if (!value->page_seen) {
        value->page_seen = true; value->last_page = value->page;
    } else if (value->last_page != value->page) {
        bongo_cat_preferences_page_cache_clear(value,
            value->last_page, value->page);
        value->last_page = value->page;
        value->page_transition_ns = SDL_GetTicksNS();
        value->render_dirty = true;
    }
    nk_group_end(context);
    }
    nk_layout_row_push(context, width - BONGO_CAT_UI_MARGIN * 2.0f - sidebar);
    bool close_requested = false;
    if (nk_group_begin(context, "preferences-content", NK_WINDOW_NO_SCROLLBAR)) {
    close_requested = bongo_cat_ui_content_header(context,
        menus[value->page], menu_icons[value->page], !modal, dark,
        native_chrome);
    if (close_requested) {
        /* Scrolled cards can overlap the header's hit area despite clipping. */
        nk_group_end(context);
        nk_layout_row_end(context);
        return true;
    }
    float body_height = interior_height - BONGO_CAT_UI_HEADER_HEIGHT;
    nk_layout_row_dynamic(context, NK_MAX(120.0f, body_height), 1);
    struct nk_rect body_bounds = nk_widget_bounds(context);
    float page_progress = 1.0f;
    if (value->page_transition_ns) {
        float elapsed = (float)(SDL_GetTicksNS() - value->page_transition_ns) /
            220000000.0f;
        if (elapsed >= 1.0f) value->page_transition_ns = 0;
        else {
            page_progress = bongo_cat_ui_ease(BONGO_CAT_UI_EASE_SWIFT,
                NK_CLAMP(0.0f, elapsed, 1.0f));
            value->render_dirty = true;
        }
    }
    context->style.window.group_padding = nk_vec2(24,
        16 + 6.0f * (1.0f - page_progress));
    context->style.window.spacing = nk_vec2(10, 10);
    int scroll_page = NK_CLAMP(0, value->page, 3);
    bool scroll_animating = fabsf(value->scroll_current[scroll_page] - value->scroll_target[scroll_page]) > .5f;
    struct nk_style_scrollbar saved_scrollv = context->style.scrollv;
    struct nk_rect scrollbar_hit = nk_rect(body_bounds.x + body_bounds.w - 14, body_bounds.y, 14, body_bounds.h);
    bool scrollbar_hover = !modal && nk_input_is_mouse_hovering_rect(&context->input, scrollbar_hit);
    context->style.scrollv.padding.x = scrollbar_hover ? 0.0f : 2.0f;
    context->style.scrollv.cursor_hover = nk_style_item_color(p.accent);
    context->style.scrollv.cursor_active = nk_style_item_color(p.accent);
    if (scroll_animating)
        context->style.scrollv.cursor_normal = nk_style_item_color(p.accent);
    if (value->scroll_ready[scroll_page])
        nk_group_set_scroll(context, page_ids[scroll_page], 0,
            (nk_uint)NK_MAX(0.0f, value->scroll_current[scroll_page]));
    struct nk_mouse_button saved_left = context->input.mouse.buttons[NK_BUTTON_LEFT];
    struct nk_vec2 saved_wheel = context->input.mouse.scroll_delta;
    if (modal) {
        context->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
        context->input.mouse.buttons[NK_BUTTON_LEFT].down = nk_false;
        context->input.mouse.scroll_delta = nk_vec2(0, 0);
    }
    if (nk_group_begin(context, page_ids[value->page], 0)) {
        bongo_cat_preferences_draw_page(value, context);
        float wheel = context->input.mouse.scroll_delta.y;
        struct nk_panel *layout = context->current->layout;
        float maximum = layout ? NK_MAX(0.0f, layout->at_y + layout->row.height - layout->bounds.y - layout->bounds.h) : 0.0f;
        if (wheel != 0.0f) context->style.scrollv.cursor_normal =
            nk_style_item_color(p.accent);
        context->input.mouse.scroll_delta.y = 0;
        nk_group_end(context);
        context->style.scrollv = saved_scrollv;
        nk_uint group_x = 0, group_y = 0;
        nk_group_get_scroll(context, page_ids[value->page], &group_x, &group_y);
        float actual = (float)group_y;
        if (!value->scroll_ready[scroll_page]) {
            value->scroll_ready[scroll_page] = true;
            value->scroll_current[scroll_page] = actual;
            value->scroll_target[scroll_page] = actual;
        }
        value->scroll_target[scroll_page] = NK_CLAMP(0.0f, value->scroll_target[scroll_page], maximum);
        if (wheel != 0.0f)
            value->scroll_target[scroll_page] = NK_CLAMP(0.0f,
                value->scroll_target[scroll_page] - wheel * 72.0f, maximum);
        else if (fabsf(actual - value->scroll_current[scroll_page]) > 2.0f)
            value->scroll_target[scroll_page] = NK_CLAMP(0.0f,
                actual, maximum);
        float blend = 1.0f - expf(-context->delta_time_seconds / .035f);
        float next = actual +
            (value->scroll_target[scroll_page] - actual) * blend;
        if (fabsf(next - value->scroll_target[scroll_page]) < .5f) next =
            value->scroll_target[scroll_page];
        value->scroll_current[scroll_page] = NK_CLAMP(0.0f, next, maximum);
        bool still_animating = fabsf(value->scroll_current[scroll_page] -
            value->scroll_target[scroll_page]) > .5f;
        if (wheel != 0.0f || still_animating ||
            (scroll_animating && !still_animating))
            value->render_dirty = true;
    } else context->style.scrollv = saved_scrollv;
    if (modal) {
        context->input.mouse.buttons[NK_BUTTON_LEFT] = saved_left;
        context->input.mouse.scroll_delta = saved_wheel;
    }
    if (page_progress < 1.0f) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(context);
        nk_fill_rect(canvas, body_bounds, 0, nk_rgba(p.surface.r,
            p.surface.g, p.surface.b,
            (nk_byte)(255 * (1.0f - page_progress))));
    }
    nk_group_end(context);
    }
    nk_layout_row_end(context);
    context->style.window.fixed_background = nk_style_item_color(p.surface);
    context->style.window.background = p.surface;
    bongo_cat_preferences_notice_draw(value, context, width, height);
    bongo_cat_preferences_remove_dialog_draw(value->app, context);
    bongo_cat_preferences_behavior_dialog_draw(value, context);
    bongo_cat_about_overlays(value, context);
    return close_requested;
}
bool bongo_cat_preferences_draw_frame(BongoCatPreferences *value,
    float width, float height,
    bool dark) {
    struct nk_context *context = &value->ui.context;
    RootStyle saved = root_style_save(context);
    BongoCatUIPalette palette = bongo_cat_ui_palette(dark);
    root_style_apply(context, palette, value->transparent_window);
    bool close_requested = false;
    if (nk_begin(context, BONGO_CAT_NAME,
        nk_rect(0, 0, (float)width, (float)height), NK_WINDOW_NO_SCROLLBAR))
        close_requested = draw_shell(value, context, width, height, dark);
    nk_end(context);
    root_style_restore(context, &saved);
    return close_requested;
}
