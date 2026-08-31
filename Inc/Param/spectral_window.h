#pragma once

typedef struct { float min_hz; float max_hz; float min_width; }
    spectral_window_limits_t;
typedef struct { float low_cut_hz; float high_cut_hz; }
    spectral_window_result_t;

spectral_window_limits_t spectral_window_reverb_limits(void);
spectral_window_limits_t spectral_window_delay_limits(void);
void spectral_window_calculate(float position, float width,
                               spectral_window_limits_t limits,
                               spectral_window_result_t *out);
float spectral_window_log_position(float frequency_hz,
                                   spectral_window_limits_t limits);
