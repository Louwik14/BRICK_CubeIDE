#include "brick6_refactor.h"

#if BRICK6_REFACTOR_STEP_1
volatile uint32_t brick6_audio_tx_half_count = 0U;
volatile uint32_t brick6_audio_tx_full_count = 0U;
volatile uint32_t brick6_audio_rx_half_count = 0U;
volatile uint32_t brick6_audio_rx_full_count = 0U;
volatile uint32_t brick6_sd_rx_cplt_count = 0U;
volatile uint32_t brick6_sd_tx_cplt_count = 0U;
volatile uint32_t brick6_sd_err_count = 0U;
volatile uint32_t brick6_sd_buf0_cplt_count = 0U;
volatile uint32_t brick6_sd_buf1_cplt_count = 0U;
volatile uint32_t brick6_usb_host_poll_count = 0U;
volatile uint32_t brick6_midi_host_poll_count = 0U;
#endif

#if BRICK6_REFACTOR_STEP_6
volatile uint32_t usb_budget_hit_count = 0U;
volatile uint32_t midi_budget_hit_count = 0U;
volatile uint32_t sd_budget_hit_count = 0U;
#endif
