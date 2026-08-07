#include <assert.h>
#include <stdint.h>

#include "NoteFx/note_fx_euclid.h"

static uint64_t low_bits(uint8_t length)
{
    if (length >= NOTE_FX_EUCLID_MASK_BITS)
        return UINT64_MAX;
    return (UINT64_C(1) << length) - 1U;
}

int main(void)
{
    /* Canonical convention: bit zero is always the first pulse.  The
     * accumulator distributes P pulses over L cells; no rotation is applied. */
    assert(euclid_build_mask(1U, 0U) == 0U);
    assert(euclid_build_mask(1U, 1U) == UINT64_C(0x1));
    assert(euclid_build_mask(2U, 1U) == UINT64_C(0x1));
    assert(euclid_build_mask(3U, 2U) == UINT64_C(0x5));
    assert(euclid_build_mask(4U, 2U) == UINT64_C(0x5));
    assert(euclid_build_mask(8U, 3U) == UINT64_C(0x49));
    assert(euclid_build_mask(8U, 4U) == UINT64_C(0x55));
    assert(euclid_build_mask(8U, 5U) == UINT64_C(0xB5));
    assert(euclid_build_mask(8U, 8U) == UINT64_C(0xFF));
    assert(euclid_build_mask(16U, 4U) == UINT64_C(0x1111));
    assert(euclid_build_mask(64U, 64U) == UINT64_MAX);

    /* Bounds: L=0 becomes one cell, and P>L is clamped to L. */
    assert(euclid_build_mask(0U, 1U) == UINT64_C(0x1));
    assert(euclid_build_mask(3U, 255U) == UINT64_C(0x5));
    assert(euclid_build_mask(255U, 1U) == UINT64_C(0x1));

    for (uint8_t length = 1U; length <= NOTE_FX_EUCLID_MASK_BITS; ++length)
    {
        for (uint8_t pulse = 0U; pulse <= length; ++pulse)
        {
            const uint64_t mask = euclid_build_mask(length, pulse);
            uint8_t count = 0U;
            for (uint8_t position = 0U; position < length; ++position)
                count = (uint8_t)(count + ((mask >> position) & 1U));
            assert(count == pulse);
            assert((mask & ~low_bits(length)) == 0U);
        }
    }
    return 0;
}
