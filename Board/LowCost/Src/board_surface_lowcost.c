#include "Board/board_surface.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

static board_surface_snapshot_t g_surface_snapshot;

static uint32_t read_shift_register_bits(void)
{
    uint32_t raw = 0U;

    CS_SR_GPIO_Port->BSRR = ((uint32_t)CS_SR_Pin << 16U);
    __NOP();
    CS_SR_GPIO_Port->BSRR = CS_SR_Pin;

    for (uint32_t i = 0U; i < 24U; i++)
    {
        SCK_SR_GPIO_Port->BSRR = ((uint32_t)SCK_SR_Pin << 16U);
        __NOP();
        raw <<= 1U;
        raw |= (SR_DATA_GPIO_Port->IDR & SR_DATA_Pin) ? 1U : 0U;
        SCK_SR_GPIO_Port->BSRR = SCK_SR_Pin;
        __NOP();
    }

    return (~raw) & 0x00FFFFFFUL;
}

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

    const uint32_t pressed = read_shift_register_bits();
    for (uint8_t lane = 0U; lane < BOARD_SURFACE_LANE_COUNT; lane++)
    {
        const uint8_t down = (uint8_t)((pressed >> lane) & 0x01U);
        snapshot->raw[lane] = down ? UINT16_MAX : 0U;
        snapshot->sample_count[lane] = g_surface_snapshot.sample_count[lane] + 1U;
        snapshot->analog[lane] = 0U;
    }
}
