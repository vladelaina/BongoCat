#pragma once

#include <GL/glew.h>

namespace bongo_cat {

struct CoreProfileBuffers {
    GLuint vao = 0;
    GLuint buffers[3] = {};

    CoreProfileBuffers() = default;
    CoreProfileBuffers(const CoreProfileBuffers &) = delete;
    CoreProfileBuffers &operator=(const CoreProfileBuffers &) = delete;

    void release() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(3, buffers);
        vao = 0;
        for (auto &buffer : buffers) buffer = 0;
    }
};

// SDK callbacks use the buffers of the model currently being drawn.
class CoreProfileBinding {
public:
    explicit CoreProfileBinding(CoreProfileBuffers &resources)
        : resources_(resources), previous_(active_) {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_);
        if (!resources_.vao) glGenVertexArrays(1, &resources_.vao);
        if (!resources_.buffers[0]) glGenBuffers(3, resources_.buffers);
        glBindVertexArray(resources_.vao);
        active_ = this;
    }

    ~CoreProfileBinding() {
        glBindVertexArray((GLuint)previous_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_);
        active_ = previous_;
    }

    CoreProfileBinding(const CoreProfileBinding &) = delete;
    CoreProfileBinding &operator=(const CoreProfileBinding &) = delete;

    static void attribute(unsigned slot, GLuint location,
        const void *data, GLsizeiptr size) {
        glBindBuffer(GL_ARRAY_BUFFER, active_->resources_.buffers[slot]);
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STREAM_DRAW);
        glVertexAttribPointer(location, 2, GL_FLOAT, GL_FALSE,
            sizeof(GLfloat) * 2, nullptr);
    }

    static void draw(GLsizei count, const GLushort *indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, active_->resources_.buffers[2]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(GLushort),
            indices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr);
    }

private:
    CoreProfileBuffers &resources_;
    CoreProfileBinding *previous_;
    GLint previous_vao_ = 0;
    GLint previous_array_ = 0;
    inline static thread_local CoreProfileBinding *active_ = nullptr;
};

} // namespace bongo_cat
