#pragma once

#include <stdint.h>

void board_controls_buttons_latch_low(void);
void board_controls_buttons_latch_high(void);
void board_controls_buttons_clock_low(void);
void board_controls_buttons_clock_high(void);
uint8_t board_controls_buttons_data_read(void);
void board_controls_io_barrier(void);

uint8_t board_controls_encoder_state(uint8_t encoder);
void board_controls_start_encoder_fast_poll_timer(void);

void board_controls_mux_pot_select(uint8_t channel);
uint32_t board_controls_millis(void);
uint8_t board_controls_pot_adc_start(void);
uint8_t board_controls_pot_adc_poll(void);
uint16_t board_controls_pot_adc_read_raw(void);
void board_controls_pot_adc_stop(void);

