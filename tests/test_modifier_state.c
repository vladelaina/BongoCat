#include "test.h"
#include "bongo_cat/modifier_state.h"

static BongoCatModifierMasks option_masks(void) {
    return (BongoCatModifierMasks){
        .combined = 1u << 0,
        .left = 1u << 1,
        .right = 1u << 2,
    };
}

void test_modifier_state(void) {
    BongoCatModifierMasks masks = option_masks();

    CHECK(bongo_cat_modifier_is_down(masks.left, masks, false));
    CHECK(bongo_cat_modifier_is_down(masks.left | masks.combined, masks, false));
    CHECK(!bongo_cat_modifier_is_down(masks.left | masks.combined, masks, true));
    CHECK(bongo_cat_modifier_is_down(masks.left | masks.right | masks.combined,
        masks, false));
    CHECK(bongo_cat_modifier_is_down(masks.left | masks.right | masks.combined,
        masks, true));

    /* A combined flag without side information is a legacy/fallback event. */
    CHECK(bongo_cat_modifier_is_down(masks.combined, masks, false));
    CHECK(bongo_cat_modifier_is_down(masks.combined, masks, true));
    CHECK(!bongo_cat_modifier_is_released(masks.combined, masks, false));
    CHECK(!bongo_cat_modifier_is_released(masks.combined, masks, true));

    /* Releasing either side while the other remains held must release only it. */
    CHECK(bongo_cat_modifier_is_released(masks.left | masks.combined, masks, true));
    CHECK(!bongo_cat_modifier_is_released(masks.left | masks.combined, masks, false));
    CHECK(bongo_cat_modifier_is_released(masks.right | masks.combined, masks, false));
    CHECK(!bongo_cat_modifier_is_released(masks.right | masks.combined, masks, true));

    /* Both sides released: the combined and device flags are clear. */
    CHECK(bongo_cat_modifier_is_released(0, masks, false));
    CHECK(bongo_cat_modifier_is_released(0, masks, true));
}
