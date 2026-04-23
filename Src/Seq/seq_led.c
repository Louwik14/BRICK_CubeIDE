/*
 * Module: seq_led
 * Role: Rendu LED de l'état séquenceur pour la page active.
 * Responsibilities: afficher trig/plocks, playhead, et couches visuelles associées.
 * Integration: lit seq_model + seq_runtime et écrit via led_layer; aucune logique transport.
 */
#include "Seq/seq_led.h"

#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "UI/ui_core.h"
#include "led_layer.h"
#include "led_remap.h"

#define SEQ_LED_BLUE_B 128U
#define SEQ_LED_GREEN_G 128U
#define SEQ_LED_WHITE   128U

void seq_led_render_active_track_page(void)
{
    const uint8_t track = ui_get_active_track();
    const uint8_t page = seq_edit_get_page(track);
    const uint8_t base_step = (uint8_t)(page * SEQ_STEPS_PER_PAGE);

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        const led_id_t led = led_remap_led_for_hall(hall);
        const uint8_t step = (uint8_t)(base_step + hall);
        const seq_step_visual_t visual = seq_model_get_step_visual(track, step);

        if (visual == SEQ_STEP_VISUAL_BLUE)
        {
            led_layer_set(LED_LAYER_SEQ_STATE, led, 0U, 0U, SEQ_LED_BLUE_B);
        }
        else if (visual == SEQ_STEP_VISUAL_GREEN)
        {
            led_layer_set(LED_LAYER_SEQ_STATE, led, 0U, SEQ_LED_GREEN_G, 0U);
        }
        else
        {
            led_layer_set(LED_LAYER_SEQ_STATE, led, 0U, 0U, 0U);
        }
    }

    /* Projection read: LED cursor visibility follows runtime running/playhead mirrors. */
    if (seq_runtime_is_running() == 0U)
    {
        return;
    }

    seq_step_id_t playhead = 0U;
    /* Projection read: playhead is consumed as a runtime mirror for cursor rendering. */
    if (seq_runtime_get_playhead_step(track, &playhead) == 0U)
    {
        return;
    }

    if ((playhead < base_step) || (playhead >= (uint8_t)(base_step + SEQ_STEPS_PER_PAGE)))
    {
        return;
    }

    const uint8_t hall = (uint8_t)(playhead - base_step);
    const led_id_t led = led_remap_led_for_hall(hall);
    led_layer_set(LED_LAYER_SEQ_CURSOR, led, SEQ_LED_WHITE, SEQ_LED_WHITE, SEQ_LED_WHITE);
}
