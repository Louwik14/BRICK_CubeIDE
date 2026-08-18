#include "warps/resources.h"
extern "C" const float *fx_audio_fold_warps_lut(void)
{
    return warps::lut_bipolar_fold;
}
