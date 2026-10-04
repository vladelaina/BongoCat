#include "image_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

bool bongo_cat_image_upload_mipmaps(const BongoCatImage *image) {
    if (!image || !image->pixels || image->width < 1 || image->height < 1)
        return false;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    if (glGetError() != GL_NO_ERROR) return false;

    return bongo_cat_image_generate_mipmaps();
}

bool bongo_cat_image_generate_mipmaps(void) {
    /* Premultiplied RGBA8 can be filtered directly, including translucent
       edges. Build once on the GPU, without CPU copies of every mip level. */
    PFNGLGENERATEMIPMAPPROC generate =
        (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmap");
    if (!generate) return false;
    generate(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) return false;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    return glGetError() == GL_NO_ERROR;
}
