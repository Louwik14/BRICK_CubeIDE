#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"

/*
===============================================================================
Module Hall centralisé

Initialisation : préparer d'abord le moteur Hall, puis démarrer l'acquisition
ADC/DMA. L'IRQ Hall ne fait que capturer les samples validés et les empiler
sans lancer la logique métier. La superloop dépile ensuite la FIFO dans
l'ordre temporel exact pour nourrir le moteur Hall hors IRQ.
===============================================================================
*/

void hall_loop_init(void)
{
    hall_engine_init();
    hall_adc_init();
}

void hall_loop_process(void)
{
    hall_adc_sample_t sample;

    while (hall_adc_pop_sample(&sample) != 0U)
    {
        hall_engine_process_sample(sample.key, sample.raw, sample.sample_count);
    }

    hall_engine_process();
}
