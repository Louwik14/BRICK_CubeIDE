#include "plaits/resources.h"

extern "C" const float *fx_audio_fold_lut(void)
{
    return plaits::lut_fold;
}
