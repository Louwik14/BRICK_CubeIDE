#include "App/Hall/hall_adc.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_keymap.h"
#include "Board/board_surface.h"
#include "IPC/live_clock_control.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

#define HALL_MUX_COUNT         8U

/*
 * ADC DMA mailboxes:
 * - ADC1 writes Hall MUX 0/2 plus the master-volume pot,
 *   ADC2 writes Hall MUX 1
 *
 * Placement in DMA_BUFFER prepares a non-cacheable policy at MPU stage.
 */
static DMA_BUFFER volatile uint16_t adc1_dma[3U];
static DMA_BUFFER volatile uint16_t adc2_dma;

static volatile uint16_t hall_raw[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count[HALL_KEY_COUNT];

static volatile uint8_t hall_mux_index;
static volatile uint8_t hall_discard_count;
static volatile uint8_t adc1_ready;
static volatile uint8_t adc2_ready;
static volatile uint16_t hall_mux_raw[3U][HALL_MUX_COUNT];
static void hall_mux_select(uint8_t index)
{
    board_surface_select_hall_mux(index);
}

static void hall_adc_queue_sample(uint8_t key, uint16_t raw)
{
    const uint32_t sample_count = hall_sample_count[key] + 1U;
    const uint32_t tim5_tick = live_clock_capture_tick();

    hall_raw[key] = raw;
    hall_sample_count[key] = sample_count;
    /* The bounded detector consumes all 24 BRICK Hall channels in the callback. */
    hall_engine_process_sample(key, raw, sample_count, tim5_tick);
}

static void hall_adc_process_pair(void)
{
    const uint16_t v1 = adc1_dma[0U];
    const uint16_t v2 = adc2_dma;
    const uint16_t v3 = adc1_dma[1U];

    if (hall_discard_count != 0U)
    {
        hall_discard_count--;
        return;
    }

    hall_mux_raw[0U][hall_mux_index] = v1;
    hall_mux_raw[1U][hall_mux_index] = v2;
    hall_mux_raw[2U][hall_mux_index] = v3;

    {
        uint8_t key_a = 0U;
        uint8_t key_b = 0U;
        uint8_t key_c = 0U;

        if (hall_keymap_key_for_mux_channel(0U, hall_mux_index, &key_a) != 0U)
        {
            hall_adc_queue_sample(key_a, v1);
        }
        if (hall_keymap_key_for_mux_channel(1U, hall_mux_index, &key_b) != 0U)
        {
            hall_adc_queue_sample(key_b, v2);
        }
        if (hall_keymap_key_for_mux_channel(2U, hall_mux_index, &key_c) != 0U)
        {
            hall_adc_queue_sample(key_c, v3);
        }

        hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 0x07U);
        hall_mux_select(hall_mux_index);
        adc1_ready = 0U;
        adc2_ready = 0U;

        hall_discard_count = 6U;
        adc1_ready = 0U;
        adc2_ready = 0U;
    }
}

void hall_adc_init(void)
{
    hall_mux_index = 0U;
    hall_discard_count = 6U;

    adc1_dma[0U] = 0U;
    adc1_dma[1U] = 0U;
    adc1_dma[2U] = 0U;
    adc2_dma = 0U;

    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_raw[i] = 0U;
        hall_sample_count[i] = 0U;
    }
    for (uint8_t mux = 0U; mux < HALL_MUX_COUNT; mux++)
    {
        hall_mux_raw[0U][mux] = 0U;
        hall_mux_raw[1U][mux] = 0U;
        hall_mux_raw[2U][mux] = 0U;
    }

    if (board_surface_start_hall_adc_dma(adc1_dma, &adc2_dma) == 0U)
    {
        return;
    }

    if (board_surface_start_hall_scan_timer() == 0U)
    {
        return;
    }
}

uint16_t hall_adc_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_raw[key];
}

uint8_t hall_adc_get_mux_index(void)
{
    return hall_mux_index;
}

uint16_t hall_adc_get_mux_raw(uint8_t mux_adc, uint8_t mux_channel)
{
    if ((mux_adc >= 3U) || (mux_channel >= HALL_MUX_COUNT))
    {
        return 0U;
    }

    return hall_mux_raw[mux_adc][mux_channel];
}

uint32_t hall_adc_get_sample_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_sample_count[key];
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == NULL)
    {
        return;
    }

    if (board_surface_is_hall_adc1_callback(hadc) != 0U)
    {
        adc1_ready = 1U;
    }
    else if (board_surface_is_hall_adc2_callback(hadc) != 0U)
    {
        adc2_ready = 1U;
    }
    else
    {
        return;
    }

    if ((adc1_ready != 0U) && (adc2_ready != 0U))
    {
        adc1_ready = 0U;
        adc2_ready = 0U;
        hall_adc_process_pair();
    }
}
