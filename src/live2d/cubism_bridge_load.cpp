#include "bongo_cat/model.h"
#include "bongo_cat/model_memory.h"
#include "bongo_cat/resource_trace.h"
#if defined(CSM_TARGET_WIN_GL) || defined(CSM_TARGET_LINUX_GL)
#include <GL/glew.h>
#endif
#include "cubism_runtime.hpp"
#include "cubism_render_resources.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>
#include <exception>
#include <new>

extern "C" BongoCatResult bongo_cat_live2d_load_ex(BongoCatLive2D *runtime,
    const char *directory, const char *setting, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    const BongoCatLive2DTextureOptions *texture_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error) {
    if (!runtime) return BONGO_CAT_ERROR_ARGUMENT;
    bongo_cat::NativeModel *previous = runtime->model;
    const size_t previous_texture_count = previous ? previous->texture_count() : 0;
    const double previous_atlas_mib = previous ? previous->texture_storage_mib() : 0.0;
    bongo_cat::NativeModel *model = nullptr;
    bool previous_resources_released = false;
    auto restore_previous = [&]() {
        if (!previous_resources_released || !previous) return;
        BongoCatError restore_error = {};
        bool restored = false;
        try {
            restored = previous->load_textures(&restore_error, nullptr, nullptr);
        } catch (...) {
            restored = false;
        }
        previous_resources_released = false;
        bongo_cat_resource_trace_atlas(previous->texture_storage_mib());
        bongo_cat_model_memory_log("rollback", "restored=%d textures=%zu",
            restored ? 1 : 0, previous->texture_count());
        if (restored) {
            SDL_Log("[runtime] Live2D resource handoff: "
                "stage=previous-restored textures=%zu",
                previous->texture_count());
            return;
        }
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "[runtime] Previous Live2D model could not be restored after "
            "replacement failure: %s",
            restore_error.message[0] ? restore_error.message : "unknown error");
        runtime->model = nullptr;
        delete previous;
        previous = nullptr;
        bongo_cat_resource_trace_atlas(0.0);
    };
    try {
        model = new(std::nothrow) bongo_cat::NativeModel();
        if (!model) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
                "Cannot allocate Live2D model");
            return BONGO_CAT_ERROR_MEMORY;
        }
        if (render_options) model->set_render_options(*render_options);
        bool dynamic_texture_resolution = texture_options &&
            texture_options->dynamic_resolution;
        if (previous) {
            /* Finish the last old frame and release every render resource
               before decoding the replacement. Keeping the old atlas alive
               until the new atlas is ready defeats the memory-saving goal and
               can create an 800 MB or larger switch-time peak. The model
               object remains available for rollback if loading fails. */
            if (!SDL_GL_GetCurrentContext()) {
                bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                    "Cannot replace a Live2D model without an OpenGL context");
                delete model;
                return error ? error->code : BONGO_CAT_ERROR_PLATFORM;
            }
            glFinish();
            previous_resources_released = true;
            previous->release_render_resources();
            bongo_cat_resource_trace_atlas(0.0);
            unsigned released_targets = bongo_cat::release_offscreen_pool();
            glFinish();
            bongo_cat_model_memory_log("previous-gpu-released",
                "texture_refs=%zu remaining_refs=%zu offscreen_targets=%u "
                "atlas_before_est_mib=%.1f live_atlas_est_mib=%.1f",
                previous_texture_count, previous->texture_count(), released_targets,
                previous_atlas_mib, previous->texture_storage_mib());
            SDL_Log("[runtime] Live2D resource handoff: "
                "stage=previous-released-before-load dynamic=%d",
                dynamic_texture_resolution ? 1 : 0);
        }
        if (!model->load(directory, setting, preset,
                dynamic_texture_resolution, progress, userdata, error)) {
            delete model;
            restore_previous();
            return error ? error->code : BONGO_CAT_ERROR_CUBISM;
        }
        model->reshape(runtime->width, runtime->height);
        bongo_cat_model_memory_log("model-core-ready", "window=%dx%d",
            runtime->width, runtime->height);
        int display_width = 0, display_height = 0;
        if (dynamic_texture_resolution && texture_options->display_size) {
            int canvas_width = 0, canvas_height = 0;
            model->canvas_size(&canvas_width, &canvas_height);
            if (!texture_options->display_size(texture_options->display_size_userdata,
                    render_options, canvas_width, canvas_height,
                    &display_width, &display_height) ||
                display_width <= 0 || display_height <= 0)
                display_width = display_height = 0;
            bongo_cat_model_memory_log("texture-display-planned",
                "previous_window=%dx%d content_pixels=%dx%d canvas=%dx%d",
                runtime->width, runtime->height, display_width, display_height,
                canvas_width, canvas_height);
        }
        if (!model->load_textures(error, progress, userdata,
                display_width, display_height)) {
            BongoCatResult result = error ? error->code : BONGO_CAT_ERROR_CUBISM;
            delete model;
            restore_previous();
            return result;
        }
        GLenum ready_error = glGetError();
        bongo_cat_resource_trace_atlas(model->texture_storage_mib());
        bongo_cat_model_memory_log("replacement-ready",
            "textures=%zu live_atlas_est_mib=%.1f previous_atlas_est_mib=%.1f",
            model->texture_count(), model->texture_storage_mib(), previous_atlas_mib);
        SDL_Log("[runtime] Live2D resource handoff: stage=replacement-ready "
            "new_textures=%zu previous_textures=%zu current_window=%p "
            "current_context=%p gl_error=0x%x",
            model->texture_count(), previous_texture_count,
            (void *)SDL_GL_GetCurrentWindow(),
            (void *)SDL_GL_GetCurrentContext(), (unsigned)ready_error);
        if (progress) progress(userdata, 1.0f);
        runtime->model = model;
        const size_t released_textures = previous_texture_count;
        // Loading and drawing use the same GL context. The old render
        // resources were released before loading; retire the model object
        // here even when a hidden/minimized window never draws again.
        if (previous) {
            delete previous;
            previous_resources_released = false;
            // Submit any queued work so driver-side releases can finish even
            // when this switch is followed by no draws or buffer swaps.
            glFlush();
        }
        SDL_Log("[runtime] Live2D resource handoff: stage=previous-released "
            "texture_refs=%zu", released_textures);
        GLenum retired_error = glGetError();
        bongo_cat_model_memory_log("previous-model-deleted", "textures=%zu",
            model->texture_count());
        SDL_Log("[runtime] Live2D resource handoff: stage=complete "
            "new_textures=%zu current_window=%p current_context=%p gl_error=0x%x",
            model->texture_count(),
            (void *)SDL_GL_GetCurrentWindow(),
            (void *)SDL_GL_GetCurrentContext(), (unsigned)retired_error);
        return BONGO_CAT_OK;
    } catch (const std::bad_alloc &) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Out of memory while loading the Live2D model");
    } catch (const std::exception &exception) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
            "Live2D model load failed: %s", exception.what());
    } catch (...) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
            "Live2D model load failed with an unknown exception");
    }
    delete model;
    restore_previous();
    return error ? error->code : BONGO_CAT_ERROR_CUBISM;
}

extern "C" BongoCatResult bongo_cat_live2d_load(BongoCatLive2D *runtime,
    const char *directory, const char *setting, bool preset,
    const BongoCatLive2DRenderOptions *render_options,
    BongoCatLive2DLoadProgress progress, void *userdata,
    BongoCatError *error) {
    return bongo_cat_live2d_load_ex(runtime, directory, setting, preset,
        render_options, nullptr, progress, userdata, error);
}
