#ifndef BONGO_CAT_MODIFIER_STATE_H
#define BONGO_CAT_MODIFIER_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BongoCatModifierMasks {
    uint64_t combined;
    uint64_t left;
    uint64_t right;
} BongoCatModifierMasks;

/*
 * Resolve one physical side of a modifier from an event's flags.
 *
 * Hardware events carry both the combined modifier flag and the device-side
 * flag. Prefer the side flag when any side is present, because the combined
 * flag stays set while the other side is still held. Fall back to the combined
 * flag only for event sources that omit device-side information.
 */
bool bongo_cat_modifier_is_down(uint64_t flags,
    BongoCatModifierMasks masks, bool right_side);

/*
 * Whether a tracked side is physically released.
 *
 * A cleared combined flag releases both sides. When the combined flag remains
 * set but no side information is available, the state is indeterminate and
 * the side is not reported as released.
 */
bool bongo_cat_modifier_is_released(uint64_t flags,
    BongoCatModifierMasks masks, bool right_side);

#endif
