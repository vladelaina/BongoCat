#include "cubism_texture_resolution.hpp"
#include "test.h"

#include <climits>
#include <initializer_list>
#include <limits>

int bongo_cat_test_failures;
using namespace bongo_cat;

static void expect_size(const TextureResolution &bound, int width, int height) {
    CHECK(bound.max_width == width && bound.max_height == height);
}

int main() {
    const float levels[] = {0.1f, 1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    for (float quality : levels) CHECK(texture_quality_valid(quality));
    const float invalid[] = {0, -1, 0.01f, 2, 11, 101,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()};
    for (float quality : invalid) {
        CHECK(!texture_quality_valid(quality));
        expect_size(texture_resolution_for(false, 0, 0, 0, 0,
            4096, 2048, 16384, quality), 4096, 2048);
    }

    // Preserve the existing display budget; quality is an additional cap.
    expect_size(texture_resolution_for(true, 250, 125, 1000, 500,
        4096, 2048, 16384, 100), 1024, 512);
    expect_size(texture_resolution_for(true, 250, 125, 1000, 500,
        4096, 2048, 16384, 10), 1024, 512);
    expect_size(texture_resolution_for(true, 250, 125, 1000, 500,
        4096, 2048, 16384, 1), 410, 205);
    expect_size(texture_resolution_for(false, 0, 0, 0, 0,
        4096, 2048, 16384, 0.1f), 130, 65);
    expect_size(texture_resolution_for(true, 0, 0, 0, 0,
        4096, 2048, 64, 0.1f), 64, 64);
    expect_size(texture_resolution_for(false, 0, 0, 0, 0,
        1, 1, 16384, 0.1f), 1, 1);

    CHECK((texture_fitted_size(4096, 2048, {130, 65, true}) ==
        std::make_pair(130, 65)));
    CHECK((texture_fitted_size(193, 131, {64, 64, true}) ==
        std::make_pair(64, 43)));
    CHECK((texture_fitted_size(193, 131, {64, 48, true}) ==
        std::make_pair(64, 43)));
    CHECK((texture_fitted_size(1, 8192, {1, 260, true}) ==
        std::make_pair(1, 260)));
    CHECK((texture_fitted_size(INT_MAX, INT_MAX, {4096, 4096, true}) ==
        std::make_pair(4096, 4096)));
    CHECK((texture_fitted_size(0, 131, {64, 64, true}) == std::make_pair(0, 0)));

    // Every supported quality must be monotonic and fit the GPU/source bounds,
    // including portrait atlases, one-pixel axes and large input dimensions.
    const int sources[][2] = {{1, 1}, {1, 8192}, {8192, 1}, {193, 131},
        {4096, 2048}, {2048, 4096}, {INT_MAX, INT_MAX}};
    for (const auto &source : sources) {
        for (bool dynamic : {false, true}) {
            std::pair<int, int> previous{0, 0};
            for (float quality : levels) {
                auto bound = texture_resolution_for(dynamic, 333, 167,
                    1000, 500, source[0], source[1], 4096, quality);
                auto size = texture_fitted_size(source[0], source[1], bound);
                CHECK(size.first >= previous.first && size.second >= previous.second);
                CHECK(size.first >= 1 && size.second >= 1);
                CHECK(size.first <= source[0] && size.second <= source[1]);
                // Disabled dynamic resolution at 100 preserves the authored
                // dimensions; the upload path reports unsupported GPU sizes.
                if (dynamic || quality < 100)
                    CHECK(size.first <= 4096 && size.second <= 4096);
                previous = size;
            }
        }
    }
    return bongo_cat_test_failures ? 1 : 0;
}
