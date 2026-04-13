#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_filter_asc.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_core.h"

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
    keyboard_runtime_init();
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

    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        const uint8_t note_on_pending = hall_engine_consume_note_on(key);
        const uint8_t note_off_pending = hall_engine_consume_note_off(key);

        if (ui_core_hall_note_is_suppressed(key) != 0U)
        {
            if (note_off_pending != 0U)
            {
                ui_core_clear_hall_note_suppression(key);
            }
            continue;
        }

        const ui_hall_mode_t hall_mode = ui_get_hall_mode();
        if ((hall_mode != UI_HALL_MODE_KEYBOARD) && (hall_mode != UI_HALL_MODE_ARP))
        {
            continue;
        }

        uint8_t velocity = hall_engine_get_velocity(key);
        if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
        {
            velocity = 100U;
        }

        if (note_on_pending != 0U)
        {
            keyboard_runtime_process_hall(key, true, velocity);
        }

        if (note_off_pending != 0U)
        {
            keyboard_runtime_process_hall(key, false, velocity);
        }
    }

    keyboard_runtime_tick();
}
