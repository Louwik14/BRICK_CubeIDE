#pragma once

#include <stdint.h>

void board_surface_select_hall_mux(uint8_t index);
uint8_t board_surface_start_hall_adc_dma(volatile uint16_t *adc1_mailbox,
                                         volatile uint16_t *adc2_mailbox);
uint8_t board_surface_start_hall_scan_timer(void);
uint8_t board_surface_is_hall_adc1_callback(void *handle);
uint8_t board_surface_is_hall_adc2_callback(void *handle);
uint8_t board_surface_read_master_volume_raw(uint16_t *raw);
