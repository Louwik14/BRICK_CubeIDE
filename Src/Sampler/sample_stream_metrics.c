#include "Sampler/sample_stream_metrics.h"

#include <stddef.h>
#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/sample_page_cache.h"
#include "stm32h7xx.h"

typedef struct
{
    uint32_t start_cycles;
    uint16_t slot_index;
    uint8_t active;
} sample_stream_refill_metric_t;

UI_HOT_DTCM volatile sample_stream_metrics_t g_sample_stream_metrics
    __attribute__((used, aligned(32)));

static sample_stream_refill_metric_t
    g_sample_stream_refill_metrics[SAMPLE_PAGE_MAX_COUNT];

_Static_assert(sizeof(sample_stream_metric_t) == 0x10U,
               "sample stream metric layout");
_Static_assert(offsetof(sample_stream_metrics_t, state) == 0x00U,
               "sample stream metric state offset");
_Static_assert(offsetof(sample_stream_metrics_t, metric) == 0x08U,
               "sample stream metrics offset");
_Static_assert(sizeof(sample_stream_metrics_t) == 0xB8U,
               "sample stream metrics layout");

static void sample_stream_metrics_prepare(void)
{
    if (g_sample_stream_metrics.state != 1U) return;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (g_sample_stream_metrics.state == 1U)
    {
        volatile uint32_t *const words =
            (volatile uint32_t *)&g_sample_stream_metrics;
        for (uint32_t i = 0U;
             i < (sizeof(g_sample_stream_metrics) / sizeof(words[0])); ++i)
        {
            words[i] = 0U;
        }
        memset(g_sample_stream_refill_metrics, 0,
               sizeof(g_sample_stream_refill_metrics));
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        g_sample_stream_metrics.state = 2U;
    }
    if (primask == 0U) __enable_irq();
}

static void sample_stream_metrics_add(sample_stream_metric_id_t id,
                                      uint32_t cycles)
{
    if ((g_sample_stream_metrics.state != 2U)
        || ((uint32_t)id >= SAMPLE_STREAM_METRIC_COUNT)) return;
    volatile sample_stream_metric_t *const metric =
        &g_sample_stream_metrics.metric[id];
    const uint32_t previous = metric->total_cycles_lo;
    metric->calls++;
    metric->total_cycles_lo = previous + cycles;
    if (metric->total_cycles_lo < previous) metric->total_cycles_hi++;
    if (cycles > metric->max_cycles) metric->max_cycles = cycles;
}

uint32_t sample_stream_metrics_begin(void)
{
    sample_stream_metrics_prepare();
    return DWT->CYCCNT;
}

void sample_stream_metrics_end(sample_stream_metric_id_t metric,
                               uint32_t start_cycles)
{
    sample_stream_metrics_add(metric, DWT->CYCCNT - start_cycles);
}

void sample_stream_metrics_refill_begin(uint16_t slot_index)
{
    const uint32_t start = sample_stream_metrics_begin();
    if ((g_sample_stream_metrics.state == 2U)
        && (slot_index < SAMPLE_PAGE_MAX_COUNT))
    {
        g_sample_stream_refill_metrics[slot_index].start_cycles = start;
        g_sample_stream_refill_metrics[slot_index].slot_index = slot_index;
        g_sample_stream_refill_metrics[slot_index].active = 1U;
    }
}

void sample_stream_metrics_refill_ready(uint16_t slot_index)
{
    if ((g_sample_stream_metrics.state == 2U)
        && (slot_index < SAMPLE_PAGE_MAX_COUNT)
        && (g_sample_stream_refill_metrics[slot_index].active != 0U))
    {
        sample_stream_metrics_add(SAMPLE_STREAM_METRIC_REFILL_TOTAL,
            DWT->CYCCNT
                - g_sample_stream_refill_metrics[slot_index].start_cycles);
        g_sample_stream_refill_metrics[slot_index].active = 0U;
    }
}
