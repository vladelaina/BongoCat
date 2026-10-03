#include "cubism_model.hpp"
#include "cubism_model_texture.hpp"
#include "cubism_texture_resolution.hpp"
#include "bongo_cat/model_memory.h"
extern "C" {
#include "bongo_cat/sha256.h"
}

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cstring>

namespace bongo_cat {

// Only live model owners retain texture storage. Weak entries allow identical
// atlases in different model/mode directories to share it during handoff.
static std::vector<std::weak_ptr<ModelTexture>> live_textures;

struct TextureHashJob {
    const char *path;
    char *digest;
    BongoCatResult result = BONGO_CAT_ERROR_IO;
};

static int SDLCALL hash_worker(void *userdata) {
    auto &job = *static_cast<TextureHashJob *>(userdata);
    job.result = bongo_cat_sha256_file(job.path, job.digest, nullptr);
    return (int)job.result;
}

static bool hash_texture(const std::string &path, char digest[65],
    BongoCatImageProgress progress, void *userdata) {
    TextureHashJob job{path.c_str(), digest};
    SDL_Thread *worker = progress ? SDL_CreateThread(hash_worker,
        BONGO_CAT_SLUG "-texture-hash", &job) : nullptr;
    if (worker) {
        const auto wait = [](SDL_Thread *thread) { SDL_WaitThread(thread, nullptr); };
        std::unique_ptr<SDL_Thread, decltype(wait)> joined(worker, wait);
        while (SDL_GetThreadState(worker) == SDL_THREAD_ALIVE) {
            progress(userdata, 0.0f);
            SDL_Delay(2);
        }
    } else hash_worker(&job);
    return job.result == BONGO_CAT_OK;
}

static bool same_source(const std::string &path, const SDL_PathInfo &before) {
    SDL_PathInfo after{};
    return SDL_GetPathInfo(path.c_str(), &after) && after.type == SDL_PATHTYPE_FILE &&
        before.size == after.size && before.modify_time == after.modify_time &&
        before.create_time == after.create_time;
}

static bool full_sampling(GLuint texture) {
    GLint previous = 0, filter = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous);
    // A temporary allocation failure must not make later loads inherit the
    // original-size linear fallback when a full mip chain could succeed.
    return filter == GL_LINEAR_MIPMAP_LINEAR;
}

static std::shared_ptr<ModelTexture> acquire_texture(const std::string &path,
    bool direct, bool dynamic_resolution, float render_quality_percent,
    int display_width, int display_height,
    int reference_width, int reference_height, int texture_limit,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error) {
    live_textures.erase(std::remove_if(live_textures.begin(), live_textures.end(),
        [](const auto &entry) { return entry.expired(); }), live_textures.end());
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    SDL_PathInfo before{};
    char digest[65]{};
    int source_width = 0, source_height = 0;
    bongo_cat_image_info(path.c_str(), &source_width, &source_height);
    TextureResolution resolution = texture_resolution_for(dynamic_resolution,
        display_width, display_height, reference_width, reference_height,
        source_width, source_height, texture_limit, render_quality_percent);
    const char *resolution_reason = render_quality_percent < 100 ?
        (dynamic_resolution ? "display-and-quality-bound" : "quality-bound") :
        !dynamic_resolution ? "disabled" :
        source_width <= 0 || source_height <= 0 ? "source-size-unknown" :
        resolution.resized ? "display-bound" : "original-size-needed";
    bongo_cat_model_memory_log("texture-plan",
        "dynamic=%d direct=%d display=%dx%d reference=%dx%d source=%dx%d "
        "bound=%dx%d gpu_limit=%d quality=%.1f reason=%s original_rgba8_mips_est_mib=%.1f "
        "bound_rgba8_mips_est_mib=%.1f file=%s",
        dynamic_resolution ? 1 : 0, direct ? 1 : 0,
        display_width, display_height, reference_width, reference_height,
        source_width, source_height, resolution.max_width,
        resolution.max_height, texture_limit, render_quality_percent,
        resolution_reason,
        bongo_cat_model_texture_mib(source_width, source_height, true),
        bongo_cat_model_texture_mib(resolution.max_width, resolution.max_height, true),
        path.c_str());
    // Hash the bytes on every acquisition: edits and same-name replacements
    // must never reuse stale pixels, even when their size/timestamp matches.
    bool reusable = context && SDL_GetPathInfo(path.c_str(), &before) &&
        before.type == SDL_PATHTYPE_FILE &&
        hash_texture(path, digest, progress, userdata) &&
        same_source(path, before);
    if (reusable) {
        for (const auto &entry : live_textures) {
            auto texture = entry.lock();
            if (texture && texture->context == context && texture->direct == direct &&
                texture->dynamic_resolution == dynamic_resolution &&
                texture->max_width == resolution.max_width &&
                texture->max_height == resolution.max_height &&
                std::strcmp(texture->digest, digest) == 0) {
                bongo_cat_model_memory_log("texture-ready",
                    "cache=hit actual=%dx%d rgba8_est_mib=%.1f",
                    texture->width, texture->height,
                    bongo_cat_model_texture_mib(texture->width, texture->height, true));
                SDL_Log("Live2D texture shared: %s", path.c_str());
                return texture;
            }
        }
    }
    // Separate the object from its weak control block so expired registry
    // entries cannot retain the 16 KiB alpha mask after the last owner leaves.
    std::shared_ptr<ModelTexture> texture(new ModelTexture);
    texture->context = context;
    texture->direct = direct;
    texture->dynamic_resolution = dynamic_resolution;
    texture->max_width = resolution.max_width;
    texture->max_height = resolution.max_height;
    int loaded_width = 0, loaded_height = 0;
    texture->id = (dynamic_resolution || render_quality_percent < 100) &&
        resolution.max_width > 0 &&
        resolution.max_height > 0 ?
        bongo_cat_image_texture_model_scaled_cached(path.c_str(), reusable ? digest : nullptr, direct,
            resolution.max_width, resolution.max_height, &loaded_width,
            &loaded_height, &texture->alpha, progress, userdata, error) :
        bongo_cat_image_texture_model(path.c_str(), direct, &loaded_width,
            &loaded_height, &texture->alpha, progress, userdata, error);
    if (!texture->id) {
        bongo_cat_model_memory_log("texture-failed", "error=%s",
            error ? error->message : "unknown");
        return {};
    }
    texture->width = loaded_width;
    texture->height = loaded_height;
    texture->source_width = source_width > 0 ? source_width : loaded_width;
    texture->source_height = source_height > 0 ? source_height : loaded_height;
    bool mipmaps = full_sampling(texture->id);
    bongo_cat_model_memory_log("texture-ready",
        "cache=miss actual=%dx%d mipmaps=%d rgba8_est_mib=%.1f cpu_pixels=freed",
        loaded_width, loaded_height, mipmaps ? 1 : 0,
        bongo_cat_model_texture_mib(loaded_width, loaded_height, mipmaps));
    bool unchanged = !reusable || same_source(path, before);
    if (!unchanged)
        bongo_cat_image_forget_texture_cache(digest,
            resolution.max_width, resolution.max_height);
    if (reusable && unchanged && mipmaps) {
        std::memcpy(texture->digest, digest, sizeof(digest));
        live_textures.emplace_back(texture);
    }
    return texture;
}

struct TextureProgressContext {
    BongoCatLive2DLoadProgress callback;
    void *userdata;
    float start;
    float span;
};

static void texture_progress(void *userdata, float progress) {
    auto *context = static_cast<TextureProgressContext *>(userdata);
    if (context && context->callback)
        context->callback(context->userdata,
            context->start + context->span * progress);
}

void NativeModel::bind_textures() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i])
            renderer->BindTexture((Csm::csmInt32)i, textures_[i]->id);
    renderer->IsPremultipliedAlpha(true);
}

void NativeModel::release_textures() {
    cancel_texture_refresh();
    textures_.clear();
    triangle_alpha_.clear();
    frame_drawables_.clear();
}

const BongoCatImageAlphaMask *NativeModel::texture_alpha(int index) const {
    return index >= 0 && (size_t)index < textures_.size() && textures_[(size_t)index]
        ? &textures_[(size_t)index]->alpha : nullptr;
}

bool NativeModel::load_textures(BongoCatError *error,
    BongoCatLive2DLoadProgress progress, void *userdata,
    int display_width, int display_height, float render_quality_percent) {
    if (!texture_quality_valid(render_quality_percent)) render_quality_percent = 100.0f;
    render_quality_percent_ = render_quality_percent;
    release_textures();
    int count = setting_->GetTextureCount();
    textures_.assign((size_t)count, nullptr);
    int canvas_width = 0, canvas_height = 0;
    canvas_size(&canvas_width, &canvas_height);
    int reference_width = render_options_.mver_projection &&
        render_options_.reference_width > 0 ? render_options_.reference_width :
        canvas_width;
    int reference_height = render_options_.mver_projection &&
        render_options_.reference_height > 0 ? render_options_.reference_height :
        canvas_height;
    if (reference_width <= 0) reference_width = width_;
    if (reference_height <= 0) reference_height = height_;
    if (display_width <= 0 || display_height <= 0) {
        display_width = viewport_width_;
        display_height = viewport_height_;
    }
    GLint texture_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    texture_limit_ = texture_limit;
    TextureProgressContext texture_context = {progress, userdata, .50f,
        .45f / (float)(count > 0 ? count : 1)};
    for (int i = 0; i < count; ++i) {
        texture_context.start = .50f + .45f * (float)i /
            (float)(count > 0 ? count : 1);
        textures_[(size_t)i] = acquire_texture(
            path(setting_->GetTextureFileName(i)), direct_textures_,
            dynamic_texture_resolution_, render_quality_percent_, display_width,
            display_height, reference_width,
            reference_height, (int)texture_limit,
            progress ? texture_progress : nullptr, &texture_context, error);
        if (!textures_[(size_t)i]) {
            release_textures();
            return false;
        }
        if (progress) progress(userdata, .50f + .45f * (float)(i + 1) /
            (float)(count > 0 ? count : 1));
    }
    if (!frame_prepared_) {
        prepare_expression_frame();
        frame_prepared_ = true;
    }
    prepare_frame_bounds();
    release_renderer();
    if (!create_renderer(error)) {
        release_textures();
        return false;
    }
    renderer_width_ = width_;
    renderer_height_ = height_;
    return true;
}

} // namespace bongo_cat
