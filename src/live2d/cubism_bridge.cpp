#include "bongo_cat/file.h"
#include "bongo_cat/model.h"
#if defined(CSM_TARGET_WIN_GL) || defined(CSM_TARGET_LINUX_GL) || defined(CSM_TARGET_MAC_GL)
#include <GL/glew.h>
#endif
#include "cubism_runtime.hpp"

#include <CubismFramework.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>
#include <cstdio>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif
using Csm::CubismFramework;

namespace {
class Allocator final : public Csm::ICubismAllocator {
public:
    void *Allocate(const Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void *memory) override { std::free(memory); }
    void *AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override {
#ifdef _WIN32
        return _aligned_malloc(size, alignment);
#else
        void *memory = nullptr;
        return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
#endif
    }
    void DeallocateAligned(void *memory) override {
#ifdef _WIN32
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }
};

Allocator allocator;
CubismFramework::Option framework_option;
std::string resource_root;
int runtime_count;

void log_message(const char *message) {
    if (message) std::fprintf(stderr, "[Cubism] %s\n", message);
}

Csm::csmByte *load_file(const std::string path, Csm::csmSizeInt *size) {
    if (size) *size = 0;
    FILE *file = bongo_cat_file_open(path.c_str(), "rb");
    if (!file) {
        const char *base = SDL_GetBasePath();
        if (base) file = bongo_cat_file_open((std::string(base) + path).c_str(), "rb");
    }
    if (!file && !resource_root.empty())
        file = bongo_cat_file_open((resource_root + "/" + path).c_str(), "rb");
    if (!file) return nullptr;
    std::fseek(file, 0, SEEK_END);
    long length = std::ftell(file);
    std::rewind(file);
    if (length <= 0) { std::fclose(file); return nullptr; }
    auto *bytes = static_cast<Csm::csmByte *>(std::malloc((size_t)length));
    if (!bytes || std::fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        std::free(bytes);
        bytes = nullptr;
    } else if (size) *size = (Csm::csmSizeInt)length;
    std::fclose(file);
    return bytes;
}

void release_file(Csm::csmByte *bytes) { std::free(bytes); }

bool start_framework(BongoCatError *error) {
    if (runtime_count++) return true;
#if defined(CSM_TARGET_WIN_GL) || defined(CSM_TARGET_LINUX_GL) || defined(CSM_TARGET_MAC_GL)
    glewExperimental = GL_TRUE;
    GLenum glew_result = glewInit();
    glGetError();
    if (glew_result != GLEW_OK) {
        runtime_count = 0;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "GLEW initialization failed: %s",
            reinterpret_cast<const char *>(glewGetErrorString(glew_result)));
        return false;
    }
    if (!glCreateShader || !glShaderSource || !glCompileShader ||
        !glGetShaderiv || !glCreateProgram || !glGenFramebuffers ||
        !glGenBuffers || !glBindBuffer || !glBufferData || !glDeleteBuffers ||
        !glGenVertexArrays || !glBindVertexArray || !glDeleteVertexArrays ||
        !glEnableVertexAttribArray || !glVertexAttribPointer) {
        runtime_count = 0;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Required OpenGL 3.3 functions are unavailable");
        return false;
    }
#endif
    framework_option = CubismFramework::Option{};
    framework_option.LogFunction = log_message;
    framework_option.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
    framework_option.LoadFileFunction = load_file;
    framework_option.ReleaseBytesFunction = release_file;
    if (!CubismFramework::StartUp(&allocator, &framework_option)) {
        runtime_count = 0;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM, "Cubism Framework startup failed");
        return false;
    }
    CubismFramework::Initialize();
    return true;
}

void stop_framework() {
    if (--runtime_count > 0) return;
    CubismFramework::Dispose();
    CubismFramework::CleanUp();
    runtime_count = 0;
}

} // namespace

static BongoCatLive2D *create_runtime(const char *asset_root,
    BongoCatError *error) {
    resource_root = asset_root ? asset_root : "";
    if (!start_framework(error)) return nullptr;
    BongoCatLive2D *runtime = new(std::nothrow) BongoCatLive2D{};
    if (!runtime) {
        stop_framework();
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate Cubism runtime");
    }
    return runtime;
}

extern "C" BongoCatLive2D *bongo_cat_live2d_create(const char *asset_root,
    BongoCatError *error) {
    return create_runtime(asset_root, error);
}

extern "C" void bongo_cat_live2d_destroy(BongoCatLive2D *runtime) {
    if (!runtime) return;
    if (runtime->retired_count) SDL_Log("[runtime] Live2D resource handoff: "
        "stage=retirement-flush queue=%u current_context=%p",
        runtime->retired_count, (void *)SDL_GL_GetCurrentContext());
    for (unsigned i = 0; i < runtime->retired_count; ++i)
        delete runtime->retired[i].model;
    delete runtime->model;
    delete runtime;
    stop_framework();
}

extern "C" bool bongo_cat_live2d_ready(const BongoCatLive2D *runtime) {
    return runtime && runtime->model;
}

extern "C" bool bongo_cat_live2d_canvas_size(const BongoCatLive2D *runtime,
    int *width, int *height) {
    return runtime && runtime->model &&
        runtime->model->canvas_size(width, height);
}

extern "C" bool bongo_cat_live2d_frame(const BongoCatLive2D *runtime,
    BongoCatLive2DFrame *frame) {
    return runtime && runtime->model && runtime->model->frame(frame);
}

extern "C" bool bongo_cat_live2d_viewport(const BongoCatLive2D *runtime,
    int *x, int *y, int *width, int *height) {
    return runtime && runtime->model &&
        runtime->model->viewport(x, y, width, height);
}

extern "C" void bongo_cat_live2d_resize(BongoCatLive2D *runtime, int width, int height) {
    if (!runtime) return;
    if (width > 0 && height > 0) {
        runtime->width = width;
        runtime->height = height;
    }
    if (runtime->model) runtime->model->resize(width, height);
}
extern "C" void bongo_cat_live2d_reshape(BongoCatLive2D *runtime, int width, int height) {
    if (!runtime) return;
    if (width > 0 && height > 0) {
        runtime->width = width;
        runtime->height = height;
    }
    if (runtime->model) runtime->model->reshape(width, height);
}
extern "C" bool bongo_cat_live2d_update(BongoCatLive2D *runtime, float elapsed) {
    if (!runtime) return false;
    bool changed = runtime->model && runtime->model->update(elapsed);
    // Finish deferred releases even when the replacement model is static.
    return changed || runtime->retired_count > 0;
}
extern "C" void bongo_cat_live2d_draw(BongoCatLive2D *runtime) {
    if (!runtime) return;
    unsigned keep = 0, released = 0;
    for (unsigned i = 0; i < runtime->retired_count; ++i) {
        BongoCatRetiredModel item = runtime->retired[i];
        if (item.frames_remaining) item.frames_remaining--;
        if (!item.frames_remaining) {
            delete item.model;
            released++;
        } else runtime->retired[keep++] = item;
    }
    runtime->retired_count = keep;
    if (released) SDL_Log("[runtime] Live2D resource handoff: "
        "stage=retirement-complete released=%u queue=%u current_context=%p "
        "gl_error=0x%x", released, keep, (void *)SDL_GL_GetCurrentContext(),
        (unsigned)glGetError());
    if (runtime->model) runtime->model->draw();
}
extern "C" void bongo_cat_live2d_set_mirror(BongoCatLive2D *runtime, bool mirror) {
    if (runtime && runtime->model) runtime->model->set_mirror(mirror); }
extern "C" void bongo_cat_live2d_set_render_options(BongoCatLive2D *runtime,
    const BongoCatLive2DRenderOptions *options) {
    if (runtime && runtime->model && options)
        runtime->model->set_render_options(*options); }
extern "C" void bongo_cat_live2d_set_dragging(BongoCatLive2D *runtime,
    float x, float y) {
    if (runtime && runtime->model) runtime->model->set_dragging(x, y); }
extern "C" void bongo_cat_live2d_set_centered_dragging(BongoCatLive2D *runtime,
    float x, float y) {
    if (runtime && runtime->model) runtime->model->set_dragging(x, y, true); }
extern "C" void bongo_cat_live2d_prepare_viewer_audit(BongoCatLive2D *runtime) {
    if (runtime && runtime->model) runtime->model->prepare_viewer_audit();
}
extern "C" bool bongo_cat_live2d_prepare_cover_capture(
    BongoCatLive2D *runtime) {
    return runtime && runtime->model &&
        runtime->model->prepare_cover_capture();
}
extern "C" bool bongo_cat_live2d_set_parameter(BongoCatLive2D *runtime, const char *id, float value) {
    return runtime && runtime->model && runtime->model->set_parameter(id, value);
}
extern "C" bool bongo_cat_live2d_parameter(BongoCatLive2D *runtime, const char *id,
    BongoCatParameterRange *range) {
    return runtime && runtime->model && range && runtime->model->parameter(id,
        &range->minimum, &range->maximum, &range->value);
}
extern "C" bool bongo_cat_live2d_start_motion(BongoCatLive2D *runtime, const char *group, int index) {
    return runtime && runtime->model && runtime->model->start_motion(group, index);
}
extern "C" bool bongo_cat_live2d_restore_motion_state(BongoCatLive2D *runtime,
    const char *group, int index) {
    return runtime && runtime->model &&
        runtime->model->restore_motion_state(group, index);
}
extern "C" bool bongo_cat_live2d_preview_motion(BongoCatLive2D *runtime, const char *group, int index) {
    return runtime && runtime->model && runtime->model->preview_motion(group, index); }
extern "C" bool bongo_cat_live2d_restore_motion_preview(BongoCatLive2D *runtime) { return runtime && runtime->model && runtime->model->restore_motion_preview(); }
extern "C" bool bongo_cat_live2d_commit_motion_preview(BongoCatLive2D *runtime, const char *group, int index) {
    return runtime && runtime->model && runtime->model->commit_motion_preview(group, index); }
extern "C" bool bongo_cat_live2d_motion_selected(const BongoCatLive2D *runtime, const char *group, int index) { return runtime && runtime->model && runtime->model->motion_selected(group, index); }
extern "C" bool bongo_cat_live2d_motion_persistent(const BongoCatLive2D *runtime,
    const char *group, int index) {
    return runtime && runtime->model &&
        runtime->model->motion_persistent(group, index); }
extern "C" bool bongo_cat_live2d_motion_visible(const BongoCatLive2D *runtime, const char *group, int index) {
    return runtime && runtime->model && runtime->model->motion_visible(group, index); }
extern "C" bool bongo_cat_live2d_motion_same_toggle(
    const BongoCatLive2D *runtime, const char *left_group, int left_index,
    const char *right_group, int right_index) {
    return runtime && runtime->model && runtime->model->motion_same_toggle(
        left_group, left_index, right_group, right_index); }
extern "C" bool bongo_cat_live2d_set_expression(BongoCatLive2D *runtime, int index) {
    return runtime && runtime->model && runtime->model->set_expression(index); }
extern "C" int bongo_cat_live2d_expression(const BongoCatLive2D *runtime) {
    return runtime && runtime->model ? runtime->model->expression() : -1; }
