#include "dial_internal.h"
#include "ui_font.h"
#include "ui_font_atlas_internal.h"
#include <stdlib.h>
#include <string.h>

static bool contains(const uint8_t *used, nk_rune rune) {
    return (used[rune >> 3] & (1u << (rune & 7))) != 0;
}

static void collect(uint8_t *used, const char *s) {
    if (!s) return;
    int length = (int)strlen(s);
    while (length > 0) {
        nk_rune rune;
        int n = nk_utf_decode(s,&rune,length);
        if (n <= 0) break;
        if (rune >= 32 && rune < 0x110000)
            used[rune >> 3] |= (uint8_t)(1u << (rune & 7));
        s += n; length -= n;
    }
}

static nk_rune *ranges(Dial *d) {
    uint8_t *used = calloc(0x110000 / 8,1);
    if (!used) return NULL;
    for (int i = 32; i < 127; ++i)
        used[i >> 3] |= (uint8_t)(1u << (i & 7));
    for (int i = 0; i < d->count; ++i) collect(used,d->items[i].label);
    collect(used,d->labels->add_model);
    for (size_t i = 0; i < d->labels->model_count; ++i) collect(used,d->labels->model_names[i]);
    for (size_t i = 0; i < d->labels->motion_count; ++i) collect(used,d->labels->motion_names[i]);
    for (size_t i = 0; i < d->labels->expression_count; ++i) collect(used,d->labels->expression_names[i]);
    for (size_t i = 0; i < d->labels->audio_count; ++i) collect(used,d->labels->audio_names[i]);
    size_t count = 0;
    for (nk_rune i = 32; i < 0x110000; ++i)
        if (contains(used,i) && (i == 32 || !contains(used,i-1))) ++count;
    nk_rune *result = calloc(count*2+1,sizeof(*result));
    if (result) {
        size_t out = 0;
        for (nk_rune i = 32; i < 0x110000; ++i) {
            if (!contains(used,i)) continue;
            result[out++] = i;
            while (i+1 < 0x110000 && contains(used,i+1)) ++i;
            result[out++] = i;
        }
    }
    free(used);
    return result;
}

bool dial_fonts_init(Dial *d) {
    DialPaint *p = &d->paint;
    p->ranges[0] = ranges(d);
    if (!p->ranges[0] || !bongo_cat_ui_font_split_ranges(p->ranges[0],
        &p->ranges[1],&p->ranges[2],&p->ranges[3])) {
        /* split_ranges frees partial allocations on failure. */
        p->ranges[1] = p->ranges[2] = p->ranges[3] = NULL;
        return false;
    }
    char paths[3][BONGO_CAT_PATH_CAP];
    const char *names[] = {
        bongo_cat_ui_system_font(paths[0],sizeof(paths[0]),false),
        bongo_cat_ui_system_font(paths[1],sizeof(paths[1]),true),
        bongo_cat_ui_system_korean_font(paths[2],sizeof(paths[2]))};
    UIFontSource sources[3] = {0};
    for (int i = 0; i < 3; ++i)
        if (names[i]) bongo_cat_ui_font_source_load(&sources[i],names[i]);
    nk_font_atlas_init_default(&p->atlas);
    nk_font_atlas_begin(&p->atlas);
    float height = 24 * fmaxf(1,fminf(2,d->scale*d->raster_scale));
    p->font = bongo_cat_ui_font_add_family(&p->atlas,&sources[0],&sources[1],&sources[2],
        height,p->ranges[0],p->ranges[1],p->ranges[2],p->ranges[3]);
    int width = 0, rows = 0, maximum = 0;
    const void *pixels = p->font ? nk_font_atlas_bake(&p->atlas,&width,&rows,NK_FONT_ATLAS_ALPHA8) : NULL;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    bool ok = pixels && width > 0 && rows > 0 && width <= maximum && rows <= maximum;
    if (ok) {
        glGenTextures(1,&p->font_texture);
        glBindTexture(GL_TEXTURE_2D,p->font_texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        /* Match the preferences atlas: white RGB with single-channel alpha. */
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_ONE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_G,GL_ONE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_B,GL_ONE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_A,GL_RED);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,width,rows,0,GL_RED,GL_UNSIGNED_BYTE,pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        ok = p->font_texture && glGetError() == GL_NO_ERROR;
        nk_font_atlas_end(&p->atlas,nk_handle_id((int)p->font_texture),NULL);
    }
    for (int i = 0; i < 3; ++i) {
        bongo_cat_ui_font_detach_source(&p->atlas,&sources[i]);
        bongo_cat_ui_font_source_release(&sources[i]);
    }
    nk_font_atlas_cleanup(&p->atlas);
    return ok;
}

static void line(Dial *d, const char *s, int length, float x, float y,
    float width, float size, uint32_t color) {
    struct nk_font *font = d->paint.font;
    const struct nk_font_glyph *glyphs[BONGO_CAT_MENU_LABEL_CAP];
    int count = 0;
    float total = 0;
    while (length > 0 && count < BONGO_CAT_MENU_LABEL_CAP) {
        nk_rune rune;
        int n = nk_utf_decode(s,&rune,length);
        if (n <= 0) break;
        const struct nk_font_glyph *g = nk_font_find_glyph(font,rune);
        if (g) { glyphs[count++] = g; total += g->xadvance; }
        s += n; length -= n;
    }
    float scale = size/font->info.height;
    if (total*scale > width && total > 0) scale = width/total;
    float left = x-total*scale/2, top = y-font->info.height*scale/2;
    for (int i = 0; i < count; ++i) {
        const struct nk_font_glyph *g = glyphs[i];
        if (g->w > 0 && g->h > 0)
            dial_quad(d,d->paint.font_texture,left+g->x0*scale,top+g->y0*scale,
                g->w*scale,g->h*scale,g->u0,g->v0,g->u1,g->v1,color);
        left += g->xadvance*scale;
    }
}

void dial_text(Dial *d, const char *s, float x, float y, float width,
    float size, uint32_t color) {
    if (!s || !d->paint.font) return;
    if (!strncmp(s,"live2d_",7)) {
        line(d,"live2d",6,x,y-size*.55f,width,size,color);
        line(d,s+7,(int)strlen(s+7),x,y+size*.55f,width,size,color);
    } else {
        /* Wrap at a UTF-8 boundary before shrinking a long name. Two lines
           fit the child ring while keeping normal labels at full size. */
        int length = (int)strlen(s), offset = 0, split = 0;
        float total = 0, best = width;
        struct nk_font *font = d->paint.font;
        float scale = size/font->info.height;
        while (offset < length) {
            nk_rune rune;
            int n = nk_utf_decode(s+offset,&rune,length-offset);
            if (n <= 0) break;
            const struct nk_font_glyph *g = nk_font_find_glyph(font,rune);
            if (g) total += g->xadvance*scale;
            offset += n;
        }
        if (total > width) {
            float advance = 0;
            offset = 0; best = total;
            while (offset < length) {
                nk_rune rune;
                int n = nk_utf_decode(s+offset,&rune,length-offset);
                if (n <= 0) break;
                const struct nk_font_glyph *g = nk_font_find_glyph(font,rune);
                if (g) advance += g->xadvance*scale;
                offset += n;
                float difference = fabsf(total-2*advance);
                if (offset < length && difference < best) {
                    best = difference; split = offset;
                }
            }
        }
        if (split) {
            line(d,s,split,x,y-size*.55f,width,size,color);
            line(d,s+split,length-split,x,y+size*.55f,width,size,color);
        } else line(d,s,length,x,y,width,size,color);
    }
}
