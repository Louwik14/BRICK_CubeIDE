#include "NoteFx/note_fx_euclid.h"

#include "NoteFx/note_fx_state.h"

uint64_t euclid_build_mask(uint8_t length, uint8_t pulse)
{
    if (length < NOTE_FX_EUCLID_LENGTH_MIN)
        length = NOTE_FX_EUCLID_LENGTH_MIN;
    if (length > NOTE_FX_EUCLID_MASK_BITS)
        length = NOTE_FX_EUCLID_MASK_BITS;
    if (pulse > length)
        pulse = length;
    if (pulse == 0U)
        return 0U;

    /* Bresenham form of the Euclidean distribution.  Starting the error at
     * length-pulse anchors the first active cell at bit zero without a
     * rotation or a division.  This function runs only when L/P changes. */
    uint8_t error = (uint8_t)(length - pulse);
    uint64_t mask = 0U;
    for (uint8_t position = 0U; position < length; ++position)
    {
        error = (uint8_t)(error + pulse);
        if (error >= length)
        {
            error = (uint8_t)(error - length);
            mask |= (UINT64_C(1) << position);
        }
    }
    return mask;
}
