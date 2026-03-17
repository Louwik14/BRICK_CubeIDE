#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"

/*
===============================================================================
Module Hall centralisé

Ce fichier regroupe tous les sous-modules Hall pour éviter de
polluer la super-loop.

Ordre logique :

ADC → ENGINE → VELOCITY
===============================================================================
*/

void hall_loop_init(void)
{
    hall_adc_init();
    hall_engine_init();
}

void hall_loop_process(void)
{
    hall_engine_process();
}
