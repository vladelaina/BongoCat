#include "cubism_model.hpp"
#include "model_frame_policy.h"

#include <algorithm>
#include <cmath>

namespace bongo_cat {
namespace {
/* Prefix sums of the existing conservative alpha mask make triangle UV-box
   queries constant time. Only used while loading; no texture readback. */
struct AlphaCoverage {
    int width = 0, height = 0;
    std::vector<unsigned> sums;

    explicit AlphaCoverage(const BongoCatImageAlphaMask *mask) {
        if (!mask || mask->width <= 0 || mask->height <= 0) return;
        width = mask->width;
        height = mask->height;
        sums.resize((size_t)(width + 1) * (height + 1));
        for (int y = 0; y < height; ++y) {
            unsigned row = 0;
            for (int x = 0; x < width; ++x) {
                row += mask->pixels[(size_t)y * width + x] > 0;
                sums[(size_t)(y + 1) * (width + 1) + x + 1] =
                    sums[(size_t)y * (width + 1) + x + 1] + row;
            }
        }
    }

    bool visible(float min_u, float min_v, float max_u, float max_v) const {
        if (sums.empty() || !std::isfinite(min_u) || !std::isfinite(min_v) ||
            !std::isfinite(max_u) || !std::isfinite(max_v)) return true;
        auto cell = [](float value, int size) {
            return (int)(std::max(0.0f, std::min(1.0f, value)) * (size - 1));
        };
        /* Include neighbouring cells for texture filtering and UV rounding.
           A UV bounding box may include transparent corners; false positives
           cost a little space, whereas false negatives would clip thin parts. */
        int x0 = std::max(0, cell(min_u, width) - 1);
        int y0 = std::max(0, cell(min_v, height) - 1);
        int x1 = std::min(width, cell(max_u, width) + 3);
        int y1 = std::min(height, cell(max_v, height) + 3);
        size_t stride = (size_t)width + 1;
        return sums[(size_t)y1 * stride + x1] + sums[(size_t)y0 * stride + x0] >
            sums[(size_t)y0 * stride + x1] + sums[(size_t)y1 * stride + x0];
    }
};
}

void NativeModel::prepare_frame_bounds() {
    frame_drawables_.clear();
    if (!_model) return;
    std::vector<AlphaCoverage> coverage;
    coverage.reserve(textures_.size());
    for (size_t i = 0; i < textures_.size(); ++i)
        coverage.emplace_back(texture_alpha((int)i));
    frame_drawables_.resize((size_t)_model->GetDrawableCount());
    for (int i = 0; i < _model->GetDrawableCount(); ++i) {
        int count = _model->GetDrawableVertexCount(i);
        const auto *indices = _model->GetDrawableVertexIndices(i);
        const auto *uv = _model->GetDrawableVertexUvs(i);
        if (count <= 0 || !indices || !uv) continue;
        std::vector<unsigned char> used((size_t)count, 0);
        int texture = _model->GetDrawableTextureIndex(i);
        const AlphaCoverage *alpha = texture >= 0 && (size_t)texture < coverage.size()
            ? &coverage[(size_t)texture] : nullptr;
        int index_count = _model->GetDrawableVertexIndexCount(i);
        for (int j = 0; j + 2 < index_count; j += 3) {
            unsigned short a = indices[j], b = indices[j + 1], c = indices[j + 2];
            if (a >= count || b >= count || c >= count) continue;
            if (alpha && !alpha->visible(
                std::min({uv[a].X, uv[b].X, uv[c].X}),
                std::min({uv[a].Y, uv[b].Y, uv[c].Y}),
                std::max({uv[a].X, uv[b].X, uv[c].X}),
                std::max({uv[a].Y, uv[b].Y, uv[c].Y}))) continue;
            used[a] = used[b] = used[c] = 1;
        }
        auto &vertices = frame_drawables_[(size_t)i].vertices;
        for (int j = 0; j < count; ++j)
            if (used[(size_t)j]) vertices.push_back((unsigned short)j);
    }
}

bool NativeModel::measure_frame(BongoCatLive2DFrame *required) {
    if (!_model || !required) return false;
    ModelBounds envelope;
    auto include = [](ModelBounds &bounds, float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        if (!bounds.valid) {
            bounds = {x, y, x, y, true};
            return;
        }
        bounds.min_x = std::min(bounds.min_x, x);
        bounds.min_y = std::min(bounds.min_y, y);
        bounds.max_x = std::max(bounds.max_x, x);
        bounds.max_y = std::max(bounds.max_y, y);
    };
    if (_model->GetModelOpacity() > 0.001f) {
        if (frame_drawables_.size() != (size_t)_model->GetDrawableCount()) {
            for (int i = 0; i < _model->GetDrawableCount(); ++i) {
                if (!_model->GetDrawableDynamicFlagIsVisible(i) ||
                    _model->GetDrawableOpacity(i) <= 0.001f) continue;
                const float *positions = _model->GetDrawableVertices(i);
                if (!positions) continue;
                for (int j = 0; j < _model->GetDrawableVertexCount(i); ++j)
                    include(envelope, positions[j * 2], positions[j * 2 + 1]);
            }
        }
        for (size_t i = 0; i < frame_drawables_.size(); ++i) {
            if (!_model->GetDrawableDynamicFlagIsVisible((int)i) ||
                _model->GetDrawableOpacity((int)i) <= 0.001f) continue;
            auto &drawable = frame_drawables_[i];
            if (drawable.dirty) {
                drawable.bounds = {};
                const float *positions = _model->GetDrawableVertices((int)i);
                if (positions)
                    for (unsigned short vertex : drawable.vertices)
                        include(drawable.bounds, positions[vertex * 2],
                            positions[vertex * 2 + 1]);
                drawable.dirty = false;
            }
            if (drawable.bounds.valid) {
                include(envelope, drawable.bounds.min_x, drawable.bounds.min_y);
                include(envelope, drawable.bounds.max_x, drawable.bounds.max_y);
            }
        }
    }
    /* Use the unpadded content aspect. The fitted viewport must never feed
       back into boundary measurement or an extreme motion could grow forever. */
    int content_width = (int)std::max(1.0, std::round(width_ /
        (1.0 + frame_.left + frame_.right)));
    int content_height = (int)std::max(1.0, std::round(height_ /
        (1.0 + frame_.top + frame_.bottom)));
    if (envelope.valid) {
        Csm::CubismMatrix44 projection;
        build_projection(projection, content_width, content_height);
        float x0 = projection.TransformX(envelope.min_x);
        float x1 = projection.TransformX(envelope.max_x);
        float y0 = projection.TransformY(envelope.min_y);
        float y1 = projection.TransformY(envelope.max_y);
        /* Trigger slightly before contact, including raster/filter coverage. */
        float guard_x = 4.0f / content_width, guard_y = 4.0f / content_height;
        required_frame_ = bongo_cat_frame_observe(required_frame_,
            std::min(x0, x1) - guard_x, std::min(y0, y1) - guard_y,
            std::max(x0, x1) + guard_x, std::max(y0, y1) + guard_y);
    }
    *required = required_frame_;
    update_viewport();
    return true;
}

} // namespace bongo_cat
