#pragma once

#include <stdint.h>
#include "buttons_ids.h"

#define BOARD_CONTROLS_BUTTON_INVALID BTN_COUNT

#define BOARD_CONTROLS_ENCODER_TRANSITIONS_PER_INCREMENT 1U
#define BOARD_CONTROLS_ENCODER_DIRECTION (-1)

void board_controls_buttons_latch_low(void);
void board_controls_buttons_latch_high(void);
void board_controls_buttons_clock_low(void);
void board_controls_buttons_clock_high(void);
uint8_t board_controls_buttons_data_read(void);
void board_controls_io_barrier(void);
uint8_t board_controls_button_physical_count(void);
button_id_t board_controls_button_logical_for_physical(uint8_t physical_idx);

uint8_t board_controls_encoder_state(uint8_t encoder);
void board_controls_start_encoder_fast_poll_timer(void);

void board_controls_mux_pot_select(uint8_t channel);
uint32_t board_controls_millis(void);
uint8_t board_controls_pot_adc_start(void);
uint8_t board_controls_pot_adc_poll(void);
uint16_t board_controls_pot_adc_read_raw(void);
void board_controls_pot_adc_stop(void);
