#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#if !defined(BRICK6_VARIANT_LOWCOST)
#include "App/Hall/hall_filter_asc.h"
#endif
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

void hall_loop_init(void)
{
    hall_engine_init();
#if !defined(BRICK6_VARIANT_LOWCOST)
    hall_filter_asc_init();
#endif
    hall_adc_init();
}

void hall_loop_process(void)
{
    hall_adc_sample_t sample;
    uint32_t samples_processed = 0U;

    while (samples_processed < HALL_LOOP_MAX_SAMPLES_PER_POLL)
    {
        if (hall_adc_pop_sample(&sample) == 0U)
        {
            break;
        }

        samples_processed++;

#if defined(BRICK6_VARIANT_LOWCOST)
        hall_engine_process_sample(sample.key, sample.raw, sample.sample_count);
#else
        {
            uint16_t filtered_raw;
            if (hall_filter_asc_process(sample.key, sample.raw, &filtered_raw) != 0U)
            {
                hall_engine_process_sample(sample.key, filtered_raw, sample.sample_count);
            }
        }
#endif
    }

    hall_engine_process();
}
