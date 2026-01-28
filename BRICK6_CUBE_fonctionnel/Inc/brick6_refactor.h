#ifndef BRICK6_REFACTOR_H
#define BRICK6_REFACTOR_H

#include <stdint.h>

#ifndef BRICK6_REFACTOR_STEP_1
#define BRICK6_REFACTOR_STEP_1 1U
#endif

#ifndef BRICK6_REFACTOR_STEP_2
#define BRICK6_REFACTOR_STEP_2 1U
#endif

#ifndef BRICK6_REFACTOR_STEP_3
#define BRICK6_REFACTOR_STEP_3 1U
#endif

#ifndef BRICK6_REFACTOR_STEP_4
#define BRICK6_REFACTOR_STEP_4 1U
#endif

#ifndef BRICK6_REFACTOR_STEP_5
#define BRICK6_REFACTOR_STEP_5 1U
#endif

#ifndef BRICK6_REFACTOR_STEP_6
#define BRICK6_REFACTOR_STEP_6 1U
#endif

#if BRICK6_REFACTOR_STEP_6
#define USB_BUDGET_PACKETS   4U
#define MIDI_BUDGET_MSGS     8U
#define SD_BUDGET_STEPS      2U
#endif

#if BRICK6_REFACTOR_STEP_1
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
#endif

#if BRICK6_REFACTOR_STEP_6
extern volatile uint32_t usb_budget_hit_count;
extern volatile uint32_t midi_budget_hit_count;
extern volatile uint32_t sd_budget_hit_count;
#endif

#endif /* BRICK6_REFACTOR_H */
