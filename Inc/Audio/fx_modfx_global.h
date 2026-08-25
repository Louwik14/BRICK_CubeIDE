#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FX_MODFX_OFF = 0,
    FX_MODFX_RETIRED_VIBE,
    FX_MODFX_RETIRED_DRIFT,
    FX_MODFX_DAISY_STEREO,
    FX_MODFX_JUNOLOGUE,
    FX_MODFX_MODEL_COUNT
} fx_modfx_model_t;

void fx_modfx_global_init(void);
void fx_modfx_global_set_model(uint8_t model);
void fx_modfx_global_set_rate(float rate_hz);
void fx_modfx_global_set_rate_b(float rate_hz);
void fx_modfx_global_set_depth(float depth);
void fx_modfx_global_set_depth_b(float depth);
void fx_modfx_global_set_feedback(float feedback);
void fx_modfx_global_set_offset(float offset);
void fx_modfx_global_set_offset_b(float offset);
void fx_modfx_global_set_width(float width);
uint8_t fx_modfx_global_is_active(void);
void fx_modfx_global_process_block(float *left, float *right, uint32_t frames);

#if defined(BRICK6_DAISY_STEREO_TEST)
typedef struct {
    float rate_hz[2];
    float depth[2];
    float delay_samples[2];
    float feedback[2];
} fx_modfx_daisy_stereo_debug_t;
void fx_modfx_global_daisy_stereo_debug(fx_modfx_daisy_stereo_debug_t *out);
#endif

#ifdef __cplusplus
}
#endif
