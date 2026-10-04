#include "cubism_core_profile.hpp"

#if defined(CSM_TARGET_MAC_GL)
#include "bongo_cat/common.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_assert.h>

namespace bongo_cat {
thread_local CubismCoreProfile *CubismCoreProfile::active_ = nullptr;

bool CubismCoreProfile::create(BongoCatError *error) {
    context_ = SDL_GL_GetCurrentContext();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(3, buffers_);
    GLenum result = glGetError();
    if (vao_ && buffers_[0] && buffers_[1] && buffers_[2] && result == GL_NO_ERROR)
        return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Cannot create Cubism core-profile buffers (OpenGL 0x%x)", (unsigned)result);
    release();
    return false;
}

void CubismCoreProfile::release() {
    if (!vao_ && !buffers_[0] && !buffers_[1] && !buffers_[2]) return;
    // The owning pet context also owns the model's textures and renderer.
    SDL_assert(SDL_GL_GetCurrentContext() == context_);
    glDeleteBuffers(3, buffers_);
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
    for (auto &buffer : buffers_) buffer = 0;
    context_ = nullptr;
}

bool CubismCoreProfile::begin() {
    if (!vao_ || SDL_GL_GetCurrentContext() != context_) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Cubism draw requires its owning OpenGL context");
        return false;
    }
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao_);
    glBindVertexArray(vao_);
    previous_ = active_;
    active_ = this;
    return true;
}

void CubismCoreProfile::end() {
    // SDK SaveProfile/RestoreProfile preserve other GL state inside this VAO.
    glBindVertexArray((GLuint)previous_vao_);
    active_ = previous_;
}

void CubismCoreProfile::attribute(GLuint location, GLsizei count,
    const float *data, unsigned stream) {
    SDL_assert(active_ && stream < 2);
    glBindBuffer(GL_ARRAY_BUFFER, active_->buffers_[stream]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * 2 * sizeof(float), data,
        GL_STREAM_DRAW);
    glVertexAttribPointer(location, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
}

void CubismCoreProfile::draw(GLsizei count, const unsigned short *indices) {
    SDL_assert(active_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, active_->buffers_[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(*indices),
        indices, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr);
}
} // namespace bongo_cat
#endif
