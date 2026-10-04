#ifndef BONGO_CAT_CUBISM_MODEL_TEXTURE_HPP
#define BONGO_CAT_CUBISM_MODEL_TEXTURE_HPP

#include "bongo_cat/image.h"
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

namespace bongo_cat {
struct ModelTexture {
    GLuint id = 0;
    SDL_GLContext context = nullptr;
    bool direct = false;
    bool dynamic_resolution = false;
    int max_width = 0, max_height = 0;
    int width = 0, height = 0;
    int source_width = 0, source_height = 0;
    char digest[65]{};
    BongoCatImageAlphaMask alpha{};
    ~ModelTexture() { if (id) glDeleteTextures(1, &id); }
};
}
#endif
