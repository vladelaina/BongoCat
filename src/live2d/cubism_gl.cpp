#include "cubism_gl.hpp"
#include <GL/glew.h>

bool bongo_cat_cubism_initialize_gl(BongoCatError *error) {
#if defined(CSM_TARGET_WIN_GL) || defined(CSM_TARGET_LINUX_GL) || defined(CSM_TARGET_MAC_GL)
    glewExperimental = GL_TRUE;
    GLenum glew_result = glewInit();
    glGetError();
    if (glew_result != GLEW_OK) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "GLEW initialization failed: %s",
            reinterpret_cast<const char *>(glewGetErrorString(glew_result)));
        return false;
    }
    // Cubism's desktop renderer uses these loaded entries before and during draw.
#define REQUIRE_GL(entry) if (!entry) { \
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, \
        "Required OpenGL function unavailable: %s", #entry); \
    return false; \
}
    REQUIRE_GL(glGenFramebuffers); REQUIRE_GL(glBindFramebuffer);
    REQUIRE_GL(glFramebufferTexture2D); REQUIRE_GL(glCheckFramebufferStatus);
    REQUIRE_GL(glDeleteFramebuffers); REQUIRE_GL(glBlitFramebuffer);
    REQUIRE_GL(glCreateShader); REQUIRE_GL(glShaderSource);
    REQUIRE_GL(glCompileShader); REQUIRE_GL(glGetShaderiv);
    REQUIRE_GL(glGetShaderInfoLog); REQUIRE_GL(glDeleteShader);
    REQUIRE_GL(glCreateProgram); REQUIRE_GL(glAttachShader);
    REQUIRE_GL(glDetachShader); REQUIRE_GL(glLinkProgram);
    REQUIRE_GL(glValidateProgram); REQUIRE_GL(glGetProgramiv);
    REQUIRE_GL(glGetProgramInfoLog); REQUIRE_GL(glDeleteProgram);
    REQUIRE_GL(glUseProgram); REQUIRE_GL(glGetAttribLocation);
    REQUIRE_GL(glGetUniformLocation); REQUIRE_GL(glUniform1i);
    REQUIRE_GL(glUniform4f); REQUIRE_GL(glUniformMatrix4fv);
    REQUIRE_GL(glActiveTexture); REQUIRE_GL(glBlendFuncSeparate);
    REQUIRE_GL(glBindBuffer); REQUIRE_GL(glGetVertexAttribiv);
    REQUIRE_GL(glEnableVertexAttribArray); REQUIRE_GL(glDisableVertexAttribArray);
    REQUIRE_GL(glVertexAttribPointer);
#if defined(GLEW_ARB_texture_barrier)
    if (GLEW_ARB_texture_barrier) { REQUIRE_GL(glTextureBarrier); }
#elif defined(GLEW_NV_texture_barrier)
    if (GLEW_NV_texture_barrier) { REQUIRE_GL(glTextureBarrier); }
#endif
#if defined(CSM_TARGET_MAC_GL)
    REQUIRE_GL(glGenVertexArrays); REQUIRE_GL(glBindVertexArray);
    REQUIRE_GL(glDeleteVertexArrays); REQUIRE_GL(glGenBuffers);
    REQUIRE_GL(glBufferData); REQUIRE_GL(glDeleteBuffers);
#endif
#undef REQUIRE_GL
#endif
    return true;
}
