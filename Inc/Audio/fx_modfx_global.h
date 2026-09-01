#pragma once

#include <stdint.h>
#include "Param/engine_model_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
