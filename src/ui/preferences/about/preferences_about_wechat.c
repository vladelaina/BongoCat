#include "preferences_state.h"
#include "preferences_about_internal.h"
#include "preferences_about_svg.h"
#include "ui_catime.h"
#include "ui_paint.h"
#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

void bongo_cat_about_wechat_icon(BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds) {
    BongoCatAboutState *s = &value->about;
    if (!s->wechat_icon_attempted) {
        s->wechat_icon_attempted = true;
        float scale = NK_CLAMP(1.0f, value->ui.raster_scale, 4.0f);
        int size = (int)(25.0f * scale + .5f);
        unsigned char *pixels = bongo_cat_about_wechat_pixels(size);
        if (pixels) {
            glGenTextures(1, &s->wechat_icon_texture);
            glBindTexture(GL_TEXTURE_2D, s->wechat_icon_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            free(pixels);
        }
    }
    if (s->wechat_icon_texture) {
        struct nk_image icon = nk_image_id((int)s->wechat_icon_texture);
        nk_draw_image(nk_window_get_canvas(context), bounds, &icon, nk_rgb(255, 255, 255));
    }
}

void bongo_cat_about_wechat(BongoCatPreferences *value, struct nk_rect anchor) {
    BongoCatAboutState *state = &value->about;
    state->qr_open = true;
    state->qr_anchor = anchor;
    state->qr_hide_at = SDL_GetTicks() + 250;
    bongo_cat_about_refresh(value);
}

void bongo_cat_about_overlays(BongoCatPreferences *value, struct nk_context *context) {
    bongo_cat_about_refresh(value);
    BongoCatAboutState *s = &value->about;
    if (!s->qr_open || value->page != 3)
        return;
    if (s->qr_texture_dirty) {
        if (s->qr_texture) glDeleteTextures(1, &s->qr_texture);
        s->qr_texture = 0;
        s->qr_texture_dirty = false;
    }
    if (!s->qr_texture && s->qr_pixels) {
        glGenTextures(1, &s->qr_texture);
        glBindTexture(GL_TEXTURE_2D, s->qr_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 240, 240, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, s->qr_pixels);
    }
    float width, height;
    bongo_cat_ui_logical_size(&value->ui, &width, &height);
    struct nk_rect anchor = s->qr_anchor;
    float size = NK_MIN(200.0f, NK_MIN(width - 24.0f, height - 24.0f));
    bool above = anchor.y >= size + 22.0f;
    float y = above ? anchor.y - size - 10.0f : anchor.y + anchor.h + 10.0f;
    struct nk_rect panel = nk_rect(NK_CLAMP(12, anchor.x + anchor.w * .5f - size * .5f, width - size - 12),
                                   NK_CLAMP(12, y, height - size - 12), size, size);
    bool over = nk_input_is_mouse_hovering_rect(&context->input, panel);
    if (over)
        s->qr_hide_at = SDL_GetTicks() + 250;
    if (SDL_GetTicks() >= s->qr_hide_at) {
        s->qr_open = false;
        return;
    }
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_push_scissor(canvas, nk_rect(0, 0, width, height));
    if (bongo_cat_ui_palette(bongo_cat_ui_dark(context)).effects)
        bongo_cat_ui_paint_shadow(context, panel, 10, 0, 2, 6, 0, nk_rgba(0, 0, 0, 8));
    nk_fill_rect(canvas, panel, 10, nk_rgb(255, 255, 255));
    nk_stroke_rect(canvas, panel, 10, 1, nk_rgba(0, 0, 0, 10));
    float arrow_x = NK_CLAMP(panel.x + 16, anchor.x + anchor.w * .5f, panel.x + panel.w - 16);
    float edge = above ? panel.y + panel.h : panel.y;
    float tip = edge + (above ? 6.0f : -6.0f);
    nk_fill_triangle(canvas, arrow_x - 5, edge, arrow_x + 5, edge, arrow_x, tip,
        nk_rgb(255, 255, 255));
    if (s->qr_texture) {
        struct nk_image image = nk_image_id((int)s->qr_texture);
        nk_draw_image(canvas, nk_rect(panel.x + 4, panel.y + 4, size - 8, size - 8), &image,
                      nk_rgb(255, 255, 255));
    } else {
        bongo_cat_about_text(value, context, nk_rect(panel.x + 16, panel.y + size * .3f, size - 32, 52),
                             s->qr_request ? "native.about.loading" : "native.about.qrUnavailable",
                             s->qr_request ? "Loading..." : "QR code unavailable",
                             bongo_cat_about_font(value, 13), nk_rgb(85, 85, 85), true);
        if (!s->qr_request) {
            struct nk_rect retry = nk_rect(panel.x + (size - 88) * .5f, panel.y + size * .55f, 88, 30);
            nk_fill_rect(canvas, retry, 6, nk_rgb(7, 193, 96));
            bongo_cat_about_text(value, context, nk_rect(retry.x, retry.y + 8, retry.w, 18),
                "native.about.reload", "Reload", bongo_cat_about_font(value, 12), nk_rgb(255, 255, 255), true);
            bongo_cat_ui_cursor_hover_rect(context, retry, BONGO_CAT_UI_CURSOR_POINTER);
            if (over && nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, retry)) {
                s->qr_attempted = false;
                bongo_cat_about_wechat(value, s->qr_anchor);
            }
        }
    }
}
