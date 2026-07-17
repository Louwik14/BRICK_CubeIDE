#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BOARD_DISPLAY_TX_OK = 0,
    BOARD_DISPLAY_TX_ERROR,
    BOARD_DISPLAY_TX_TIMEOUT
} board_display_tx_status_t;

void board_display_transport_reset(void);
void board_display_transport_begin(uint8_t is_data);
void board_display_transport_end(void);
board_display_tx_status_t board_display_transport_tx(const uint8_t *data,
                                                      size_t len,
                                                      uint32_t timeout_ms);
board_display_tx_status_t board_display_transport_tx_dma(const uint8_t *data,
                                                          size_t len);
uint8_t board_display_transport_is_tx_callback(void *handle);

