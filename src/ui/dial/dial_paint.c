#include "dial_internal.h"
#include <stddef.h>
#include <stdlib.h>

static const char *vertex_shader =
    "#version 330 core\n"
    "layout(location=0) in vec2 Position; layout(location=1) in vec2 UV;"
    "layout(location=2) in vec4 Color; uniform mat4 Projection;"
    "out vec2 uv; out vec4 color;"
    "void main(){uv=UV;color=Color;gl_Position=Projection*vec4(Position,0,1);}";
static const char *fragment_shader =
    "#version 330 core\n"
    "in vec2 uv; in vec4 color; uniform sampler2D Texture; out vec4 pixel;"
    "void main(){pixel=color*texture(Texture,uv);}";

bool dial_paint_init(Dial *d) {
    DialPaint *p = &d->paint;
    BongoCatError error = {0};
    if (!bongo_cat_gl_load(&p->gl, &error)) return SDL_SetError("Radial menu GL loader: %s",error.message);
    bongo_cat_gl_clear_errors();
    p->program = bongo_cat_gl_program(&p->gl,vertex_shader,fragment_shader,&error);
    if (!p->program) { SDL_SetError("%s",error.message); return false; }
    p->projection = p->gl.uniform_location(p->program,"Projection");
    p->sampler = p->gl.uniform_location(p->program,"Texture");
    p->gl.gen_vertex_arrays(1,&p->vao);
    p->gl.gen_buffers(1,&p->vbo);
    p->gl.bind_vertex_array(p->vao);
    p->gl.bind_buffer(GL_ARRAY_BUFFER,p->vbo);
    p->gl.enable_attribute(0); p->gl.enable_attribute(1); p->gl.enable_attribute(2);
    p->gl.attribute_pointer(0,2,GL_FLOAT,GL_FALSE,sizeof(DialVertex),(void *)offsetof(DialVertex,x));
    p->gl.attribute_pointer(1,2,GL_FLOAT,GL_FALSE,sizeof(DialVertex),(void *)offsetof(DialVertex,u));
    p->gl.attribute_pointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(DialVertex),(void *)offsetof(DialVertex,r));
    glGenTextures(1,&p->white); glBindTexture(GL_TEXTURE_2D,p->white);
    const uint8_t white[] = {255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    if (!dial_fonts_init(d)) return SDL_SetError("Radial menu font atlas could not be created");
    for (int i = 0; i < d->count; ++i) {
        float angle = -DIAL_PI/2 + (float)i*2*DIAL_PI/(float)d->count;
        float half = DIAL_PI/(float)d->count;
        dial_sector(&p->roots[i],82,184,angle-half,angle+half);
        if (!p->roots[i].triangle_count) return SDL_SetError("Radial menu sector %d could not be triangulated",i);
    }
    GLenum status = glGetError();
    return status == GL_NO_ERROR || SDL_SetError("Radial menu GL initialization error: 0x%x",(unsigned)status);
}

bool dial_paint_frame(Dial *d) {
    DialPaint *p = &d->paint;
    p->count = 0; p->batch_count = 0;
    /* Never show a completely transparent first frame: it cannot receive
       pointer hits on some native transparent-window implementations. */
    p->alpha = fmaxf(.15f,fminf(1,(float)(SDL_GetTicks()-d->opened_at)/180));
    dial_scene(d);
    if (p->failed) return SDL_SetError("Radial menu geometry budget exceeded");
    glViewport(0,0,d->pixel_width,d->pixel_height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND); p->gl.blend_equation(GL_FUNC_ADD);
    p->gl.blend_func_separate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    float width = (float)d->width, height = (float)d->height;
    const float projection[16] = {2/width,0,0,0, 0,-2/height,0,0, 0,0,-1,0, -1,1,0,1};
    p->gl.use_program(p->program);
    p->gl.uniform_matrix_4fv(p->projection,1,GL_FALSE,projection);
    p->gl.uniform_1i(p->sampler,0);
    p->gl.active_texture(GL_TEXTURE0);
    p->gl.bind_vertex_array(p->vao);
    p->gl.bind_buffer(GL_ARRAY_BUFFER,p->vbo);
    p->gl.buffer_data(GL_ARRAY_BUFFER,(GLsizeiptr)(p->count*sizeof(*p->vertices)),p->vertices,GL_STREAM_DRAW);
    for (int i = 0; i < p->batch_count; ++i) {
        DialBatch b = p->batches[i];
        glBindTexture(GL_TEXTURE_2D,b.texture);
        glDrawArrays(GL_TRIANGLES,b.first,b.count);
    }
    if (glGetError() != GL_NO_ERROR) return SDL_SetError("Radial menu OpenGL draw failed");
    return SDL_GL_SwapWindow(d->window);
}

void dial_paint_free(Dial *d) {
    DialPaint *p = &d->paint;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i)
        if (d->covers[i].texture) glDeleteTextures(1,&d->covers[i].texture);
    if (p->font_texture) glDeleteTextures(1,&p->font_texture);
    if (p->atlas.permanent.alloc) nk_font_atlas_clear(&p->atlas);
    for (int i = 0; i < 4; ++i) free(p->ranges[i]);
    if (p->white) glDeleteTextures(1,&p->white);
    if (p->vbo) p->gl.delete_buffers(1,&p->vbo);
    if (p->vao) p->gl.delete_vertex_arrays(1,&p->vao);
    if (p->program) p->gl.delete_program(p->program);
    free(p->vertices);
}
