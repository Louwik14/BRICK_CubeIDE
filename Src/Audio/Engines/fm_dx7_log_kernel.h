#pragma once

#include <stdint.h>

/*
 * Experimental, algorithm-1-only DX7 renderer used by FM_KERNEL_BENCH.
 * The persistent state is deliberately self-contained and has no MSFA bus.
 */
struct dx7_log_kernel_operator_t
{
    uint32_t phase;
    uint32_t phase_increment;
    int32_t phase_increment_delta;
    int32_t attenuation_q16;
    int32_t attenuation_delta_q16;
};

struct dx7_log_kernel_voice_t
{
    dx7_log_kernel_operator_t operators[6];
    uint32_t phase_increment_target[6];
    int32_t attenuation_target_q16[6];
    int32_t feedback[2];
};

static_assert(sizeof(dx7_log_kernel_voice_t) == 176U,
              "DX7 log kernel voice state budget changed");

void dx7_log_kernel_init(void);
void dx7_log_kernel_reset(dx7_log_kernel_voice_t *voice);
void dx7_log_kernel_note_on(dx7_log_kernel_voice_t *voice, bool sync);
void dx7_log_kernel_initialize_held(dx7_log_kernel_voice_t *voice);
void dx7_log_kernel_set_phase_increment(dx7_log_kernel_voice_t *voice,
                                        uint32_t operator_index,
                                        uint32_t phase_increment);
void dx7_log_kernel_prepare_operator(dx7_log_kernel_voice_t *voice,
                                     uint32_t operator_index,
                                     int32_t level_q24,
                                     uint32_t phase_increment,
                                     uint32_t frames);
void __attribute__((noinline)) dx7_log_kernel_render_algorithm_1(
    dx7_log_kernel_voice_t *voice,
    uint32_t feedback_shift,
    float *output,
    uint32_t frames);
