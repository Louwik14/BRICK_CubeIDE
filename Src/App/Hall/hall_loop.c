#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "stm32h7xx_hal.h"

/*
===============================================================================
Module Hall centralisé

 Initialisation : préparer d'abord le moteur Hall, puis démarrer l'acquisition
 ADC/DMA. L'IRQ Hall ne fait que capturer les samples validés et les empiler
 sans lancer la logique métier. La superloop dépile ensuite la FIFO brute et
 nourrit directement le moteur Hall afin de préserver l'attaque et la
 détection press/navigation.
===============================================================================
*/

void hall_loop_init(void)
{
    hall_engine_init();
    hall_adc_init();
}

void hall_loop_process(void)
{
#if !defined(BRICK6_VARIANT_LOWCOST)
    hall_adc_sample_t sample;
    uint32_t samples_processed = 0U;

    while (samples_processed < HALL_LOOP_MAX_SAMPLES_PER_POLL)
    {
        if (hall_adc_pop_sample(&sample) == 0U)
        {
            break;
        }

        samples_processed++;

        hall_engine_process_sample(sample.key, sample.raw, sample.sample_count);
    }
#endif

    hall_engine_process();
}
