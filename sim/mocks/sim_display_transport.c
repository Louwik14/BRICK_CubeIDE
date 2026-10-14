#include "Board/board_display_transport.h"

void board_display_transport_reset(void) {}
void board_display_transport_begin(uint8_t is_data) { (void)is_data; }
void board_display_transport_end(void) {}

board_display_tx_status_t board_display_transport_tx(const uint8_t *data,
                                                      size_t len,
                                                      uint32_t timeout_ms)
{
    (void)data;
    (void)len;
    (void)timeout_ms;
    return BOARD_DISPLAY_TX_OK;
}

board_display_tx_status_t board_display_transport_tx_dma(const uint8_t *data,
                                                          size_t len)
{
    (void)data;
    (void)len;
    return BOARD_DISPLAY_TX_OK;
}

uint8_t board_display_transport_is_tx_callback(void *handle)
{
    (void)handle;
    return 0U;
}

