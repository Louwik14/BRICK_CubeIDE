#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FX_MODFX_OFF = 0,
    FX_MODFX_CHORUS_Q31,
    FX_MODFX_CHORUS_F32,
    FX_MODFX_STEREO_CHORUS_Q31,
    FX_MODFX_STEREO_CHORUS_F32,
    FX_MODFX_DAISY_CHORUS,
    FX_MODFX_DELUGE_DIMENSION,
    FX_MODFX_TEENSY_CHORUS,
    FX_MODFX_JUNOLOGUE,
    FX_MODFX_MODEL_COUNT
} fx_modfx_model_t;

void fx_modfx_global_init(void);
void fx_modfx_global_set_model(uint8_t model);
void fx_modfx_global_set_rate(float rate_hz);
void fx_modfx_global_set_depth(float depth);
void fx_modfx_global_set_feedback(float feedback);
void fx_modfx_global_set_offset(float offset);
uint8_t fx_modfx_global_is_active(void);
void fx_modfx_global_process_block(float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif
