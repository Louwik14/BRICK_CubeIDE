#pragma once

#include <stdint.h>
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void mixer_meter_submit_track_peak(uint32_t track_id, float peak_abs);
void mixer_meter_submit_master_block(float peak_abs, uint32_t clip_count);
void mixer_meter_advance_window(uint32_t frames);

float mixer_meter_get_track_peak(uint32_t track);
float mixer_meter_get_master_peak(void);
uint32_t mixer_meter_get_master_clip_count(void);
void mixer_meter_reset_window(void);

#ifdef __cplusplus
}
#endif

