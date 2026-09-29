#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_capture.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_keymap.h"
#include "Board/board_surface.h"
#include "Platform/brick_media_clock.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "adc.h"

#define HALL_MUX_COUNT         8U
#define HALL_MUX_SETTLE_DISCARD_PAIRS 12U

_Static_assert(sizeof(hall_capture_record_t) == 56U,
               "Hall capture dump layout changed");
_Static_assert((HALL_CAPTURE_CAPACITY & (HALL_CAPTURE_CAPACITY - 1U)) == 0U,
               "Hall capture ring capacity must be a power of two");

/*
 * ADC DMA mailboxes:
 * ADC1 writes Hall MUX 0/2 plus the master-volume pot,
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
CONTROL_STATE_SDRAM volatile hall_capture_record_t
    g_hall_capture[HALL_CAPTURE_CAPACITY]
    __attribute__((used, externally_visible));
CONTROL_STATE_SDRAM volatile hall_capture_calibration_t
    g_hall_capture_calibration[HALL_KEY_COUNT]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_capture_sequence __attribute__((used, externally_visible));
volatile uint32_t g_hall_capture_calibration_generation
    __attribute__((used, externally_visible));
volatile uint8_t g_hall_capture_frozen __attribute__((used, externally_visible));
volatile uint8_t g_hall_capture_release_keys[3]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_capture_release_ticks[3]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_capture_held_mask
    __attribute__((used, externally_visible));
static uint32_t g_hall_adc1_callbacks;
static uint32_t g_hall_adc2_callbacks;
static uint32_t g_hall_adc1_callback_tick;
static uint32_t g_hall_adc2_callback_tick;
static uint32_t g_hall_capture_cluster_start;
static uint8_t g_hall_capture_cluster_count;

void hall_capture_note_release(uint8_t key, uint32_t held_ms)
{
    if ((held_ms < 500U) || (g_hall_capture_frozen != 0U)) return;
    const uint32_t now = HAL_GetTick();
    if ((g_hall_capture_cluster_count == 0U)
            || ((uint32_t)(now - g_hall_capture_cluster_start) > 100U))
    {
        g_hall_capture_cluster_start = now;
        g_hall_capture_cluster_count = 0U;
    }
    for (uint8_t i = 0U; i < g_hall_capture_cluster_count; ++i)
        if (g_hall_capture_release_keys[i] == key) return;
    const uint8_t index = g_hall_capture_cluster_count++;
    g_hall_capture_release_keys[index] = key;
    g_hall_capture_release_ticks[index] = now;
    if (g_hall_capture_cluster_count == 3U)
        g_hall_capture_frozen = 1U;
}

static uint8_t hall_adc_gpio_mux(uint8_t input)
{
    const uint32_t s0 = input ? MUX_HALL_S0_GPIO_Port->IDR
                              : MUX_HALL_S0_GPIO_Port->ODR;
    const uint32_t s1 = input ? MUX_HALL_S1_GPIO_Port->IDR
                              : MUX_HALL_S1_GPIO_Port->ODR;
    const uint32_t s2 = input ? MUX_HALL_S2_GPIO_Port->IDR
                              : MUX_HALL_S2_GPIO_Port->ODR;
    return (uint8_t)(((s0 & MUX_HALL_S0_Pin) ? 1U : 0U)
        | ((s1 & MUX_HALL_S1_Pin) ? 2U : 0U)
        | ((s2 & MUX_HALL_S2_Pin) ? 4U : 0U));
}
static void hall_mux_select(uint8_t index)
{
    board_surface_select_hall_mux(index);
}

static void hall_adc_queue_sample(uint8_t key, uint16_t raw)
{
    const uint32_t sample_count = hall_sample_count[key] + 1U;
    const uint32_t tim5_tick = brick_media_clock_now_tick();

    hall_raw[key] = raw;
    hall_sample_count[key] = sample_count;
    board_surface_update_lane(key, raw, sample_count);

    /* Both boards run the same bounded detector in the acquisition callback. */
    hall_engine_process_sample(key, raw, sample_count, tim5_tick);
}

static void hall_adc_process_pair(uint8_t completing_adc)
{
    const uint16_t adc1_dma_ndtr_before =
        (uint16_t)((DMA_Stream_TypeDef *)hadc1.DMA_Handle->Instance)->NDTR;
    const uint16_t v1 = adc1_dma[0U];
    const uint16_t v2 = adc2_dma;
    const uint16_t v3 = adc1_dma[1U];
    const uint16_t volume = adc1_dma[2U];
    const uint16_t adc1_dma_ndtr_after =
        (uint16_t)((DMA_Stream_TypeDef *)hadc1.DMA_Handle->Instance)->NDTR;

    if (hall_discard_count != 0U)
    {
        hall_discard_count--;
        return;
    }

    volatile hall_capture_record_t *record = NULL;
    uint32_t trace_sequence = 0U;
    if (g_hall_capture_frozen == 0U)
    {
        trace_sequence = ++g_hall_capture_sequence;
        record = &g_hall_capture[(trace_sequence - 1U)
                                 & (HALL_CAPTURE_CAPACITY - 1U)];
        record->sequence = 0U;
        record->tick_ms = HAL_GetTick();
        record->tim5_tick = brick_media_clock_now_tick();
        record->held_before = g_hall_capture_held_mask;
        record->calibration_generation =
            g_hall_capture_calibration_generation;
        record->adc1_hall_a = v1;
        record->adc2_hall_b = v2;
        record->adc1_hall_c = v3;
        record->adc1_volume = volume;
        record->adc1_callbacks = (uint16_t)g_hall_adc1_callbacks;
        record->adc2_callbacks = (uint16_t)g_hall_adc2_callbacks;
        record->mux_expected = hall_mux_index;
        record->mux_odr = hall_adc_gpio_mux(0U);
        record->mux_idr = hall_adc_gpio_mux(1U);
        record->completing_adc = completing_adc;
        record->adc1_callback_tick = g_hall_adc1_callback_tick;
        record->adc2_callback_tick = g_hall_adc2_callback_tick;
        record->adc1_error = (uint16_t)hadc1.ErrorCode;
        record->adc2_error = (uint16_t)hadc2.ErrorCode;
        record->adc1_dma_ndtr_before = adc1_dma_ndtr_before;
        record->adc1_dma_ndtr_after = adc1_dma_ndtr_after;
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

        if (record != NULL)
        {
            record->held_after = g_hall_capture_held_mask;
            __DMB();
            record->sequence = trace_sequence;
        }

        hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 0x07U);
        hall_mux_select(hall_mux_index);
        adc1_ready = 0U;
        adc2_ready = 0U;

        hall_discard_count = HALL_MUX_SETTLE_DISCARD_PAIRS;
        adc1_ready = 0U;
        adc2_ready = 0U;
    }
}

void hall_adc_init(void)
{
    hall_mux_index = 0U;
    hall_discard_count = HALL_MUX_SETTLE_DISCARD_PAIRS;
    g_hall_capture_sequence = 0U;
    g_hall_capture_calibration_generation = 0U;
    g_hall_capture_frozen = 0U;
    g_hall_capture_held_mask = 0U;
    g_hall_capture_cluster_count = 0U;
    g_hall_adc1_callbacks = 0U;
    g_hall_adc2_callbacks = 0U;
    g_hall_adc1_callback_tick = 0U;
    g_hall_adc2_callback_tick = 0U;

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
        ++g_hall_adc1_callbacks;
        g_hall_adc1_callback_tick = brick_media_clock_now_tick();
        adc1_ready = 1U;
    }
    else if (board_surface_is_hall_adc2_callback(hadc) != 0U)
    {
        ++g_hall_adc2_callbacks;
        g_hall_adc2_callback_tick = brick_media_clock_now_tick();
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
        hall_adc_process_pair((hadc->Instance == ADC1) ? 1U : 2U);
    }
}
