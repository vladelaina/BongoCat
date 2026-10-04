#include "dial_internal.h"
#include <stdlib.h>

static void vertex(Dial *d, GLuint texture, float x, float y, float u, float v,
    uint32_t color) {
    DialPaint *p = &d->paint;
    if (p->failed) return;
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? p->capacity * 2 : 32768;
        if (capacity > 524288) { p->failed = true; return; }
        void *memory = realloc(p->vertices, capacity * sizeof(*p->vertices));
        if (!memory) { p->failed = true; return; }
        p->vertices = memory; p->capacity = capacity;
    }
    if (!p->batch_count || p->batches[p->batch_count-1].texture != texture) {
        if (p->batch_count == 512) { p->failed = true; return; }
        p->batches[p->batch_count++] = (DialBatch){texture, (int)p->count, 0};
    }
    float scale = d->scale * d->opening;
    p->vertices[p->count++] = (DialVertex){
        (float)d->width / 2 + (x*p->zoom+p->offset_x)*scale,
        (float)d->height / 2 + (y*p->zoom+p->offset_y)*scale, u, v,
        (uint8_t)(color>>16), (uint8_t)(color>>8), (uint8_t)color,
        (uint8_t)((float)(color>>24)*p->alpha)};
    p->batches[p->batch_count-1].count++;
}

void dial_triangle(Dial *d, DialPoint a, DialPoint b, DialPoint c,
    uint32_t ca, uint32_t cb, uint32_t cc) {
    vertex(d,d->paint.white,a.x,a.y,0,0,ca);
    vertex(d,d->paint.white,b.x,b.y,0,0,cb);
    vertex(d,d->paint.white,c.x,c.y,0,0,cc);
}

void dial_quad(Dial *d, GLuint texture, float x, float y, float w, float h,
    float u0, float v0, float u1, float v1, uint32_t color) {
    vertex(d,texture,x,y,u0,v0,color);
    vertex(d,texture,x+w,y,u1,v0,color);
    vertex(d,texture,x+w,y+h,u1,v1,color);
    vertex(d,texture,x,y,u0,v0,color);
    vertex(d,texture,x+w,y+h,u1,v1,color);
    vertex(d,texture,x,y+h,u0,v1,color);
}

static void strip(Dial *d, DialPoint a, DialPoint b, float nx, float ny,
    float inner, float outer, uint32_t ci, uint32_t co) {
    DialPoint a0 = {a.x+nx*inner,a.y+ny*inner}, a1 = {a.x+nx*outer,a.y+ny*outer};
    DialPoint b0 = {b.x+nx*inner,b.y+ny*inner}, b1 = {b.x+nx*outer,b.y+ny*outer};
    dial_triangle(d,a0,b0,b1,ci,ci,co);
    dial_triangle(d,a0,b1,a1,ci,co,co);
}

void dial_dot(Dial *d, float x, float y, float r, uint32_t color) {
    const int count = 20;
    for (int i = 0; i < count; ++i) {
        float a = (float)i*2*DIAL_PI/(float)count, b = (float)(i+1)*2*DIAL_PI/(float)count;
        DialPoint p = {x+r*cosf(a),y+r*sinf(a)}, q = {x+r*cosf(b),y+r*sinf(b)};
        dial_triangle(d,(DialPoint){x,y},p,q,color,color,color);
    }
}

void dial_stroke(Dial *d, const DialPoint *points, int count, float width,
    uint32_t color, bool smooth) {
    float edge = .7f / fmaxf(.1f, d->scale*d->opening);
    for (int i = 1; i < count; ++i) {
        DialPoint a = points[i-1], b = points[i];
        float dx = b.x-a.x, dy = b.y-a.y, length = hypotf(dx,dy);
        if (length < .001f) continue;
        float nx = -dy/length, ny = dx/length;
        strip(d,a,b,nx,ny,-width/2,width/2,color,color);
        if (smooth) {
            strip(d,a,b,nx,ny,width/2,width/2+edge,color,color&0xffffff);
            strip(d,a,b,nx,ny,-width/2,-width/2-edge,color,color&0xffffff);
        }
    }
    if (smooth && count > 1) {
        dial_dot(d,points[0].x,points[0].y,width/2,color);
        dial_dot(d,points[count-1].x,points[count-1].y,width/2,color);
    }
}
