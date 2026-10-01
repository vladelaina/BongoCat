#include "ui_paint_cache.h"

#include <SDL3/SDL_opengl.h>
#include <stddef.h>
#include <string.h>

struct BongoCatUIPaintTexture {
    BongoCatUIBackend *backend;
    BongoCatUIPaintKey key;
    GLuint texture;
    uint64_t used;
    size_t bytes;
};

#define PAINT_CACHE_COUNT 48
/* Effects are regenerated when evicted, so keep the cache bounded instead
   of allowing a long settings session to retain a large GPU allocation. */
#define PAINT_CACHE_BYTE_LIMIT (16u * 1024u * 1024u)

static BongoCatUIPaintTexture textures[PAINT_CACHE_COUNT];
static uint64_t use_counter;

static void release_texture(BongoCatUIPaintTexture *item) {
    if (item->texture) glDeleteTextures(1, &item->texture);
    memset(item, 0, sizeof(*item));
}

static bool reserve_bytes(BongoCatUIBackend *backend,
    BongoCatUIPaintTexture *keep, size_t required) {
    if (required > PAINT_CACHE_BYTE_LIMIT) return false;
    for (;;) {
        size_t used = 0;
        BongoCatUIPaintTexture *oldest = NULL;
        for (size_t i = 0; i < PAINT_CACHE_COUNT; ++i) {
            BongoCatUIPaintTexture *item = &textures[i];
            if (item == keep || item->backend != backend || !item->texture)
                continue;
            used += item->bytes;
            bool stale = !backend->paint_frame_marker ||
                item->used < backend->paint_frame_marker;
            if (stale && (!oldest || item->used < oldest->used)) oldest = item;
        }
        if (used <= PAINT_CACHE_BYTE_LIMIT &&
            required <= PAINT_CACHE_BYTE_LIMIT - used) return true;
        if (!oldest) return false;
        release_texture(oldest);
    }
}

BongoCatUIPaintTexture *bongo_cat_ui_paint_cache_get(
    BongoCatUIBackend *backend, const BongoCatUIPaintKey *key) {
    BongoCatUIPaintTexture *empty = NULL, *oldest = NULL;
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        BongoCatUIPaintTexture *item = &textures[i];
        if (item->texture && item->backend == backend &&
            memcmp(&item->key, key, sizeof(*key)) == 0) {
            item->used = ++use_counter;
            return item;
        }
        if (!item->texture && !empty) empty = item;
        bool reusable = item->backend == backend &&
            (!backend->paint_frame_marker ||
                item->used < backend->paint_frame_marker);
        if (reusable && (!oldest || item->used < oldest->used)) oldest = item;
    }
    BongoCatUIPaintTexture *item = empty ? empty : oldest;
    if (!item) return NULL;
    release_texture(item);
    item->backend = backend;
    item->key = *key;
    item->used = ++use_counter;
    return item;
}

bool bongo_cat_ui_paint_cache_ready(
    const BongoCatUIPaintTexture *item) {
    return item && item->texture != 0;
}

bool bongo_cat_ui_paint_cache_upload(BongoCatUIPaintTexture *item,
    const unsigned char *pixels, bool single_channel) {
    if (!item || !pixels || item->key.width <= 0 || item->key.height <= 0)
        return false;
    size_t channels = single_channel ? 1u : 4u;
    if ((size_t)item->key.width > SIZE_MAX / (size_t)item->key.height ||
        (size_t)item->key.width * (size_t)item->key.height >
            SIZE_MAX / channels) return false;
    size_t bytes = (size_t)item->key.width * item->key.height * channels;
    if (!reserve_bytes(item->backend, item, bytes)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "UI paint cache limit rejected a %llu-byte texture",
            (unsigned long long)bytes);
        return false;
    }
    bongo_cat_gl_clear_errors();
    glGenTextures(1, &item->texture);
    glBindTexture(GL_TEXTURE_2D, item->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (single_channel) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, single_channel ? GL_R8 : GL_RGBA8,
        item->key.width, item->key.height, 0,
        single_channel ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GLenum upload_error = glGetError();
    if (item->texture && upload_error == GL_NO_ERROR) {
        item->bytes = bytes;
        return true;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "UI paint texture upload failed for %dx%d (0x%x)",
        item->key.width, item->key.height, (unsigned)upload_error);
    if (item->texture) glDeleteTextures(1, &item->texture);
    item->texture = 0;
    return false;
}

void bongo_cat_ui_paint_cache_draw(struct nk_context *context,
    struct nk_rect bounds, const BongoCatUIPaintTexture *item,
    struct nk_color tint) {
    struct nk_image image = nk_image_id((int)item->texture);
    nk_draw_image(nk_window_get_canvas(context), bounds, &image, tint);
}

void bongo_cat_ui_paint_cache_begin_frame(BongoCatUIBackend *backend) {
    if (!backend) return;
    /* Reuse hover/animation variants across frames. LRU allocation still caps
       this cache at 48 entries / 16 MiB and protects current-frame commands. */
    backend->paint_frame_marker = use_counter + 1;
}

void bongo_cat_ui_paint_cache_trim_idle(BongoCatUIBackend *backend) {
    /* Keep small reusable effects, evict older variants first. No timer or
       extra redraw is needed; closing the window releases everything. */
    size_t bytes = bongo_cat_ui_paint_cache_usage(backend, NULL);
    while (bytes > 4u * 1024u * 1024u) {
        BongoCatUIPaintTexture *oldest = NULL;
        for (size_t i = 0; i < PAINT_CACHE_COUNT; i++) {
            BongoCatUIPaintTexture *item = &textures[i];
            if (item->backend == backend && item->texture &&
                (!oldest || item->used < oldest->used)) oldest = item;
        }
        if (!oldest) break;
        bytes -= oldest->bytes;
        release_texture(oldest);
    }
}

size_t bongo_cat_ui_paint_cache_usage(BongoCatUIBackend *backend,
    size_t *texture_count) {
    size_t bytes = 0, count = 0;
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        const BongoCatUIPaintTexture *item = &textures[i];
        if (item->backend != backend || !item->texture) continue;
        bytes += item->bytes;
        count++;
    }
    if (texture_count) *texture_count = count;
    return bytes;
}

void bongo_cat_ui_paint_cache_destroy(BongoCatUIBackend *backend) {
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        BongoCatUIPaintTexture *item = &textures[i];
        if (item->backend != backend) continue;
        release_texture(item);
    }
    if (backend) backend->paint_frame_marker = 0;
}

void bongo_cat_ui_paint_cache_abandon(BongoCatUIBackend *backend) {
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        BongoCatUIPaintTexture *item = &textures[i];
        if (item->backend == backend) memset(item, 0, sizeof(*item));
    }
    if (backend) backend->paint_frame_marker = 0;
}
