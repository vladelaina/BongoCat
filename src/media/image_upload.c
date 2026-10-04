#include "image_internal.h"
#include "image_upload_sync.h"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

typedef struct UploadState {
    GLint texture, buffer, alignment, row_length, skip_pixels, skip_rows;
    PFNGLBINDBUFFERPROC bind_buffer;
} UploadState;

static bool begin_upload(UploadState *state) {
    state->bind_buffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    if (!state->bind_buffer) return false;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state->buffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state->row_length);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state->skip_pixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state->skip_rows);
    state->bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    return true;
}

static void end_upload(const UploadState *state) {
    glBindTexture(GL_TEXTURE_2D, (GLuint)state->texture);
    state->bind_buffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)state->buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, state->alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, state->row_length);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, state->skip_pixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, state->skip_rows);
}

static GLuint create_texture(bool model) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* Match Cubism's atlas addressing, including mask-source draws. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, model ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, model ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (model && SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        GLfloat maximum = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
        if (maximum > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                SDL_min(maximum, 8.0f));
    }
    return texture;
}

unsigned int bongo_cat_image_begin_model_texture(int width, int height,
    bool mipmaps, BongoCatError *error) {
    UploadState state = {0};
    if (!begin_upload(&state)) return 0;
    GLuint texture = create_texture(true);
    /* Allocate the complete chain before filling any texels. Growing mutable
       base storage during GenerateMipmap can temporarily duplicate the whole
       atlas. Immutable storage prevents that driver-side allocation peak. */
    PFNGLTEXSTORAGE2DPROC storage = mipmaps &&
        SDL_GL_ExtensionSupported("GL_ARB_texture_storage") ?
        (PFNGLTEXSTORAGE2DPROC)SDL_GL_GetProcAddress("glTexStorage2D") : NULL;
    if (texture && storage) {
        int levels = 1;
        for (int size = SDL_max(width, height); size > 1; size /= 2) ++levels;
        storage(GL_TEXTURE_2D, levels, GL_RGBA8, width, height);
        if (glGetError() == GL_NO_ERROR) {
            end_upload(&state);
            return texture;
        }
        // Preserve the original-size mutable path on older/limited drivers.
        glDeleteTextures(1, &texture);
        bongo_cat_gl_clear_errors();
        texture = create_texture(true);
    }
    if (texture)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GLenum status = glGetError();
    if (!texture || status != GL_NO_ERROR) {
        if (texture) glDeleteTextures(1, &texture);
        texture = 0;
        bongo_cat_error_set(error, status == GL_OUT_OF_MEMORY
            ? BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
            "Live2D texture allocation failed (%dx%d, 0x%x)",
            width, height, (unsigned)status);
    }
    end_upload(&state);
    return texture;
}

void bongo_cat_image_release_upload_buffer(BongoCatImageUploadBuffer *buffer) {
    if (!buffer) return;
    if (buffer->framebuffer) {
        PFNGLDELETEFRAMEBUFFERSPROC remove =
            (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
        if (remove) remove(1, &buffer->framebuffer);
    }
    if (buffer->texture) glDeleteTextures(1, &buffer->texture);
    *buffer = (BongoCatImageUploadBuffer){0};
}

/* Upload through a small RGBA8 surface, then copy texels on the GPU. Some
   drivers map the entire destination for even a small TexSubImage update,
   retaining a full-atlas transfer buffer (256 MiB for an 8192 atlas). Keeping
   CPU transfers on this strip bounds that extra allocation without changing
   the destination format, dimensions, texels or mip generation. */
static GLenum copy_model_rows(GLuint texture, const BongoCatImage *rows, int y,
    BongoCatImageUploadBuffer *buffer, BongoCatImageUploadSync *sync) {
    PFNGLGENFRAMEBUFFERSPROC generate =
        (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    PFNGLBINDFRAMEBUFFERPROC bind =
        (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    PFNGLFRAMEBUFFERTEXTURE2DPROC attach =
        (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    PFNGLCHECKFRAMEBUFFERSTATUSPROC check =
        (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    if (!generate || !bind || !attach || !check) return GL_INVALID_OPERATION;
    GLint previous_read = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
    bool ready = true;
    if (!buffer->texture) {
        glGenTextures(1, &buffer->texture);
        glBindTexture(GL_TEXTURE_2D, buffer->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rows->width, rows->height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        buffer->width = rows->width;
        buffer->height = rows->height;
        generate(1, &buffer->framebuffer);
        if (buffer->texture && buffer->framebuffer) {
            bind(GL_READ_FRAMEBUFFER, buffer->framebuffer);
            attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, buffer->texture, 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            ready = check(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        } else ready = false;
    }
    GLenum status = glGetError();
    ready = ready && buffer->width == rows->width &&
        rows->height <= buffer->height && status == GL_NO_ERROR;
    if (ready) {
        glBindTexture(GL_TEXTURE_2D, buffer->texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rows->width, rows->height,
            GL_RGBA, GL_UNSIGNED_BYTE, rows->pixels);
        bind(GL_READ_FRAMEBUFFER, buffer->framebuffer);
        glBindTexture(GL_TEXTURE_2D, texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, 0, 0,
            rows->width, rows->height);
        /* Reusing a strip while its copy is still queued makes some drivers
           retain one renamed surface per strip, recreating a full atlas of
           temporary storage. Loaders must check completion before reusing
           the strip; contexts without fences finish synchronously here. */
        status = glGetError();
        if (status == GL_NO_ERROR)
            status = bongo_cat_image_upload_sync_submit(sync);
    }
    bind(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
    return status != GL_NO_ERROR ? status :
        (ready ? GL_NO_ERROR : GL_INVALID_OPERATION);
}

bool bongo_cat_image_upload_model_rows(unsigned int texture,
    BongoCatImage *rows, int y, BongoCatImageUploadBuffer *buffer,
    BongoCatError *error) {
    bongo_cat_image_premultiply(rows);
    return bongo_cat_image_upload_model_rows_prepared(texture, rows, y,
        buffer, NULL, error);
}

bool bongo_cat_image_upload_model_rows_prepared(unsigned int texture,
    const BongoCatImage *rows, int y, BongoCatImageUploadBuffer *buffer,
    BongoCatImageUploadSync *sync, BongoCatError *error) {
    if (sync && sync->fence) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Texture staging strip is still in use");
        return false;
    }
    UploadState state = {0};
    if (!begin_upload(&state)) return false;
    GLenum status = copy_model_rows(texture, rows, y, buffer, sync);
    GLenum restore_status = glGetError();
    if (status == GL_NO_ERROR) status = restore_status;
    end_upload(&state);
    if (status != GL_NO_ERROR)
        bongo_cat_error_set(error, status == GL_OUT_OF_MEMORY
            ? BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
            "Live2D texture row upload failed (row %d, 0x%x)", y, (unsigned)status);
    return status == GL_NO_ERROR;
}

bool bongo_cat_image_finish_model_texture(unsigned int texture,
    BongoCatError *error) {
    UploadState state = {0};
    if (!begin_upload(&state)) return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    bool ok = bongo_cat_image_generate_mipmaps();
    end_upload(&state);
    if (!ok)
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Live2D mipmap generation failed");
    return ok;
}

unsigned int bongo_cat_image_upload_texture(BongoCatImage *image,
    unsigned int existing, bool model, BongoCatError *error) {
    if (!image || !image->pixels || image->width < 1 || image->height < 1 ||
        (model && existing)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Invalid image upload");
        return 0;
    }
    if (!SDL_GL_GetCurrentContext() || !bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Image upload requires a usable OpenGL context");
        return 0;
    }
    UploadState state = {0};
    if (!begin_upload(&state)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Image upload requires OpenGL buffer bindings");
        return 0;
    }
    if (model) bongo_cat_image_premultiply(image);
    GLuint texture = existing ? existing : create_texture(model);
    if (texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        if (existing) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image->width, image->height,
                GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        } else if (model && !bongo_cat_image_upload_mipmaps(image)) {
            // A partial mip chain must not retain storage after recovery.
            glDeleteTextures(1, &texture);
            bongo_cat_gl_clear_errors();
            texture = create_texture(true);
            if (texture)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Live2D mipmap upload unavailable; retrying original %dx%d pixels",
                image->width, image->height);
        } else if (!model) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
                0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        }
    }
    GLenum status = glGetError();
    if (!texture || status != GL_NO_ERROR) {
        if (texture && !existing) glDeleteTextures(1, &texture);
        texture = 0;
        bongo_cat_error_set(error, status == GL_OUT_OF_MEMORY
            ? BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
            "%s texture upload failed (%dx%d, 0x%x)", model ? "Live2D" : "Image",
            image->width, image->height, (unsigned)status);
    }
    end_upload(&state);
    return texture;
}
