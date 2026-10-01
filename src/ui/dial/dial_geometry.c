#include "dial_internal.h"
#include <string.h>

static DialPoint polar(float r, float a) {
    return (DialPoint){r * cosf(a), r * sinf(a)};
}
static void point(DialPath *p, DialPoint v) {
    if (p->count && hypotf(v.x - p->points[p->count-1].x,
        v.y - p->points[p->count-1].y) < .001f) return;
    if (p->count < DIAL_PATH_POINTS) p->points[p->count++] = v;
}
static void quad(DialPath *p, DialPoint a, DialPoint c, DialPoint b) {
    point(p, a);
    for (int i = 1; i <= 6; ++i) {
        float t = (float)i / 6, u = 1 - t;
        point(p, (DialPoint){u*u*a.x + 2*u*t*c.x + t*t*b.x,
            u*u*a.y + 2*u*t*c.y + t*t*b.y});
    }
}
static void arc(DialPath *p, float r, float a, float b) {
    int steps = (int)ceilf(fabsf(b-a) * r / 3);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; ++i)
        point(p, polar(r, a + (b-a) * (float)i / (float)steps));
}
static float cross(DialPoint a, DialPoint b, DialPoint c) {
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}
static void triangulate(DialPath *p) {
    unsigned short remaining[DIAL_PATH_POINTS];
    float area = 0;
    for (int i = 0; i < p->count; ++i) {
        remaining[i] = (unsigned short)i;
        DialPoint a = p->points[i], b = p->points[(i+1)%p->count];
        area += a.x*b.y - b.x*a.y;
    }
    float sign = area > 0 ? 1.0f : -1.0f;
    int count = p->count;
    while (count >= 3) {
        bool found = false;
        for (int i = 0; i < count; ++i) {
            int a = remaining[(i+count-1)%count], b = remaining[i], c = remaining[(i+1)%count];
            if (cross(p->points[a], p->points[b], p->points[c])*sign <= .00001f) continue;
            bool inside = false;
            for (int j = 0; j < count; ++j) {
                int v = remaining[j];
                if (v == a || v == b || v == c) continue;
                DialPoint q = p->points[v];
                if (cross(p->points[a], p->points[b], q)*sign >= 0 &&
                    cross(p->points[b], p->points[c], q)*sign >= 0 &&
                    cross(p->points[c], p->points[a], q)*sign >= 0) {
                    inside = true; break;
                }
            }
            if (inside) continue;
            p->triangles[p->triangle_count++] = (unsigned short)a;
            p->triangles[p->triangle_count++] = (unsigned short)b;
            p->triangles[p->triangle_count++] = (unsigned short)c;
            memmove(remaining+i, remaining+i+1, (size_t)(count-i-1)*sizeof(*remaining));
            --count; found = true; break;
        }
        if (!found) break;
    }
}
void dial_sector(DialPath *p, float inner, float outer, float start, float end) {
    memset(p, 0, sizeof(*p));
    const float corner = 12, a = start + .024f, b = end - .024f;
    const float ci = corner / inner, co = corner / outer;
    point(p, polar(outer-corner, b));
    quad(p, polar(inner+corner,b), polar(inner,b), polar(inner,b-ci));
    arc(p, inner,b-ci,a+ci);
    quad(p, polar(inner,a+ci),polar(inner,a),polar(inner+corner,a));
    p->rim_start = p->count;
    quad(p,polar(outer-corner,a),polar(outer,a),polar(outer,a+co));
    arc(p,outer,a+co,b-co);
    quad(p,polar(outer,b-co),polar(outer,b),polar(outer-corner,b));
    if (p->count > 1) --p->count; /* Last point closes back onto the first. */
    p->min_x = p->min_y = 1000; p->max_x = p->max_y = -1000;
    for (int i = 0; i < p->count; ++i) {
        p->min_x = fminf(p->min_x, p->points[i].x); p->max_x = fmaxf(p->max_x,p->points[i].x);
        p->min_y = fminf(p->min_y, p->points[i].y); p->max_y = fmaxf(p->max_y,p->points[i].y);
    }
    triangulate(p);
}
void dial_child_paths(Dial *d) {
    int count = dial_child_count(d);
    float step = dial_child_step(d);
    DialPaint *p = &d->paint;
    /* Page contents do not affect geometry. Keep the last layout even when
       the child ring is temporarily hidden; the draw loop uses child_count. */
    if (!count) return;
    if (p->child_path_count == count && p->child_path_active == d->active &&
        p->child_path_roots == d->count && p->child_path_step == step) return;
    memset(d->paint.children, 0, sizeof(d->paint.children));
    for (int i = 0; i < count; ++i) {
        float angle = dial_child_angle(d,i), half = dial_child_step(d)/2;
        dial_sector(&d->paint.children[i],198,262,angle-half,angle+half);
    }
    p->child_path_count = count;
    p->child_path_active = d->active;
    p->child_path_roots = d->count;
    p->child_path_step = step;
}

static float angle_distance(float a, float b) {
    return atan2f(sinf(a - b), cosf(a - b));
}

void dial_hit(Dial *d, float x, float y, int *root, int *child) {
    float radius = hypotf(x, y), angle = atan2f(y, x);
    *root = -1; *child = -1;
    if (d->active >= 0 && radius >= 190 && radius <= 277) {
        for (int i = 0; i < dial_child_count(d); ++i) {
            if (fabsf(angle_distance(angle, dial_child_angle(d, i))) <
                dial_child_step(d) / 2) {
                *root = d->active; *child = i; return;
            }
        }
    }
    if (radius >= 73 && radius <= 198) {
        for (int i = 0; i < d->count; ++i) {
            if (d->child_focus && i != d->active) continue;
            float center = -DIAL_PI / 2 + i * 2 * DIAL_PI / d->count;
            if (fabsf(angle_distance(angle, center)) <= DIAL_PI / d->count) {
                *root = i; return;
            }
        }
    }
    /* Keep the iris open while crossing its radial gap or the center hub. */
    if (radius <= 280) *root = d->active;
}

void dial_transform(Dial *d, float zoom, float x, float y) {
    d->paint.zoom = zoom;
    d->paint.offset_x = x;
    d->paint.offset_y = y;
}
