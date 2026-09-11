#pragma once

#include <stdint.h>

extern volatile uint8_t g_debug_mic_measurement_enabled;

extern volatile uint32_t g_debug_mic_sai_right_abs;
extern volatile int32_t g_debug_mic_sai_right_min;
extern volatile int32_t g_debug_mic_sai_right_max;
extern volatile uint32_t g_debug_mic_sai_right_peak_abs;

extern volatile float g_debug_mic_mono_abs;
extern volatile float g_debug_mic_mono_min;
extern volatile float g_debug_mic_mono_max;
extern volatile float g_debug_mic_mono_peak_abs;

extern volatile uint32_t g_debug_mic_rec_abs;
extern volatile int32_t g_debug_mic_rec_min;
extern volatile int32_t g_debug_mic_rec_max;
extern volatile uint32_t g_debug_mic_rec_peak_abs;

void audio_mic_debug_set_enabled(uint8_t enabled);
