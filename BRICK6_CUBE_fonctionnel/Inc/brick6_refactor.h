#ifndef BRICK6_REFACTOR_H
#define BRICK6_REFACTOR_H

#include <stdint.h>

#define BRICK6_ENABLE_DIAGNOSTICS  1
#define BRICK6_ENABLE_BUDGETS      1

#define USB_BUDGET_PACKETS   4U
#define MIDI_BUDGET_MSGS     8U
#define SD_BUDGET_STEPS      2U

#if BRICK6_ENABLE_DIAGNOSTICS
extern volatile uint32_t brick6_audio_tx_half_count;
extern volatile uint32_t brick6_audio_tx_full_count;
extern volatile uint32_t brick6_audio_rx_half_count;
extern volatile uint32_t brick6_audio_rx_full_count;
extern volatile uint32_t brick6_sd_rx_cplt_count;
extern volatile uint32_t brick6_sd_tx_cplt_count;
extern volatile uint32_t brick6_sd_err_count;
extern volatile uint32_t brick6_sd_buf0_cplt_count;
extern volatile uint32_t brick6_sd_buf1_cplt_count;
extern volatile uint32_t brick6_usb_host_poll_count;
extern volatile uint32_t brick6_midi_host_poll_count;

extern volatile uint32_t usb_budget_hit_count;
extern volatile uint32_t midi_budget_hit_count;
extern volatile uint32_t sd_budget_hit_count;

extern volatile uint32_t audio_underflow_count;
#endif

#endif /* BRICK6_REFACTOR_H */
