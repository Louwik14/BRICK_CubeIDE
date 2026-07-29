#include "Core/brick6_daisy_math.h"

#include <math.h>

float brick6_daisy_sinf(float phase_rad)
{
    return sinf(phase_rad);
}

float brick6_daisy_cosf(float phase_rad)
{
    return cosf(phase_rad);
}

float brick6_daisy_mtof(float midi_note)
{
    return powf(2.0f, (midi_note - 69.0f) / 12.0f) * 440.0f;
}
