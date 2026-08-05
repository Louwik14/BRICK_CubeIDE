#include "Mod/mod_ramp.h"

#include <stddef.h>

void mod_destination_ramp_prepare(float start,
                                  float end,
                                  uint32_t frames,
                                  uint8_t discontinuous,
                                  mod_destination_ramp_t *out)
{
    if (out == NULL)
    {
        return;
    }

    out->current = start;
    out->end = (discontinuous != 0U) ? start : end;
    out->frames = frames;
    out->discontinuous = discontinuous;
    out->step = ((discontinuous != 0U) || (frames <= 1U))
        ? 0.0f
        : ((out->end - start) / (float)(frames - 1U));
}

float mod_destination_ramp_value_at(const mod_destination_ramp_t *ramp, uint32_t index)
{
    if ((ramp == NULL) || (ramp->frames == 0U))
    {
        return 0.0f;
    }
    if (index >= ramp->frames)
    {
        return ramp->end;
    }
    if (index == (ramp->frames - 1U))
    {
        return ramp->end;
    }
    return ramp->current + (ramp->step * (float)index);
}
