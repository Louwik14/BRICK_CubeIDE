#include "Board/board_display_transport.h"

#include "gpio.h"
#include "spi.h"

static inline void cs_low(void) { HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET); }
static inline void dc_cmd(void) { HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET); }
static inline void dc_data(void) { HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET); }

void board_display_transport_reset(void)
{
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
}

void board_display_transport_begin(uint8_t is_data)
{
    if (is_data != 0U) dc_data();
    else dc_cmd();
    cs_low();
}

void board_display_transport_end(void)
{
    cs_high();
}

board_display_tx_status_t board_display_transport_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    HAL_StatusTypeDef rc = HAL_SPI_Transmit(&hspi5, (uint8_t *)data, (uint16_t)len, timeout_ms);
    if (rc == HAL_OK) return BOARD_DISPLAY_TX_OK;
    return (rc == HAL_TIMEOUT) ? BOARD_DISPLAY_TX_TIMEOUT : BOARD_DISPLAY_TX_ERROR;
}

board_display_tx_status_t board_display_transport_tx_dma(const uint8_t *data, size_t len)
{
    HAL_StatusTypeDef rc = HAL_SPI_Transmit_DMA(&hspi5, (uint8_t *)data, (uint16_t)len);
    if (rc == HAL_OK) return BOARD_DISPLAY_TX_OK;
    return (rc == HAL_TIMEOUT) ? BOARD_DISPLAY_TX_TIMEOUT : BOARD_DISPLAY_TX_ERROR;
}

uint8_t board_display_transport_is_tx_callback(void *handle)
{
    return (handle == (void *)&hspi5) ? 1U : 0U;
}
