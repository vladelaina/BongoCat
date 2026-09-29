#include "overlay_internal.h"

#include "bongo_cat/image.h"

static void draw(BongoCatOverlay *value, GLuint texture,
    bool mirror, bool blend) {
    if (!value || !texture) return;
    if (blend) {
        glEnable(GL_BLEND);
        value->gl.blend_func_separate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else glDisable(GL_BLEND);
    value->gl.use_program(value->program);
    value->gl.uniform_1i(value->mirror_location, mirror);
    value->gl.uniform_1i(value->vertical_flip_location, value->vertical_flip);
    value->gl.uniform_1i(value->image_location, 0);
    value->gl.uniform_1i(value->reference_width_location, value->reference_width);
    value->gl.uniform_1i(value->reference_height_location, value->reference_height);
    value->gl.uniform_1i(value->erase_left_location,
        !blend && value->composed_cover && !value->composite &&
            value->left != 0);
    value->gl.uniform_1i(value->erase_right_location,
        !blend && value->composed_cover && !value->composite &&
            value->right != 0);
    value->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    value->gl.bind_vertex_array(value->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    value->gl.bind_vertex_array(0);
}

void bongo_cat_overlay_draw_background(BongoCatOverlay *value, bool mirror) {
    if (!value) return;
#ifndef BONGO_CAT_HAS_CUBISM
    if (value->composite_dirty) {
        value->composite_dirty = false;
        if (value->left || value->right) {
            BongoCatError ignored = {0};
            value->composite = bongo_cat_image_composite_texture(
                value->background_path, value->left_path, value->right_path,
                value->composite, value->clean_paws && value->left,
                value->clean_paws && value->right, &ignored);
        }
    }
#endif
    bool active = value->left || value->right;
    draw(value, active && value->composite ? value->composite :
        value->background, mirror, false);
}

void bongo_cat_overlay_draw_keys(BongoCatOverlay *value, bool mirror) {
    if (!value) return;
#ifdef BONGO_CAT_HAS_CUBISM
    draw(value, value->left, mirror, true);
    draw(value, value->right, mirror, true);
#else
    (void)mirror;
#endif
}

void bongo_cat_overlay_draw_pointer_before_keys(BongoCatOverlay *value) {
    if (bongo_cat_overlay_mver_pointer_enabled(value))
        bongo_cat_mver_pointer_overlay_draw_before_keys(value->mver_pointer);
}

void bongo_cat_overlay_draw_effect(BongoCatOverlay *value, bool mirror) {
    if (!value) return;
#ifdef BONGO_CAT_HAS_CUBISM
    draw(value, value->effect, mirror, true);
#else
    (void)mirror;
#endif
}

void bongo_cat_overlay_draw_pointer_after_keys(BongoCatOverlay *value) {
    if (bongo_cat_overlay_mver_pointer_enabled(value))
        bongo_cat_mver_pointer_overlay_draw_after_keys(value->mver_pointer);
}
