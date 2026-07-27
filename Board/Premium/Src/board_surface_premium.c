#include "Board/board_surface.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

static board_surface_snapshot_t g_surface_snapshot;

void board_surface_select_hall_mux(uint8_t index)
{
    const uint8_t mux = index & 0x07U;

    HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                      (mux & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                      (mux & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                      (mux & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t board_surface_start_hall_adc_dma(volatile uint16_t *adc1_mailbox,
                                         volatile uint16_t *adc2_mailbox)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_mailbox, 1U) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_mailbox, 1U) != HAL_OK)
    {
        return 0U;
    }

    return 1U;
}

uint8_t board_surface_start_hall_scan_timer(void)
{
    return (HAL_TIM_Base_Start(&htim6) == HAL_OK) ? 1U : 0U;
}

uint8_t board_surface_is_hall_adc1_callback(void *handle)
{
    ADC_HandleTypeDef *hadc = (ADC_HandleTypeDef *)handle;
    return ((hadc != NULL) && (hadc->Instance == ADC1)) ? 1U : 0U;
}

uint8_t board_surface_is_hall_adc2_callback(void *handle)
{
    ADC_HandleTypeDef *hadc = (ADC_HandleTypeDef *)handle;
    return ((hadc != NULL) && (hadc->Instance == ADC2)) ? 1U : 0U;
}

void board_surface_update_lane(uint8_t lane, uint16_t raw, uint32_t sample_count)
{
    if (lane >= BOARD_SURFACE_LANE_COUNT)
    {
        return;
    }

    g_surface_snapshot.raw[lane] = raw;
    g_surface_snapshot.sample_count[lane] = sample_count;
    g_surface_snapshot.analog[lane] = 1U;
}

void board_surface_snapshot(board_surface_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    *snapshot = g_surface_snapshot;
}

uint8_t board_surface_read_master_volume_raw(uint16_t *raw)
{
    (void)raw;
    return 0U;
}
