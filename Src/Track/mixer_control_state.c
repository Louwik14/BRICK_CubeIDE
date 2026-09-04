#include "Track/mixer_control_state.h"

#include <math.h>
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_registry.h"
#include <stddef.h>

#include "Platform/memory_layout.h"
#include "Track/entity_types.h"

CONTROL_STATE_SDRAM static mixer_control_state_t
    g_mixer_control[BRICK_ENTITY_CAPACITY];

void mixer_control_state_init(void)
{
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        g_mixer_control[entity] = (mixer_control_state_t){
            .level = 1.0f,
            .pan = 0.0f,
            .send1 = 0.0f,
            .send2 = 0.0f,
            .send3 = 0.0f
        };
    }
}

uint8_t mixer_control_state_reset(uint8_t entity)
{
    const mixer_control_state_t state = { .level = 1.0f };
    return mixer_control_state_restore(entity, &state);
}

uint8_t mixer_control_state_get_param(uint8_t entity, param_id_t id,
                                      float *out_value)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_value == NULL)) return 0U;
    const mixer_control_state_t *const state = &g_mixer_control[entity];
    switch (id)
    {
        case PARAM_MIX_LEVEL: *out_value = state->level; return 1U;
        case PARAM_MIX_PAN: *out_value = state->pan; return 1U;
        case PARAM_MIX_SEND1: *out_value = state->send1; return 1U;
        case PARAM_MIX_SEND2: *out_value = state->send2; return 1U;
        case PARAM_MIX_SEND3: *out_value = state->send3; return 1U;
        default: return 0U;
    }
}

uint8_t mixer_control_state_set_param(uint8_t entity, param_id_t id,
                                      float value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    mixer_control_state_t *const state = &g_mixer_control[entity];
    switch (id)
    {
        case PARAM_MIX_LEVEL: state->level = value; return 1U;
        case PARAM_MIX_PAN: state->pan = value; return 1U;
        case PARAM_MIX_SEND1: state->send1 = value; return 1U;
        case PARAM_MIX_SEND2: state->send2 = value; return 1U;
        case PARAM_MIX_SEND3: state->send3 = value; return 1U;
        default: return 0U;
    }
}

uint8_t mixer_control_state_capture(uint8_t entity,mixer_control_state_t*out_state)
{if(entity>=BRICK_ENTITY_CAPACITY||out_state==NULL)return 0U;*out_state=g_mixer_control[entity];return 1U;}
uint8_t mixer_control_state_validate(const mixer_control_state_t*state)
{
    if(state==NULL)return 0U;
    if(!isfinite(state->level)||state->level<0.0f||state->level>2.0f
            ||!isfinite(state->pan)||state->pan<-1.0f||state->pan>1.0f)
        return 0U;
    const float*v=&state->send1;
    for(uint8_t i=0U;i<3U;++i)
        if(!isfinite(v[i])||v[i]<0.0f||v[i]>1.0f)return 0U;
    return 1U;
}
uint8_t mixer_control_state_restore(uint8_t entity,const mixer_control_state_t*state)
{
    if(entity>=BRICK_ENTITY_CAPACITY||!mixer_control_state_validate(state))return 0U;
    static const param_id_t ids[5U]={PARAM_MIX_LEVEL,PARAM_MIX_PAN,
        PARAM_MIX_SEND1,PARAM_MIX_SEND2,PARAM_MIX_SEND3};
    mixer_control_state_t canonical_state=*state;
    float *const values=&canonical_state.level;
    for(uint8_t i=0U;i<5U;++i){param_registry_prepared_value_t prepared;
        if(!param_registry_prepare_value(ids[i],values[i],&prepared))return 0U;
        values[i]=prepared.value;}
    live_parameter_audio_bulk_t bulk={.capture_tick=0U,
        .count=5U};
    for(uint8_t i=0U;i<5U;++i)bulk.item[i]=
        (live_parameter_audio_bulk_item_t){.parameter_id=(uint16_t)ids[i],
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=entity,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float(values[i])};
    if(!live_parameter_audio_publication_submit_bulk_now(&bulk))return 0U;
    g_mixer_control[entity]=canonical_state;
    return 1U;
}
