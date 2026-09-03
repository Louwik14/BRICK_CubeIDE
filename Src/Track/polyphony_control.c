#include "Track/polyphony_control.h"

#include "IPC/live_clock_control.h"
#include "IPC/control_audio_command.h"
#include "App/live_parameter_audio_publication.h"
#include "Track/entity_types.h"
#include "Track/synth_polyphony.h"
#include "Param/param_registry.h"
#include "IPC/live_parameter_event.h"
#include <math.h>
#include <stddef.h>

static uint8_t g_polyphony_voice_count[BRICK_ENTITY_CAPACITY];
static float g_polyphony_spread[BRICK_ENTITY_CAPACITY];

void polyphony_control_init(void)
{
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        g_polyphony_voice_count[track] = 1U;
        g_polyphony_spread[track] = 0.0f;
    }
}

uint8_t polyphony_control_reset(uint8_t track)
{
    const polyphony_control_state_t state = { .voice_count = 1U };
    return polyphony_control_restore(track, &state);
}

uint8_t polyphony_control_get_voice_count(uint8_t track)
{
    return (track < BRICK_ENTITY_CAPACITY) ? g_polyphony_voice_count[track] : 1U;
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
{if(track>=BRICK_ENTITY_CAPACITY||out_state==NULL)return 0U;out_state->voice_count=g_polyphony_voice_count[track];out_state->spread=g_polyphony_spread[track];return 1U;}
uint8_t polyphony_control_prepare(const polyphony_control_state_t*state,
                                  polyphony_control_state_t*out_prepared)
{
    param_registry_prepared_value_t spread;
    if(state==NULL||out_prepared==NULL||state->voice_count<1U
            ||state->voice_count>SYNTH_POLYPHONY_MAX_VOICES
            ||!isfinite(state->spread)
            ||!param_registry_prepare_value(PARAM_CFG_POLY_SPREAD,
                state->spread,&spread))return 0U;
    *out_prepared=(polyphony_control_state_t){state->voice_count,spread.value};
    return 1U;
}
uint8_t polyphony_control_bulk_add(uint8_t track,
    const polyphony_control_state_t*prepared,live_parameter_audio_bulk_t*bulk)
{
    if(track>=BRICK_ENTITY_CAPACITY||prepared==NULL||bulk==NULL
            ||bulk->count>(LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS-2U))return 0U;
    bulk->item[bulk->count++]=(live_parameter_audio_bulk_item_t){
        .parameter_id=CONTROL_AUDIO_CONFIG_POLY_VOICES,
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=track,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float((float)prepared->voice_count)};
    bulk->item[bulk->count++]=(live_parameter_audio_bulk_item_t){
        .parameter_id=PARAM_CFG_POLY_SPREAD,
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=track,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float(prepared->spread)};
    return 1U;
}
uint8_t polyphony_control_install_prepared(uint8_t track,
    const polyphony_control_state_t*prepared)
{
    if(track>=BRICK_ENTITY_CAPACITY||prepared==NULL)return 0U;
    g_polyphony_voice_count[track]=prepared->voice_count;
    g_polyphony_spread[track]=prepared->spread;
    return 1U;
}
uint8_t polyphony_control_restore(uint8_t track,const polyphony_control_state_t*state)
{polyphony_control_state_t prepared;live_parameter_audio_bulk_t bulk={.capture_tick=live_clock_capture_tick()};if(track>=BRICK_ENTITY_CAPACITY||!polyphony_control_prepare(state,&prepared)||!polyphony_control_bulk_add(track,&prepared,&bulk)||!live_parameter_audio_publication_submit_bulk(&bulk))return 0U;return polyphony_control_install_prepared(track,&prepared);}
