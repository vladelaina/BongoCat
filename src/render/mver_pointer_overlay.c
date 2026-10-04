#include "mver_pointer_overlay_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
#include "bongo_cat/model.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static const char *vertex_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;layout(location=1) in vec2 uv;"
    "layout(location=2) in vec4 tint;out vec2 tex;out vec4 color;"
    "void main(){gl_Position=vec4(pos,0,1);tex=uv;color=tint;}";

static const char *fragment_source =
    "#version 330 core\n"
    "in vec2 tex;in vec4 color;out vec4 outputColor;uniform sampler2D image;"
    "uniform bool textured;void main(){vec4 value=color;"
    "if(textured)value*=texture(image,tex);if(value.a<=.001)discard;"
    "value.rgb*=value.a;outputColor=value;}";

static void clear_textures(BongoCatMverPointerOverlay *value) {
    BongoCatPointerTexture *textures[] = {&value->arm, &value->device,
        &value->left, &value->right, &value->side};
    for (size_t index = 0; index < sizeof(textures) / sizeof(textures[0]); ++index) {
        if (textures[index]->id) glDeleteTextures(1, &textures[index]->id);
        *textures[index] = (BongoCatPointerTexture){0};
    }
    value->enabled = false;
}

BongoCatMverPointerOverlay *bongo_cat_mver_pointer_overlay_create(
    BongoCatError *error) {
    BongoCatMverPointerOverlay *value = calloc(1, sizeof(*value));
    if (!value || !bongo_cat_gl_load(&value->gl, error)) {
        free(value);
        return NULL;
    }
    value->program = bongo_cat_gl_program(&value->gl, vertex_source,
        fragment_source, error);
    if (!value->program) {
        free(value);
        return NULL;
    }
    value->textured_location = value->gl.uniform_location(value->program, "textured");
    value->image_location = value->gl.uniform_location(value->program, "image");
    value->gl.gen_vertex_arrays(1, &value->vao);
    value->gl.bind_vertex_array(value->vao);
    value->gl.gen_buffers(1, &value->vbo);
    value->gl.bind_buffer(GL_ARRAY_BUFFER, value->vbo);
    value->gl.enable_attribute(0);
    value->gl.attribute_pointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, NULL);
    value->gl.enable_attribute(1);
    value->gl.attribute_pointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
        (const void *)(sizeof(float) * 2));
    value->gl.enable_attribute(2);
    value->gl.attribute_pointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
        (const void *)(sizeof(float) * 4));
    value->gl.bind_vertex_array(0);
    return value;
}

void bongo_cat_mver_pointer_overlay_set_vertical_flip(
    BongoCatMverPointerOverlay *value, bool flipped) {
    if (value) value->vertical_flip = flipped;
}

void bongo_cat_mver_pointer_overlay_destroy(BongoCatMverPointerOverlay *value) {
    if (!value) return;
    clear_textures(value);
    if (value->vbo) value->gl.delete_buffers(1, &value->vbo);
    if (value->vao) value->gl.delete_vertex_arrays(1, &value->vao);
    if (value->program) value->gl.delete_program(value->program);
    free(value);
}

void bongo_cat_mver_pointer_overlay_clear(BongoCatMverPointerOverlay *value) {
    if (value) clear_textures(value);
}

static bool safe_relative(const char *path) {
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':'))
        return false;
    const char *part = path;
    while ((part = strstr(part, "..")) != NULL) {
        bool left = part == path || part[-1] == '/' || part[-1] == '\\';
        bool right = !part[2] || part[2] == '/' || part[2] == '\\';
        if (left && right) return false;
        part += 2;
    }
    return true;
}

static bool load_texture(const char *directory, const char *relative,
    bool required, BongoCatPointerTexture *texture, BongoCatError *error) {
    char path[BONGO_CAT_PATH_CAP];
    if (!relative || !relative[0]) return !required;
    if (!safe_relative(relative) ||
        !bongo_cat_path_join(path, sizeof(path), directory, relative) ||
        !bongo_cat_path_is_file(path)) {
        if (required) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Missing Mver pointer asset: %s", relative);
        return !required;
    }
    texture->id = bongo_cat_image_texture(path, &texture->width,
        &texture->height, error);
    return texture->id != 0;
}

static double number_or(yyjson_val *value, double fallback) {
    return yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
}

static float color(yyjson_val *pointer, const char *name) {
    double value = number_or(yyjson_obj_get(pointer, name), 0.0);
    if (value < 0.0) value = 0.0;
    if (value > 255.0) value = 255.0;
    return (float)(value / 255.0);
}

bool bongo_cat_mver_pointer_overlay_load(BongoCatMverPointerOverlay *value,
    const char *directory, BongoCatError *error) {
    if (!value || !directory) return false;
    clear_textures(value);
    char metadata[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_model_adapter_metadata_path(directory, metadata,
        sizeof(metadata))) return false;
    yyjson_doc *document = bongo_cat_json_read_file(metadata, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *pointer = yyjson_obj_get(root, "standardPointer");
    yyjson_val *render = yyjson_obj_get(root, "render");
    bool requested = yyjson_is_obj(pointer) &&
        yyjson_is_true(yyjson_obj_get(pointer, "enabled"));
    if (!requested) {
        yyjson_doc_free(document);
        return true;
    }
    value->reference_width = (int)number_or(
        yyjson_obj_get(render, "referenceWidth"), 1400.0);
    value->reference_height = (int)number_or(
        yyjson_obj_get(render, "referenceHeight"), 1400.0);
    value->mouse = yyjson_is_true(yyjson_obj_get(pointer, "mouse"));
    value->left_handed = yyjson_is_true(yyjson_obj_get(pointer, "leftHanded"));
    value->geometry.offset_x = (float)number_or(yyjson_obj_get(pointer, "offsetX"), 0.0);
    value->geometry.offset_y = (float)number_or(yyjson_obj_get(pointer, "offsetY"), 0.0);
    value->geometry.hand_offset_x = (float)number_or(
        yyjson_obj_get(pointer, "handOffsetX"), 0.0);
    value->geometry.hand_offset_y = (float)number_or(
        yyjson_obj_get(pointer, "handOffsetY"), 0.0);
    value->scale = (float)number_or(yyjson_obj_get(pointer, "scale"), 1.0);
    value->line_red = color(pointer, "lineRed");
    value->line_green = color(pointer, "lineGreen");
    value->line_blue = color(pointer, "lineBlue");
    value->x_ratio = value->y_ratio = 0.5f;
    const char *arm = yyjson_get_str(yyjson_obj_get(pointer, "arm"));
    const char *device = yyjson_get_str(yyjson_obj_get(pointer, "device"));
    const char *left = yyjson_get_str(yyjson_obj_get(pointer, "left"));
    const char *right = yyjson_get_str(yyjson_obj_get(pointer, "right"));
    const char *side = yyjson_get_str(yyjson_obj_get(pointer, "side"));
    bool valid = value->reference_width > 0 && value->reference_height > 0 &&
        isfinite(value->scale) && load_texture(directory, arm, true,
            &value->arm, error) && load_texture(directory, device, true,
            &value->device, error) && load_texture(directory, left, false,
            &value->left, error) && load_texture(directory, right, false,
            &value->right, error) && load_texture(directory, side, false,
            &value->side, error);
    value->enabled = valid;
    yyjson_doc_free(document);
    if (!valid) clear_textures(value);
    return valid;
}

bool bongo_cat_mver_pointer_overlay_enabled(
    const BongoCatMverPointerOverlay *value) {
    return value && value->enabled;
}

bool bongo_cat_mver_pointer_overlay_left_handed(
    const BongoCatMverPointerOverlay *value) {
    return value && value->enabled && value->left_handed;
}

void bongo_cat_mver_pointer_overlay_set(BongoCatMverPointerOverlay *value,
    float x_ratio, float y_ratio, bool left, bool right, bool side) {
    if (!value) return;
    value->x_ratio = x_ratio;
    value->y_ratio = y_ratio;
    value->left_down = left;
    value->right_down = right;
    value->side_down = side;
}
