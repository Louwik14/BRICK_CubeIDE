#include "Board/board_surface.h"

#include "adc.h"
#include "main.h"
#include "tim.h"
#include "App/control_rt_wakeup.h"

static volatile uint16_t *g_adc1_mailbox;
static volatile uint8_t g_master_volume_valid;
static volatile uint16_t g_master_volume_last_raw;
static volatile uint32_t g_master_volume_version;

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

    sConfig.Channel = ADC_CHANNEL_18;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
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
    g_master_volume_last_raw = 0U;
    g_master_volume_version = 0U;

    if (hadc1.DMA_Handle == NULL)
    {
        return 0U;
    }
    hadc1.DMA_Handle->Init.MemInc = DMA_MINC_ENABLE;
    if (HAL_DMA_Init(hadc1.DMA_Handle) != HAL_OK)
    {
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

uint8_t board_surface_read_master_volume_raw(uint16_t *raw)
{
    if ((raw == 0) || (g_adc1_mailbox == 0) || (g_master_volume_valid == 0U))
    {
        return 0U;
    }

    *raw = g_adc1_mailbox[2U];
    return 1U;
}

uint32_t board_surface_master_volume_version(void)
{
    return g_master_volume_version;
}

uint8_t board_surface_is_hall_adc1_callback(void *handle)
{
    ADC_HandleTypeDef *hadc = (ADC_HandleTypeDef *)handle;
    const uint8_t is_adc1 = ((hadc != NULL) && (hadc->Instance == ADC1)) ? 1U : 0U;
    if (is_adc1 != 0U)
    {
        const uint16_t raw = (g_adc1_mailbox != 0) ? g_adc1_mailbox[2U] : 0U;
        const uint8_t changed = ((g_master_volume_valid == 0U)
                                 || (raw != g_master_volume_last_raw)) ? 1U : 0U;

        /* Hall continuous sampling is consumed in this DMA callback.  CONTROL
         * only needs a doorbell for the first valid pot value or a new value. */
        g_master_volume_valid = 1U;
        g_master_volume_last_raw = raw;
        if (changed != 0U)
        {
            ++g_master_volume_version;
            control_rt_wakeup(CONTROL_RT_WAKE_LATEST);
        }
    }
    return is_adc1;
}
