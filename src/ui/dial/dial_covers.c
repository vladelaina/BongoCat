#include "dial_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

void dial_covers_tick(Dial *d) {
    if (d->active != 9) return;
    for (int i = 0; i < dial_child_count(d); ++i) {
        size_t index = (size_t)(d->page*DIAL_PAGE+i);
        if (index >= d->labels->model_count || index >= BONGO_CAT_MODEL_CAP) continue;
        DialCover *cover = &d->covers[index];
        if (cover->attempted) continue;
        cover->attempted = true;
        const char *directory = d->labels->model_cover_directories ?
            d->labels->model_cover_directories[index] : NULL;
        char path[BONGO_CAT_PATH_CAP];
        if (directory && bongo_cat_path_join(path,sizeof(path),directory,"resources/cover.png")) {
            int width = 0, height = 0;
            BongoCatError ignored = {0};
            int pixels = (int)fminf(160,ceilf(44*d->scale*d->raster_scale*1.25f));
            cover->texture = bongo_cat_image_texture_thumbnail(path,pixels,pixels,&width,&height,&ignored);
            if (cover->texture && width > 0 && height > 0) {
                float fit = fminf(44.0f/(float)width,40.0f/(float)height);
                cover->width = (float)width*fit; cover->height = (float)height*fit;
            }
        }
        d->dirty = true;
        return; /* One image per frame; failed reads are also cached. */
    }
}

bool dial_cover_draw(Dial *d, int child, float x, float y, float opacity) {
    if (d->active != 9 || child < 0) return false;
    size_t index = (size_t)(d->page*DIAL_PAGE+child);
    if (index >= d->labels->model_count || index >= BONGO_CAT_MODEL_CAP) return false;
    DialCover *cover = &d->covers[index];
    uint32_t color = 0xffffff | ((uint32_t)(255*opacity)<<24);
    if (cover->texture && cover->width > 0 && cover->height > 0)
        dial_quad(d,cover->texture,x-cover->width/2,y-cover->height/2,
            cover->width,cover->height,0,0,1,1,color);
    else dial_icon(d,9,x,y,28,(color&0xff000000)|0xa78bfa);
    return true;
}
