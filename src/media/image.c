#include "bongo_cat/gl_api.h"
#include "bongo_cat/image.h"
#include "image_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stdlib.h>
#include <string.h>

#define BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT 2048
BongoCatResult bongo_cat_image_load(const char *path, BongoCatImage *image, BongoCatError *error) {
    BongoCatResult result = bongo_cat_image_decode_pixels(path, image, error);
    if (result != BONGO_CAT_OK) return result;
    image->surface = SDL_CreateSurfaceFrom(image->width, image->height,
        SDL_PIXELFORMAT_RGBA32, image->pixels, image->width * 4);
    if (!image->surface) {
        bongo_cat_image_free(image);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "Cannot create image surface");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    return BONGO_CAT_OK;
}
void bongo_cat_image_free(BongoCatImage *image) {
    if (!image) return;
    if (image->surface) SDL_DestroySurface(image->surface);
    if (image->pixels) {
        if (image->pixels_stbi) stbi_image_free(image->pixels);
        else free(image->pixels);
    }
    memset(image, 0, sizeof(*image));
}
static unsigned int upload(const BongoCatImage *image, GLuint texture,
    bool model_texture) {
    /* Premultiply in place before any GPU filtering. Cubism must use its
     * premultiplied shader for these model textures, including fallback. */
    if (model_texture) {
        size_t count = (size_t)image->width * image->height;
        for (size_t i = 0; i < count; ++i) {
            unsigned char *pixel = image->pixels + i * 4;
            for (int c = 0; c < 3; ++c)
                pixel[c] = (unsigned char)((pixel[c] * pixel[3] + 127) / 255);
        }
    }
    bool created = texture == 0;
    if (created) glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (created) {
        /* Keep direct image/UI textures on the inexpensive linear path. Model
         * textures switch to alpha-safe trilinear filtering after upload. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (model_texture && SDL_GL_ExtensionSupported(
            "GL_EXT_texture_filter_anisotropic")) {
            GLfloat maximum = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
            if (maximum > 1.0f)
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    SDL_min(maximum, 8.0f));
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (created && model_texture) {
        if (!bongo_cat_image_upload_mipmaps(image)) {
            bongo_cat_gl_clear_errors();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width,
                image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        }
    } else if (created) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width,
            image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image->width, image->height,
            GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return texture;
}
unsigned int bongo_cat_image_texture(const char *path, int *width, int *height, BongoCatError *error) {
    BongoCatImage image;
    if (bongo_cat_image_load(path, &image, error) != BONGO_CAT_OK) return 0;
    GLuint texture = upload(&image, 0, false);
    if (width) *width = image.width;
    if (height) *height = image.height;
    bongo_cat_image_free(&image);
    return texture;
}

unsigned int bongo_cat_image_texture_thumbnail(const char *path, int max_width,
    int max_height, int *width, int *height, BongoCatError *error) {
    BongoCatImage image;
#ifdef _WIN32
    if (max_width > 0 && max_height > 0 &&
        bongo_cat_image_decode_wic_responsive(path, &image,
            max_width, max_height, NULL, NULL)) {
        GLuint texture = upload(&image, 0, false);
        if (width) *width = image.width;
        if (height) *height = image.height;
        bongo_cat_image_free(&image);
        return texture;
    }
#endif
    if (bongo_cat_image_load(path, &image, error) != BONGO_CAT_OK) return 0;
    int target_width = image.width, target_height = image.height;
    if (max_width > 0 && max_height > 0 &&
        (target_width > max_width || target_height > max_height)) {
        float scale = SDL_min((float)max_width / target_width,
            (float)max_height / target_height);
        target_width = SDL_max(1, (int)(target_width * scale + .5f));
        target_height = SDL_max(1, (int)(target_height * scale + .5f));
        SDL_Surface *scaled = SDL_ScaleSurface(image.surface, target_width,
            target_height, SDL_SCALEMODE_LINEAR);
        if (scaled) {
            BongoCatImage thumbnail = {
                .pixels = scaled->pixels, .width = scaled->w, .height = scaled->h};
            GLuint texture = upload(&thumbnail, 0, false);
            if (width) *width = thumbnail.width;
            if (height) *height = thumbnail.height;
            SDL_DestroySurface(scaled);
            bongo_cat_image_free(&image);
            return texture;
        }
        target_width = image.width;
        target_height = image.height;
    }
    GLuint texture = upload(&image, 0, false);
    if (width) *width = target_width;
    if (height) *height = target_height;
    bongo_cat_image_free(&image);
    return texture;
}
typedef struct ImageProgressStage {
    BongoCatImageProgress progress;
    void *userdata;
    float start;
    float span;
} ImageProgressStage;

static void report_stage_progress(void *userdata, float progress) {
    ImageProgressStage *stage = userdata;
    if (stage && stage->progress)
        stage->progress(stage->userdata, stage->start + stage->span * progress);
}

/* stb decodes a model atlas at full resolution, so an oversized sheet still
   costs its whole pixel buffer transiently. Constraining the pixels before the
   alpha mask and the upload bounds what stays resident in the GL texture and
   its mipmap chain for as long as the model is loaded. Leaves the image
   untouched and reports failure when the resize cannot be allocated. */
static bool constrain_model_texture(BongoCatImage *image, int limit) {
    if (!image || !image->pixels || limit < 1 ||
        (image->width <= limit && image->height <= limit)) return true;
    float scale = SDL_min((float)limit / (float)image->width,
        (float)limit / (float)image->height);
    int target_width = SDL_max(1, (int)lroundf((float)image->width * scale));
    int target_height = SDL_max(1, (int)lroundf((float)image->height * scale));
    size_t count = (size_t)target_width * (size_t)target_height;
    unsigned char *pixels = count <= SIZE_MAX / 4 ? malloc(count * 4) : NULL;
    if (!pixels || !stbir_resize_uint8_srgb(image->pixels, image->width,
        image->height, image->width * 4, pixels, target_width, target_height,
        target_width * 4, STBIR_RGBA)) {
        free(pixels);
        return false;
    }
    unsigned char *previous_pixels = image->pixels;
    bool previous_stbi = image->pixels_stbi;
    SDL_Surface *previous_surface = image->surface;
    image->pixels = pixels;
    image->pixels_stbi = false;
    image->surface = NULL;
    image->width = target_width;
    image->height = target_height;
    if (previous_surface) SDL_DestroySurface(previous_surface);
    if (previous_stbi) stbi_image_free(previous_pixels);
    else free(previous_pixels);
    return true;
}

unsigned int bongo_cat_image_texture_model(const char *path, bool direct_decode,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error) {
    BongoCatImage image = {0};
    ImageProgressStage stage = {progress, userdata, 0.0f, .30f};
    BongoCatImageProgress staged = progress ? report_stage_progress : NULL;
    GLint hardware_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &hardware_limit);
    if (hardware_limit < 1) hardware_limit = BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT;
#ifdef _WIN32
    /*
     * A model atlas can advertise 8192px even when the pet is rendered in a
     * few hundred pixels. Decoding that atlas at full size costs hundreds of
     * MB of temporary RAM and makes alpha-mask generation and glTexImage2D
     * dominate model switching. Keep a bounded working size for normal loads;
     * this is still well above the largest bundled 1024px atlas. The hardware
     * limit remains authoritative on low-end GPUs.
     */
    int decode_limit = SDL_min(hardware_limit,
        BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT);
    /* Preset atlases are verified byte-identical in WIC and stb. Keep WIC for
       custom PNG color metadata and for memory-bounded oversized decoding. */
    bool scaled = bongo_cat_image_needs_wic_scaling(path, decode_limit);
    if (!direct_decode || scaled) {
        stage.span = .20f;
        if (!bongo_cat_image_decode_wic_responsive(path, &image,
            decode_limit, decode_limit, staged, &stage)) {
            stage = (ImageProgressStage){progress, userdata, .20f, .10f};
            if (bongo_cat_image_decode_pixels_responsive(path, &image,
                staged, &stage, error) != BONGO_CAT_OK) return 0;
        }
    } else if (bongo_cat_image_decode_pixels_responsive(path, &image,
        staged, &stage, error) != BONGO_CAT_OK) return 0;
    if (scaled) SDL_Log("Live2D texture constrained to %dx%d by a %d pixel "
        "texture limit: %s", image.width, image.height, decode_limit, path);
    else if (image.width > BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT ||
        image.height > BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT)
        SDL_Log("High-resolution Live2D texture preserved at %dx%d: %s",
            image.width, image.height, path);
#else
    (void)direct_decode;
    if (bongo_cat_image_decode_pixels_responsive(path, &image,
        staged, &stage, error) != BONGO_CAT_OK) return 0;
    /* WIC scales an oversized atlas while decoding; stb cannot, so the same
       bound is applied to the decoded pixels. Only the resident texture and
       its mipmap chain shrink; the decode peak is unchanged. */
    int efficient_limit = SDL_min(hardware_limit,
        BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT);
    if (image.width > efficient_limit || image.height > efficient_limit) {
        if (constrain_model_texture(&image, efficient_limit))
            SDL_Log("Live2D texture constrained to %dx%d by a %d pixel "
                "texture limit: %s", image.width, image.height,
                efficient_limit, path);
        else SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Live2D texture kept at "
            "%dx%d because constraining it to %d pixels failed: %s",
            image.width, image.height, efficient_limit, path);
    }
#endif
    if (progress) progress(userdata, .30f);
    stage = (ImageProgressStage){progress, userdata, .30f, .30f};
    bongo_cat_image_make_alpha_mask_progress(&image, alpha, staged, &stage);
    bongo_cat_gl_clear_errors();
    GLuint texture = upload(&image, 0, true);
    GLenum upload_error = glGetError();
#ifdef _WIN32
    if (upload_error == GL_OUT_OF_MEMORY &&
        (image.width > BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT ||
            image.height > BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT)) {
        if (texture) glDeleteTextures(1, &texture);
        texture = 0;
        bongo_cat_image_free(&image);
        int fallback_limit = SDL_min(hardware_limit,
            BONGO_CAT_LIVE2D_EFFICIENT_TEXTURE_LIMIT);
        stage = (ImageProgressStage){progress, userdata, .90f, .04f};
        if (bongo_cat_image_decode_wic_responsive(path, &image,
            fallback_limit, fallback_limit, staged, &stage)) {
            stage = (ImageProgressStage){progress, userdata, .94f, .03f};
            bongo_cat_image_make_alpha_mask_progress(
                &image, alpha, staged, &stage);
            bongo_cat_gl_clear_errors();
            texture = upload(&image, 0, true);
            upload_error = glGetError();
            if (upload_error == GL_NO_ERROR) SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Live2D texture fell back to %dx%d after full-resolution "
                "GPU upload exhausted memory: %s", image.width, image.height, path);
        }
    }
#endif
    if (!texture || upload_error != GL_NO_ERROR) {
        if (texture) glDeleteTextures(1, &texture);
        texture = 0;
        bongo_cat_error_set(error, upload_error == GL_OUT_OF_MEMORY
            ? BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
            "OpenGL texture upload failed (0x%x): %s", (unsigned)upload_error, path);
    }
    if (width) *width = image.width;
    if (height) *height = image.height;
    bongo_cat_image_free(&image);
    if (progress) progress(userdata, 1.0f);
    return texture;
}

static void erase_paw(BongoCatImage *image, bool left) {
    float cx = left ? .700f : .275f, cy = left ? .515f : .397f;
    float rx = left ? .080f : .070f, ry = left ? .170f : .160f;
    for (int y = 0; y < image->height; ++y) for (int x = 0; x < image->width; ++x) {
        float dx = (x / (float)image->width - cx) / rx;
        float dy = (y / (float)image->height - cy) / ry;
        unsigned char *pixel = image->pixels + ((size_t)y * image->width + x) * 4;
        if (dx * dx + dy * dy < 1.0f && pixel[3])
            pixel[0] = pixel[1] = pixel[2] = 255;
    }
}

static bool blend_file(BongoCatImage *base, const char *path, BongoCatError *error) {
    if (!path || !path[0]) return true;
    BongoCatImage layer;
    if (bongo_cat_image_load(path, &layer, error) != BONGO_CAT_OK) return false;
    bool valid = layer.width == base->width && layer.height == base->height;
    if (valid) for (int i = 0; i < base->width * base->height; ++i) {
        unsigned char *dst = base->pixels + i * 4, *src = layer.pixels + i * 4;
        unsigned alpha = src[3], inverse = 255 - alpha, destination_alpha = dst[3];
        unsigned output_alpha = alpha * 255 + destination_alpha * inverse;
        if (!output_alpha) { memset(dst, 0, 4); continue; }
        for (int channel = 0; channel < 3; ++channel) {
            unsigned color = src[channel] * alpha * 255 +
                dst[channel] * destination_alpha * inverse;
            dst[channel] = (unsigned char)((color + output_alpha / 2) / output_alpha);
        }
        dst[3] = (unsigned char)((output_alpha + 127) / 255);
    }
    bongo_cat_image_free(&layer);
    return valid;
}

unsigned int bongo_cat_image_composite_texture(const char *base, const char *left,
    const char *right, unsigned int texture, bool erase_left, bool erase_right,
    BongoCatError *error) {
    BongoCatImage image;
    if (bongo_cat_image_load(base, &image, error) != BONGO_CAT_OK) return 0;
    if (erase_left) erase_paw(&image, true);
    if (erase_right) erase_paw(&image, false);
    bool valid = blend_file(&image, left, error) && blend_file(&image, right, error);
    if (valid) texture = upload(&image, texture, false);
    bongo_cat_image_free(&image);
    return texture;
}
