#pragma once

#include <stdint.h>
#include "stm32h7xx.h"

/* All fields are 32-bit words, in this order, for a raw GDB dump. */
typedef struct
{
    volatile uint32_t waveform_capture_max_cycles;
    volatile uint32_t generic_first_max_cycles;
    volatile uint32_t generic_second_max_cycles;
    volatile uint32_t sd_start_max_cycles;
    volatile uint32_t sd_poll_max_cycles;
    volatile uint32_t card_state_max_cycles;
    volatile uint32_t rec_source_max_cycles;
    volatile uint32_t unlink_max_cycles;
    volatile uint32_t audio_recorder_total_max_cycles;
    volatile uint32_t backpressure_at_max_late;
} rec_active_step_diag_t;

extern volatile rec_active_step_diag_t g_rec_active_step_diag;
extern volatile uint32_t g_rec_active_step_diag_reset_requested;
extern volatile uint32_t g_rec_active_step_diag_scope_active;

static inline void rec_active_step_diag_max(volatile uint32_t *maximum,
                                            uint32_t started)
{
    const uint32_t elapsed = DWT->CYCCNT - started;
    if (elapsed > *maximum) *maximum = elapsed;
}
