#ifndef BONGO_CAT_CUBISM_CORE_PROFILE_HPP
#define BONGO_CAT_CUBISM_CORE_PROFILE_HPP

#if defined(CSM_TARGET_MAC_GL)
#include <GL/glew.h>
#include <SDL3/SDL_video.h>

struct BongoCatError;

namespace bongo_cat {
// Owned by one NativeModel; the SDK borrows these buffers only during DrawModel.
class CubismCoreProfile {
public:
    bool create(BongoCatError *error);
    void release();
    bool begin();
    void end();
    static void attribute(GLuint location, GLsizei count, const float *data,
        unsigned stream);
    static void draw(GLsizei count, const unsigned short *indices);
private:
    GLuint vao_ = 0;
    GLuint buffers_[3]{};
    GLint previous_vao_ = 0;
    SDL_GLContext context_ = nullptr;
    CubismCoreProfile *previous_ = nullptr;
    static thread_local CubismCoreProfile *active_;
};
} // namespace bongo_cat
#endif
#endif
