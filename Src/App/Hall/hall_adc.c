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
#include "tim.h"

#define HALL_MUX_COUNT         8U
#define HALL_MUX_SETTLE_DISCARD_PAIRS 6U

_Static_assert(sizeof(hall_capture_record_t) == 72U,
               "Hall capture dump layout changed");
_Static_assert(sizeof(hall_fixed_record_t) == 40U,
               "Hall fixed-mux dump layout changed");
_Static_assert(sizeof(hall_direct_record_t) == 16U,
               "Hall direct ADC dump layout changed");
_Static_assert(sizeof(hall_hold_record_t) == 24U,
               "Hall continuous fixed-mux dump layout changed");
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
CONTROL_STATE_SDRAM volatile hall_fixed_record_t
    g_hall_fixed_trace[HALL_FIXED_CAPACITY]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_fixed_count __attribute__((used, externally_visible));
volatile uint8_t g_hall_fixed_done __attribute__((used, externally_visible));
volatile uint8_t g_hall_fixed_key __attribute__((used, externally_visible));
volatile uint8_t g_hall_fixed_mux __attribute__((used, externally_visible));
volatile uint8_t g_hall_fixed_adc __attribute__((used, externally_visible));
volatile uint16_t g_hall_fixed_baseline __attribute__((used, externally_visible));
volatile uint16_t g_hall_fixed_trigger_raw __attribute__((used, externally_visible));
CONTROL_STATE_SDRAM volatile hall_direct_record_t
    g_hall_direct_trace[HALL_DIRECT_CAPACITY]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_direct_count __attribute__((used, externally_visible));
volatile uint32_t g_hall_direct_error __attribute__((used, externally_visible));
volatile uint8_t g_hall_direct_state __attribute__((used, externally_visible));
volatile uint8_t g_hall_direct_restore_ok __attribute__((used, externally_visible));
CONTROL_STATE_SDRAM volatile hall_hold_record_t
    g_hall_hold_trace[HALL_HOLD_CAPACITY]
    __attribute__((used, externally_visible));
volatile uint32_t g_hall_hold_sequence __attribute__((used, externally_visible));
volatile uint32_t g_hall_hold_trigger_tick __attribute__((used, externally_visible));
volatile uint32_t g_hall_hold_held_at_lock __attribute__((used, externally_visible));
volatile uint16_t g_hall_hold_baseline __attribute__((used, externally_visible));
volatile uint16_t g_hall_hold_trigger_raw __attribute__((used, externally_visible));
volatile uint8_t g_hall_hold_target_key __attribute__((used, externally_visible)) = 2U;
volatile uint8_t g_hall_hold_target_mux __attribute__((used, externally_visible));
volatile uint8_t g_hall_hold_target_adc __attribute__((used, externally_visible));
volatile uint8_t g_hall_hold_mode_arm __attribute__((used, externally_visible)) = 1U;
volatile uint8_t g_hall_hold_mode_active __attribute__((used, externally_visible));
volatile uint8_t g_hall_hold_trace_frozen __attribute__((used, externally_visible));
static uint8_t g_hall_hold_divider;
static uint16_t g_hall_stable_baseline[HALL_KEY_COUNT];
static uint16_t g_hall_stable_count[HALL_KEY_COUNT];
static uint16_t g_hall_held_count[HALL_KEY_COUNT];
static uint8_t g_hall_fixed_active;
static uint32_t g_hall_fixed_start_tick;
static uint32_t g_hall_adc1_callbacks;
static uint32_t g_hall_adc2_callbacks;
static uint32_t g_hall_adc1_callback_tick;
static uint32_t g_hall_adc2_callback_tick;
static volatile uint32_t g_hall_mux_generation;
static uint32_t g_hall_adc1_callback_generation;
static uint32_t g_hall_adc2_callback_generation;
static uint16_t g_hall_adc1_callback_ndtr;
static uint16_t g_hall_adc2_callback_ndtr;
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

static void hall_adc_probe_drift(uint8_t key, uint8_t channel, uint16_t raw)
{
    if (g_hall_hold_mode_active != 0U) return;
    if ((g_hall_fixed_active != 0U) || (g_hall_fixed_done != 0U)) return;
    if (hall_engine_is_pressed(key) == 0U)
    {
        g_hall_stable_baseline[key] = UINT16_MAX;
        g_hall_stable_count[key] = 0U;
        g_hall_held_count[key] = 0U;
        return;
    }
    if (g_hall_held_count[key] < UINT16_MAX) ++g_hall_held_count[key];
    if (raw < g_hall_stable_baseline[key])
    {
        g_hall_stable_baseline[key] = raw;
        g_hall_stable_count[key] = 1U;
    }
    else if ((uint32_t)raw <= (uint32_t)g_hall_stable_baseline[key] + 512U)
    {
        if (g_hall_stable_count[key] < UINT16_MAX) ++g_hall_stable_count[key];
    }
    if ((g_hall_hold_mode_arm != 0U)
        && (key == g_hall_hold_target_key)
        && (g_hall_held_count[key] >= 360U)
        && (g_hall_stable_count[key] >= 8U)
        && ((uint32_t)raw <= (uint32_t)g_hall_stable_baseline[key] + 512U))
    {
        g_hall_hold_target_mux = hall_mux_index;
        g_hall_hold_target_adc = channel;
        g_hall_hold_baseline = g_hall_stable_baseline[key];
        g_hall_hold_held_at_lock = g_hall_capture_held_mask;
        g_hall_hold_divider = 39U;
        g_hall_hold_mode_active = 1U;
        return;
    }
    if (g_hall_hold_mode_arm != 0U) return;
    if ((g_hall_held_count[key] >= 360U)
             && (g_hall_stable_count[key] >= 8U)
             && ((uint32_t)raw >= (uint32_t)g_hall_stable_baseline[key] + 2500U)
             && (raw < hall_engine_get_trig_hi(key)))
    {
        g_hall_fixed_key = key;
        g_hall_fixed_mux = hall_mux_index;
        g_hall_fixed_adc = channel;
        g_hall_fixed_baseline = g_hall_stable_baseline[key];
        g_hall_fixed_trigger_raw = raw;
        g_hall_fixed_count = 0U;
        g_hall_fixed_start_tick = brick_media_clock_now_tick();
        g_hall_fixed_active = 1U;
    }
}

static void hall_adc_queue_sample(uint8_t key, uint8_t channel, uint16_t raw)
{
    const uint32_t sample_count = hall_sample_count[key] + 1U;
    const uint32_t tim5_tick = brick_media_clock_now_tick();

    hall_raw[key] = raw;
    hall_sample_count[key] = sample_count;
    board_surface_update_lane(key, raw, sample_count);
    hall_adc_probe_drift(key, channel, raw);

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

    if (g_hall_hold_mode_active != 0U)
    {
        if (g_hall_hold_trace_frozen == 0U)
        {
            if (++g_hall_hold_divider >= 40U)
            {
                g_hall_hold_divider = 0U;
                const uint32_t sequence = ++g_hall_hold_sequence;
                volatile hall_hold_record_t *hold =
                    &g_hall_hold_trace[(sequence - 1U) & (HALL_HOLD_CAPACITY - 1U)];
                hold->sequence = 0U;
                hold->tim5_tick = brick_media_clock_now_tick();
                hold->raw_a = v1;
                hold->raw_b = v2;
                hold->raw_c = v3;
                hold->adc1_callbacks = (uint16_t)g_hall_adc1_callbacks;
                hold->adc2_callbacks = (uint16_t)g_hall_adc2_callbacks;
                hold->mux_odr = hall_adc_gpio_mux(0U);
                hold->mux_idr = hall_adc_gpio_mux(1U);
                hold->completing_adc = completing_adc;
                hold->reserved = 0U;
                __DMB();
                hold->sequence = sequence;
                const uint16_t target_raw = (g_hall_hold_target_adc == 0U)
                    ? v1 : ((g_hall_hold_target_adc == 1U) ? v2 : v3);
                if ((g_hall_hold_trigger_tick == 0U)
                    && ((uint32_t)target_raw >=
                        (uint32_t)g_hall_hold_baseline + 2500U))
                {
                    g_hall_hold_trigger_tick = hold->tim5_tick;
                    g_hall_hold_trigger_raw = target_raw;
                }
                if ((g_hall_hold_trigger_tick != 0U)
                    && ((uint32_t)(hold->tim5_tick - g_hall_hold_trigger_tick)
                        >= 1500000U))
                    g_hall_hold_trace_frozen = 1U;
            }
        }
        return;
    }

    if (g_hall_fixed_active != 0U)
    {
        const uint32_t index = g_hall_fixed_count;
        volatile hall_fixed_record_t *fixed = &g_hall_fixed_trace[index];
        fixed->sequence = 0U;
        fixed->tim5_tick = brick_media_clock_now_tick();
        fixed->adc1_callback_tick = g_hall_adc1_callback_tick;
        fixed->adc2_callback_tick = g_hall_adc2_callback_tick;
        fixed->adc1_callbacks = g_hall_adc1_callbacks;
        fixed->adc2_callbacks = g_hall_adc2_callbacks;
        fixed->raw_a = v1;
        fixed->raw_b = v2;
        fixed->raw_c = v3;
        fixed->adc1_ndtr_before = adc1_dma_ndtr_before;
        fixed->adc1_ndtr_after = adc1_dma_ndtr_after;
        fixed->adc2_ndtr =
            (uint16_t)((DMA_Stream_TypeDef *)hadc2.DMA_Handle->Instance)->NDTR;
        fixed->mux_odr = hall_adc_gpio_mux(0U);
        fixed->mux_idr = hall_adc_gpio_mux(1U);
        fixed->completing_adc = completing_adc;
        fixed->reserved = 0U;
        __DMB();
        fixed->sequence = index + 1U;
        g_hall_fixed_count = index + 1U;
        if ((g_hall_fixed_count >= HALL_FIXED_CAPACITY)
            || ((uint32_t)(fixed->tim5_tick - g_hall_fixed_start_tick) >= 5000U))
        {
            g_hall_fixed_active = 0U;
            g_hall_fixed_done = 1U;
            TIM6->CR1 &= ~TIM_CR1_CEN;
            g_hall_direct_state = 1U;
        }
        return;
    }

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
        record->mux_generation = g_hall_mux_generation;
        record->adc1_callback_generation = g_hall_adc1_callback_generation;
        record->adc2_callback_generation = g_hall_adc2_callback_generation;
        record->adc1_callback_ndtr = g_hall_adc1_callback_ndtr;
        record->adc2_callback_ndtr = g_hall_adc2_callback_ndtr;
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
            hall_adc_queue_sample(key_a, 0U, v1);
        }
        if (hall_keymap_key_for_mux_channel(1U, hall_mux_index, &key_b) != 0U)
        {
            hall_adc_queue_sample(key_b, 1U, v2);
        }
        if (hall_keymap_key_for_mux_channel(2U, hall_mux_index, &key_c) != 0U)
        {
            hall_adc_queue_sample(key_c, 2U, v3);
        }

        if (record != NULL)
        {
            record->held_after = g_hall_capture_held_mask;
            __DMB();
            record->sequence = trace_sequence;
        }

        if ((g_hall_fixed_active == 0U)
            && (g_hall_hold_mode_active == 0U))
        {
            hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 0x07U);
            hall_mux_select(hall_mux_index);
            ++g_hall_mux_generation;
        }
        adc1_ready = 0U;
        adc2_ready = 0U;

        hall_discard_count = ((g_hall_fixed_active != 0U)
            || (g_hall_hold_mode_active != 0U))
            ? 0U : HALL_MUX_SETTLE_DISCARD_PAIRS;
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
    g_hall_mux_generation = 0U;
    g_hall_adc1_callback_generation = 0U;
    g_hall_adc2_callback_generation = 0U;
    g_hall_adc1_callback_ndtr = 0U;
    g_hall_adc2_callback_ndtr = 0U;
    g_hall_fixed_active = 0U;
    g_hall_fixed_done = 0U;
    g_hall_fixed_count = 0U;
    g_hall_fixed_key = UINT8_MAX;
    g_hall_fixed_mux = 0U;
    g_hall_fixed_adc = 0U;
    g_hall_fixed_baseline = 0U;
    g_hall_fixed_trigger_raw = 0U;
    g_hall_direct_count = 0U;
    g_hall_direct_error = 0U;
    g_hall_direct_state = 0U;
    g_hall_direct_restore_ok = 0U;
    g_hall_hold_sequence = 0U;
    g_hall_hold_trigger_tick = 0U;
    g_hall_hold_held_at_lock = 0U;
    g_hall_hold_baseline = 0U;
    g_hall_hold_trigger_raw = 0U;
    g_hall_hold_target_mux = 0U;
    g_hall_hold_target_adc = 0U;
    g_hall_hold_mode_active = 0U;
    g_hall_hold_trace_frozen = 0U;
    g_hall_hold_divider = 0U;

    adc1_dma[0U] = 0U;
    adc1_dma[1U] = 0U;
    adc1_dma[2U] = 0U;
    adc2_dma = 0U;

    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_raw[i] = 0U;
        hall_sample_count[i] = 0U;
        g_hall_stable_baseline[i] = UINT16_MAX;
        g_hall_stable_count[i] = 0U;
        g_hall_held_count[i] = 0U;
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
    if ((hadc == NULL) || (g_hall_direct_state == 1U)
        || (g_hall_direct_state == 2U))
    {
        return;
    }

    if (board_surface_is_hall_adc1_callback(hadc) != 0U)
    {
        ++g_hall_adc1_callbacks;
        g_hall_adc1_callback_tick = brick_media_clock_now_tick();
        g_hall_adc1_callback_generation = g_hall_mux_generation;
        g_hall_adc1_callback_ndtr =
            (uint16_t)((DMA_Stream_TypeDef *)hadc->DMA_Handle->Instance)->NDTR;
        adc1_ready = 1U;
    }
    else if (board_surface_is_hall_adc2_callback(hadc) != 0U)
    {
        ++g_hall_adc2_callbacks;
        g_hall_adc2_callback_tick = brick_media_clock_now_tick();
        g_hall_adc2_callback_generation = g_hall_mux_generation;
        g_hall_adc2_callback_ndtr =
            (uint16_t)((DMA_Stream_TypeDef *)hadc->DMA_Handle->Instance)->NDTR;
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

void hall_adc_service_direct_probe(void)
{
    if (g_hall_direct_state != 1U) return;
    g_hall_direct_state = 2U;

    if (HAL_TIM_Base_Stop(&htim6) != HAL_OK) g_hall_direct_error |= 1U;
    const HAL_StatusTypeDef stop1 = HAL_ADC_Stop_DMA(&hadc1);
    const HAL_StatusTypeDef stop2 = HAL_ADC_Stop_DMA(&hadc2);
    if (stop1 != HAL_OK) g_hall_direct_error |= 2U;
    if (stop2 != HAL_OK) g_hall_direct_error |= 4U;

    if ((stop1 == HAL_OK) && (stop2 == HAL_OK))
    {
        ADC_HandleTypeDef *direct_adc = (g_hall_fixed_adc == 1U) ? &hadc2 : &hadc1;
        ADC_InjectionConfTypeDef config = {0};
        config.InjectedChannel = (g_hall_fixed_adc == 0U) ? ADC_CHANNEL_11
            : ((g_hall_fixed_adc == 1U) ? ADC_CHANNEL_18 : ADC_CHANNEL_19);
        config.InjectedRank = ADC_INJECTED_RANK_1;
        config.InjectedSamplingTime = ADC_SAMPLETIME_64CYCLES_5;
        config.InjectedSingleDiff = ADC_SINGLE_ENDED;
        config.InjectedOffsetNumber = ADC_OFFSET_NONE;
        config.InjectedNbrOfConversion = 1U;
        config.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START;
        config.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_NONE;
        if (HAL_ADCEx_InjectedConfigChannel(direct_adc, &config) != HAL_OK)
        {
            g_hall_direct_error |= 8U;
        }
        else
        {
            for (uint32_t i = 0U; i < HALL_DIRECT_CAPACITY; ++i)
            {
                if (HAL_ADCEx_InjectedStart(direct_adc) != HAL_OK)
                {
                    g_hall_direct_error |= 16U;
                    break;
                }
                if (HAL_ADCEx_InjectedPollForConversion(direct_adc, 2U) != HAL_OK)
                {
                    g_hall_direct_error |= 32U;
                    (void)HAL_ADCEx_InjectedStop(direct_adc);
                    break;
                }
                volatile hall_direct_record_t *sample = &g_hall_direct_trace[i];
                sample->sequence = 0U;
                sample->tim5_tick = brick_media_clock_now_tick();
                sample->adc_isr = direct_adc->Instance->ISR;
                sample->raw = (uint16_t)HAL_ADCEx_InjectedGetValue(
                    direct_adc, ADC_INJECTED_RANK_1);
                sample->status = 0U;
                __DMB();
                sample->sequence = i + 1U;
                g_hall_direct_count = i + 1U;
                if (HAL_ADCEx_InjectedStop(direct_adc) != HAL_OK)
                {
                    g_hall_direct_error |= 64U;
                    break;
                }
            }
        }
    }

    adc1_ready = 0U;
    adc2_ready = 0U;
    hall_discard_count = HALL_MUX_SETTLE_DISCARD_PAIRS;
    hall_mux_index = (uint8_t)((g_hall_fixed_mux + 1U) & 0x07U);
    hall_mux_select(hall_mux_index);
    ++g_hall_mux_generation;
    if (board_surface_start_hall_adc_dma(adc1_dma, &adc2_dma) == 0U)
    {
        g_hall_direct_error |= 128U;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        (void)HAL_ADC_Stop_DMA(&hadc2);
    }
    else if (board_surface_start_hall_scan_timer() == 0U)
    {
        g_hall_direct_error |= 256U;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        (void)HAL_ADC_Stop_DMA(&hadc2);
    }
    else
    {
        g_hall_direct_restore_ok = 1U;
    }
    g_hall_capture_frozen = 1U;
    g_hall_direct_state = 3U;
}
