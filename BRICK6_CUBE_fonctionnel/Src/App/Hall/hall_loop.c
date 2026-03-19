#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_filter_asc.h"

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
    hall_filter_asc_init();
    hall_adc_init();
}

void hall_loop_process(void)
{
    hall_adc_sample_t sample;

    while (hall_adc_pop_sample(&sample) != 0U)
    {
        uint16_t filtered_raw;

        if (hall_filter_asc_process(sample.key, sample.raw, &filtered_raw) != 0U)
        {
            hall_engine_process_sample(sample.key, filtered_raw, sample.sample_count);
        }
    }

    hall_engine_process();
}
