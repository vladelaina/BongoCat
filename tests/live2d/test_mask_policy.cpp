#include "cubism_mask_policy.hpp"
#include "test.h"
#include <climits>

int bongo_cat_test_failures;
using namespace bongo_cat;

int main() {
    CHECK(mask_buffer_count(0) == 1 && mask_buffer_count(36) == 1);
    CHECK(mask_buffer_count(37) == 2 && mask_buffer_count(64) == 2);
    CHECK(mask_buffer_count(65) == 3);
    CHECK((mask_layout(4, 1) == MaskSize{1, 1}));
    CHECK((mask_layout(5, 1) == MaskSize{2, 1}));
    CHECK((mask_layout(8, 1) == MaskSize{2, 1}));
    CHECK((mask_layout(9, 1) == MaskSize{2, 2}));
    CHECK((mask_layout(16, 1) == MaskSize{2, 2}));
    CHECK((mask_layout(17, 1) == MaskSize{3, 3}));
    CHECK((mask_layout(32, 2) == MaskSize{2, 2}));
    CHECK((mask_target_size({}, 600, 300, 16384) == MaskSize{}));
    for (size_t masks = 1; masks <= 150; ++masks) {
        MaskSize layout = mask_layout(masks, mask_buffer_count(masks));
        for (int width : {300, 600, 1920, 3840}) {
            int height = width / 2;
            MaskSize size = mask_target_size(layout, width, height, 16384);
            CHECK((int64_t)size.width * 10 >= (int64_t)width * 11 * layout.width);
            CHECK((int64_t)size.height * 10 >= (int64_t)height * 11 * layout.height);
            CHECK(size.height < size.width);
        }
    }
    MaskSize allocated = mask_target_size({2, 2}, 600, 300, 16384);
    CHECK((allocated == MaskSize{1408, 768}));
    CHECK(mask_target_size({2, 2}, 590, 290, 16384, allocated) == allocated);
    MaskSize small = mask_target_size({2, 2}, 300, 150, 16384, allocated);
    CHECK(small.width < allocated.width && small.height < allocated.height);
    CHECK((mask_target_size({3, 3}, INT_MAX, INT_MAX, 4096) == MaskSize{4096, 4096}));
    return bongo_cat_test_failures ? 1 : 0;
}
