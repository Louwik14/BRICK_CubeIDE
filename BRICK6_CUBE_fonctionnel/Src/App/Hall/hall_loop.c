#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"

/*
===============================================================================
Module Hall centralisé

Initialisation : préparer d'abord le moteur Hall, puis démarrer l'acquisition
ADC/DMA. La logique Hall critique est exécutée dans l'IRQ ADC/DMA Hall.
La superloop ne fait plus d'interprétation fine des samples.
===============================================================================
*/

void hall_loop_init(void)
{
    hall_engine_init();
    hall_adc_init();
}

void hall_loop_process(void)
{
    hall_engine_process();
}
