#include "Board/board_surface.h"
#include "Board/board_controls.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

static board_surface_snapshot_t g_surface_snapshot;
static volatile uint16_t *g_adc1_mailbox;
static volatile uint8_t g_master_volume_valid;
static volatile uint8_t g_master_volume_nonzero_seen;

static uint32_t read_shift_register_bits(void)
{
    uint32_t raw = 0U;

    CS_SR_GPIO_Port->BSRR = ((uint32_t)CS_SR_Pin << 16U);
    __NOP();
    CS_SR_GPIO_Port->BSRR = CS_SR_Pin;

    for (uint32_t i = 0U; i < 32U; i++)
    {
        SCK_SR_GPIO_Port->BSRR = ((uint32_t)SCK_SR_Pin << 16U);
        __NOP();
        raw <<= 1U;
        raw |= (SR_DATA_GPIO_Port->IDR & SR_DATA_Pin) ? 1U : 0U;
        SCK_SR_GPIO_Port->BSRR = SCK_SR_Pin;
        __NOP();
    }

    return ~raw;
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
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc1.Init.NbrOfConversion = 3U;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        return 0U;
    }

    sConfig.Channel = ADC_CHANNEL_11;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return 0U;
    }

    sConfig.Channel = ADC_CHANNEL_19;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return 0U;
    }

    sConfig.Channel = ADC_CHANNEL_5;
    sConfig.Rank = ADC_REGULAR_RANK_3;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return 0U;
    }

    g_adc1_mailbox = adc1_mailbox;
    g_master_volume_valid = 0U;
    g_master_volume_nonzero_seen = 0U;

    if (hadc1.DMA_Handle == NULL)
    {
        g_adc1_mailbox = 0;
        return 0U;
    }
    hadc1.DMA_Handle->Init.MemInc = DMA_MINC_ENABLE;
    if (HAL_DMA_Init(hadc1.DMA_Handle) != HAL_OK)
    {
        g_adc1_mailbox = 0;
        return 0U;
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_mailbox, 3U) != HAL_OK)
    {
        g_adc1_mailbox = 0;
        g_master_volume_valid = 0U;
        return 0U;
    }

    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_mailbox, 1U) != HAL_OK)
    {
        g_adc1_mailbox = 0;
        g_master_volume_valid = 0U;
        g_master_volume_nonzero_seen = 0U;
        return 0U;
    }

    return 1U;
}

uint8_t board_surface_start_hall_scan_timer(void)
{
    return (HAL_TIM_Base_Start(&htim6) == HAL_OK) ? 1U : 0U;
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
    uint16_t lane_mask = 0U;

    for (uint8_t physical_idx = 0U; physical_idx < 32U; ++physical_idx)
    {
        const button_id_t button = board_controls_button_logical_for_physical(physical_idx);
        if ((button >= BTN_STEP_1) && (button <= BTN_STEP_16)
            && (((pressed >> physical_idx) & 0x01U) != 0U))
        {
            lane_mask |= (uint16_t)(1U << ((uint8_t)button - (uint8_t)BTN_STEP_1));
        }
    }

    for (uint8_t lane = 0U; lane < BOARD_SURFACE_LANE_COUNT; lane++)
    {
        const uint8_t down = (uint8_t)((lane_mask >> lane) & 0x01U);
        snapshot->raw[lane] = down ? UINT16_MAX : 0U;
        snapshot->sample_count[lane] = g_surface_snapshot.sample_count[lane] + 1U;
        snapshot->analog[lane] = 0U;
    }
}

uint8_t board_surface_read_master_volume_raw(uint16_t *raw)
{
    if ((raw == 0) || (g_adc1_mailbox == 0) || (g_master_volume_valid == 0U))
    {
        return 0U;
    }

    const uint16_t sample = g_adc1_mailbox[2U];
    if (sample != 0U)
    {
        g_master_volume_nonzero_seen = 1U;
    }
    else if (g_master_volume_nonzero_seen == 0U)
    {
        return 0U;
    }

    *raw = sample;
    return 1U;
}

uint8_t board_surface_is_hall_adc1_callback(void *handle)
{
    ADC_HandleTypeDef *hadc = (ADC_HandleTypeDef *)handle;
    const uint8_t is_adc1 = ((hadc != NULL) && (hadc->Instance == ADC1)) ? 1U : 0U;
    if (is_adc1 != 0U)
    {
        g_master_volume_valid = 1U;
    }
    return is_adc1;
}
