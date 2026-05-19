#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_filter_asc.h"
#include "stm32h7xx_hal.h"

/*
===============================================================================
Module Hall centralisé

Initialisation : préparer d'abord le moteur Hall et le filtre ASC, puis
démarrer l'acquisition ADC/DMA. L'IRQ Hall ne fait que capturer les samples
validés et les empiler sans lancer la logique métier. La superloop dépile
ensuite la FIFO brute, applique l'ASC par touche hors IRQ, puis nourrit le
moteur Hall uniquement quand une sortie filtrée est prête.
===============================================================================
*/

volatile hall_loop_metrics_t g_hall_loop_metrics;

void hall_loop_init(void)
{
    g_hall_loop_metrics.calls = 0U;
    g_hall_loop_metrics.last_cycles = 0U;
    g_hall_loop_metrics.max_cycles = 0U;
    g_hall_loop_metrics.last_samples = 0U;
    g_hall_loop_metrics.max_samples = 0U;
    g_hall_loop_metrics.last_backlog_samples = 0U;
    g_hall_loop_metrics.max_backlog_samples = 0U;
    g_hall_loop_metrics.cap_hit_count = 0U;

    hall_engine_init();
    hall_filter_asc_init();
    hall_adc_init();
}

void hall_loop_process(void)
{
    const uint32_t start_cycles = DWT->CYCCNT;
    hall_adc_sample_t sample;
    uint32_t samples_processed = 0U;

    while (samples_processed < HALL_LOOP_MAX_SAMPLES_PER_POLL)
    {
        uint16_t filtered_raw;

        if (hall_adc_pop_sample(&sample) == 0U)
        {
            break;
        }

        samples_processed++;

        if (hall_filter_asc_process(sample.key, sample.raw, &filtered_raw) != 0U)
        {
            hall_engine_process_sample(sample.key, filtered_raw, sample.sample_count);
        }
    }

    hall_engine_process();

    {
        const uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;
        const uint32_t backlog_samples = hall_adc_get_fifo_depth();

        g_hall_loop_metrics.calls++;
        g_hall_loop_metrics.last_cycles = elapsed_cycles;
        if (elapsed_cycles > g_hall_loop_metrics.max_cycles)
        {
            g_hall_loop_metrics.max_cycles = elapsed_cycles;
        }

        g_hall_loop_metrics.last_samples = samples_processed;
        if (samples_processed > g_hall_loop_metrics.max_samples)
        {
            g_hall_loop_metrics.max_samples = samples_processed;
        }

        g_hall_loop_metrics.last_backlog_samples = backlog_samples;
        if (backlog_samples > g_hall_loop_metrics.max_backlog_samples)
        {
            g_hall_loop_metrics.max_backlog_samples = backlog_samples;
        }

        if ((samples_processed >= HALL_LOOP_MAX_SAMPLES_PER_POLL) &&
            (backlog_samples != 0U))
        {
            g_hall_loop_metrics.cap_hit_count++;
        }
    }
}
