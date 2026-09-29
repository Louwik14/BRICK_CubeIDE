#pragma once

#include <stdint.h>

#define BOARD_SURFACE_LANE_COUNT 16U

typedef struct
{
    uint16_t raw[BOARD_SURFACE_LANE_COUNT];
    uint32_t sample_count[BOARD_SURFACE_LANE_COUNT];
    uint8_t analog[BOARD_SURFACE_LANE_COUNT];
} board_surface_snapshot_t;

void board_surface_select_hall_mux(uint8_t index);
uint8_t board_surface_start_hall_adc_dma(volatile uint16_t *adc1_mailbox,
                                         volatile uint16_t *adc2_mailbox);
uint8_t board_surface_start_hall_scan_timer(void);
uint8_t board_surface_is_hall_adc1_callback(void *handle);
uint8_t board_surface_is_hall_adc2_callback(void *handle);
void board_surface_update_lane(uint8_t lane, uint16_t raw, uint32_t sample_count);
void board_surface_snapshot(board_surface_snapshot_t *snapshot);
uint8_t board_surface_read_master_volume_raw(uint16_t *raw);
