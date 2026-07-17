#include "App/Hall/hall_adc.h"

#include "Board/board_surface.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define HALL_MUX_COUNT         8U
#define HALL_SAMPLE_FIFO_SIZE  512U
#define HALL_SAMPLE_FIFO_MASK  (HALL_SAMPLE_FIFO_SIZE - 1U)

/*
 * ADC DMA mailboxes (1 sample per stream):
 * - DMA writes (ADC1/ADC2 circular, length 1)
 * - CPU reads in hall_adc_process_pair()
 *
 * Placement in DMA_BUFFER prepares a non-cacheable policy at MPU stage.
 */
static DMA_BUFFER volatile uint16_t adc1_dma;
static DMA_BUFFER volatile uint16_t adc2_dma;

static volatile uint16_t hall_raw[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count[HALL_KEY_COUNT];

static hall_adc_sample_t hall_sample_fifo[HALL_SAMPLE_FIFO_SIZE];
static volatile uint32_t hall_fifo_head;
static volatile uint32_t hall_fifo_tail;
static volatile uint32_t hall_fifo_push_count;
static volatile uint32_t hall_fifo_drop_count;
static volatile uint16_t hall_fifo_max_depth;

static volatile uint8_t hall_mux_index;
static volatile uint8_t hall_discard_count;
static volatile uint8_t adc1_ready;
static volatile uint8_t adc2_ready;
/*
 * Table de remap physique MUX -> index logique hall.
 *
 * Mapping demandé :
 * hall 0 -> mux 4
 * hall 1 -> mux 6
 * hall 2 -> mux 7
 * hall 3 -> mux 5
 * hall 4 -> mux 3
 * hall 5 -> mux 0
 * hall 6 -> mux 1
 * hall 7 -> mux 2
 * hall 8 -> mux 12
 * hall 9 -> mux 14
 * hall 10 -> mux 15
 * hall 11 -> mux 13
 * hall 12 -> mux 11
 * hall 13 -> mux 8
 * hall 14 -> mux 9
 * hall 15 -> mux 10
 *
 * Donc inverse MUX -> hall :
 * mux 0  -> hall 5
 * mux 1  -> hall 6
 * mux 2  -> hall 7
 * mux 3  -> hall 4
 * mux 4  -> hall 0
 * mux 5  -> hall 3
 * mux 6  -> hall 1
 * mux 7  -> hall 2
 * mux 8  -> hall 13
 * mux 9  -> hall 14
 * mux 10 -> hall 15
 * mux 11 -> hall 12
 * mux 12 -> hall 8
 * mux 13 -> hall 11
 * mux 14 -> hall 9
 * mux 15 -> hall 10
 */
static const uint8_t hall_key_from_mux[HALL_KEY_COUNT] =
{
    5U,  6U,  7U,  4U,
    0U,  3U,  1U,  2U,
    13U, 14U, 15U, 12U,
    8U,  11U, 9U,  10U
};

static void hall_mux_select(uint8_t index)
{
    board_surface_select_hall_mux(index);
}

static void hall_adc_queue_sample(uint8_t key, uint16_t raw)
{
    const uint32_t sample_count = hall_sample_count[key] + 1U;
    const uint32_t head = hall_fifo_head;
    const uint32_t tail = hall_fifo_tail;
    const uint32_t depth = head - tail;

    hall_raw[key] = raw;
    hall_sample_count[key] = sample_count;
    board_surface_update_lane(key, raw, sample_count);

    if (depth >= HALL_SAMPLE_FIFO_SIZE)
    {
        hall_fifo_drop_count++;
        return;
    }

    {
        hall_adc_sample_t *entry = &hall_sample_fifo[head & HALL_SAMPLE_FIFO_MASK];
        const uint16_t new_depth = (uint16_t)(depth + 1U);

        entry->key = key;
        entry->raw = raw;
        entry->sample_count = sample_count;

        __DMB();
        hall_fifo_head = head + 1U;
        hall_fifo_push_count++;

        if (new_depth > hall_fifo_max_depth)
        {
            hall_fifo_max_depth = new_depth;
        }
    }
}

static void hall_adc_process_pair(void)
{
    const uint16_t v1 = adc1_dma;
    const uint16_t v2 = adc2_dma;

    if (hall_discard_count != 0U)
    {
        hall_discard_count--;
        return;
    }

    {
        const uint8_t mux_a = hall_mux_index;
        const uint8_t mux_b = (uint8_t)(mux_a + HALL_MUX_COUNT);
        const uint8_t key_a = hall_key_from_mux[mux_a];
        const uint8_t key_b = hall_key_from_mux[mux_b];

        hall_adc_queue_sample(key_a, v1);
        hall_adc_queue_sample(key_b, v2);

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

    adc1_dma = 0U;
    adc2_dma = 0U;

    hall_fifo_head = 0U;
    hall_fifo_tail = 0U;
    hall_fifo_push_count = 0U;
    hall_fifo_drop_count = 0U;
    hall_fifo_max_depth = 0U;

    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_raw[i] = 0U;
        hall_sample_count[i] = 0U;
    }

    if (board_surface_start_hall_adc_dma(&adc1_dma, &adc2_dma) == 0U)
    {
        return;
    }

    if (board_surface_start_hall_scan_timer() == 0U)
    {
        return;
    }
}

uint8_t hall_adc_pop_sample(hall_adc_sample_t *sample)
{
    const uint32_t tail = hall_fifo_tail;

    if (sample == 0)
    {
        return 0U;
    }

    if (tail == hall_fifo_head)
    {
        return 0U;
    }

    *sample = hall_sample_fifo[tail & HALL_SAMPLE_FIFO_MASK];

    __DMB();
    hall_fifo_tail = tail + 1U;

    return 1U;
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

uint32_t hall_adc_get_sample_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_sample_count[key];
}

uint32_t hall_adc_get_fifo_push_count(void)
{
    return hall_fifo_push_count;
}

uint32_t hall_adc_get_fifo_drop_count(void)
{
    return hall_fifo_drop_count;
}

uint16_t hall_adc_get_fifo_depth(void)
{
    const uint32_t depth = hall_fifo_head - hall_fifo_tail;

    if (depth > 65535U)
    {
        return 65535U;
    }

    return (uint16_t)depth;
}

uint16_t hall_adc_get_fifo_max_depth(void)
{
    return hall_fifo_max_depth;
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
