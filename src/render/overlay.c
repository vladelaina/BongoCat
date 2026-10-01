#include "overlay_internal.h"

#include <stdlib.h>

static const char *vertex_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos; layout(location=1) in vec2 uv;\n"
    "out vec2 tex; uniform bool mirror;\n"
    "uniform sampler2D image; uniform int reference_width,reference_height;\n"
    "void main(){vec2 p=pos;"
    "if(reference_width>0&&reference_height>0){"
    "vec2 extent=vec2(textureSize(image,0))/vec2(reference_width,reference_height);"
    "p=(p+vec2(1,-1))*extent+vec2(-1,1);}"
    "if(mirror)p.x=-p.x;gl_Position=vec4(p,0,1);tex=uv;}";
static const char *fragment_source =
    "#version 330 core\n"
    "in vec2 tex; out vec4 color; uniform sampler2D image;\n"
    "uniform bool erase_left;uniform bool erase_right;\n"
    "void main(){color=texture(image,tex);if(color.a<=.001)discard;"
    "vec2 dl=(tex-vec2(.700,.515))/vec2(.080,.170);"
    "vec2 dr=(tex-vec2(.275,.397))/vec2(.070,.160);"
    "bool l=dot(dl,dl)<1.;bool r=dot(dr,dr)<1.;"
    "if((erase_left&&l)||(erase_right&&r))color.rgb=vec3(1);color.rgb*=color.a;}";

BongoCatOverlay *bongo_cat_overlay_create(BongoCatError *error) {
    BongoCatOverlay *value = calloc(1, sizeof(*value));
    if (!value || !bongo_cat_gl_load(&value->gl, error)) {
        free(value);
        return NULL;
    }
    value->program = bongo_cat_gl_program(&value->gl, vertex_source, fragment_source, error);
    if (!value->program) { free(value); return NULL; }
    value->mirror_location = value->gl.uniform_location(value->program, "mirror");
    value->image_location = value->gl.uniform_location(value->program, "image");
    value->reference_width_location = value->gl.uniform_location(value->program,
        "reference_width");
    value->reference_height_location = value->gl.uniform_location(value->program,
        "reference_height");
    value->erase_left_location = value->gl.uniform_location(value->program, "erase_left");
    value->erase_right_location = value->gl.uniform_location(value->program, "erase_right");
    const float vertices[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
    value->gl.gen_vertex_arrays(1, &value->vao);
    value->gl.bind_vertex_array(value->vao);
    value->gl.gen_buffers(1, &value->vbo);
    value->gl.bind_buffer(GL_ARRAY_BUFFER, value->vbo);
    value->gl.buffer_data(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    value->gl.enable_attribute(0);
    value->gl.attribute_pointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, NULL);
    value->gl.enable_attribute(1);
    value->gl.attribute_pointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
        (const void *)(sizeof(float) * 2));
    value->mver_pointer = bongo_cat_mver_pointer_overlay_create(error);
    if (!value->mver_pointer) {
        bongo_cat_overlay_destroy(value);
        return NULL;
    }
    return value;
}

void bongo_cat_overlay_destroy(BongoCatOverlay *value) {
    if (!value) return;
    bongo_cat_mver_pointer_overlay_destroy(value->mver_pointer);
    bongo_cat_overlay_clear_textures(value);
    if (value->vbo) value->gl.delete_buffers(1, &value->vbo);
    if (value->vao) value->gl.delete_vertex_arrays(1, &value->vao);
    if (value->program) value->gl.delete_program(value->program);
    free(value);
}

void bongo_cat_overlay_clear(BongoCatOverlay *value) {
    if (!value) return;
    bongo_cat_overlay_clear_textures(value);
    bongo_cat_mver_pointer_overlay_clear(value->mver_pointer);
    value->directory[0] = '\0';
}

bool bongo_cat_overlay_mver_pointer_enabled(const BongoCatOverlay *value) {
    return value && !value->model_pointer_preferred &&
        bongo_cat_mver_pointer_overlay_enabled(value->mver_pointer);
}
bool bongo_cat_overlay_mver_pointer_left_handed(const BongoCatOverlay *value) {
    return bongo_cat_overlay_mver_pointer_enabled(value) &&
        bongo_cat_mver_pointer_overlay_left_handed(value->mver_pointer);
}
void bongo_cat_overlay_set_mver_pointer(BongoCatOverlay *value,
    float x_ratio, float y_ratio, bool left, bool right, bool side) {
    if (bongo_cat_overlay_mver_pointer_enabled(value))
        bongo_cat_mver_pointer_overlay_set(value->mver_pointer, x_ratio,
            y_ratio, left, right, side);
}
