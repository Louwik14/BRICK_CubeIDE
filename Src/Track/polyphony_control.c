#include "Track/polyphony_control.h"

#include "Platform/brick_media_clock.h"
#include "ControlRT/control_audio_command.h"
#include "ControlRT/live_parameter_audio_publication.h"
#include "Track/entity_types.h"
#include "Track/synth_polyphony.h"
#include "Seq/seq_engine.h"
#include "Track/control_music_output.h"
#include "Track/track_runtime.h"
#include "Param/param_registry.h"
#include "ControlRT/live_parameter_event.h"
#include "main.h"
#include <math.h>
#include <stddef.h>

static float g_polyphony_spread[BRICK_ENTITY_CAPACITY];

void polyphony_control_init(void)
{
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        g_polyphony_spread[track] = 0.0f;
    }
}

void polyphony_control_make_default(polyphony_control_state_t *out)
{
    if (out == NULL) return;
    *out = (polyphony_control_state_t){ .spread = 0.0f };
}

uint8_t polyphony_control_reset(uint8_t track)
{
    polyphony_control_state_t state;
    polyphony_control_make_default(&state);
    return polyphony_control_restore(track, &state);
}

uint8_t polyphony_control_get_spread(uint8_t track, float *out_spread)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (out_spread == NULL)) return 0U;
    *out_spread = g_polyphony_spread[track];
    return 1U;
}

uint8_t polyphony_control_set_spread(uint8_t track, float spread)
{
    if (track >= BRICK_ENTITY_CAPACITY) return 0U;
    g_polyphony_spread[track] = spread;
    return 1U;
}

uint8_t polyphony_control_capture(uint8_t track,polyphony_control_state_t*out_state)
{if(track>=BRICK_ENTITY_CAPACITY||out_state==NULL)return 0U;out_state->spread=g_polyphony_spread[track];return 1U;}
uint8_t polyphony_control_prepare(const polyphony_control_state_t*state,
                                  polyphony_control_state_t*out_prepared)
{
    param_registry_prepared_value_t spread;
    if(state==NULL||out_prepared==NULL||!isfinite(state->spread)
            ||!param_registry_prepare_value(PARAM_CFG_POLY_SPREAD,
                state->spread,&spread))return 0U;
    *out_prepared=(polyphony_control_state_t){.spread=spread.value};
    return 1U;
}
uint8_t polyphony_control_bulk_add(uint8_t track,
    const polyphony_control_state_t*prepared,live_parameter_audio_bulk_t*bulk)
{
    if(track>=BRICK_ENTITY_CAPACITY||prepared==NULL||bulk==NULL
            ||bulk->count>(LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS-1U))return 0U;
    if(track_runtime_get_effective_param_status(track,PARAM_CFG_POLY_SPREAD)
            !=TRACK_RUNTIME_PARAM_ALLOWED)return 1U;
    bulk->item[bulk->count++]=(live_parameter_audio_target_t){
        .parameter_id=PARAM_CFG_POLY_SPREAD,
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=track,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .semantic=CONTROL_AUDIO_PARAM_BASE,
        .value=live_parameter_event_encode_float(prepared->spread)};
    return 1U;
}
uint8_t polyphony_control_install_prepared(uint8_t track,
    const polyphony_control_state_t*prepared)
{
    if(track>=BRICK_ENTITY_CAPACITY||prepared==NULL)return 0U;
    g_polyphony_spread[track]=prepared->spread;
    return 1U;
}
uint8_t polyphony_control_restore(uint8_t track,const polyphony_control_state_t*state)
{polyphony_control_state_t prepared;live_parameter_audio_bulk_t bulk={.capture_tick=brick_media_clock_now_tick()};if(track>=BRICK_ENTITY_CAPACITY||!polyphony_control_prepare(state,&prepared)||!polyphony_control_bulk_add(track,&prepared,&bulk)||((bulk.count!=0U)&&!live_parameter_audio_publication_submit_bulk(&bulk)))return 0U;const uint8_t installed=polyphony_control_install_prepared(track,&prepared);if(installed==0U)Error_Handler();return installed;}
