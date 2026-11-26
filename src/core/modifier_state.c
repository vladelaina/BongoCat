#include "bongo_cat/modifier_state.h"

static uint64_t side_mask(BongoCatModifierMasks masks, bool right_side) {
    return right_side ? masks.right : masks.left;
}

bool bongo_cat_modifier_is_down(uint64_t flags,
    BongoCatModifierMasks masks, bool right_side) {
    uint64_t sides = masks.left | masks.right;
    if (flags & sides) return (flags & side_mask(masks, right_side)) != 0;
    return (flags & masks.combined) != 0;
}

bool bongo_cat_modifier_is_released(uint64_t flags,
    BongoCatModifierMasks masks, bool right_side) {
    if (!(flags & masks.combined)) return true;
    uint64_t sides = masks.left | masks.right;
    if (!(flags & sides)) return false;
    return (flags & side_mask(masks, right_side)) == 0;
}
