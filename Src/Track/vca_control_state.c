#include "Track/vca_control_state.h"

#include <math.h>
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_registry.h"
#include <stddef.h>

#include "Platform/memory_layout.h"
#include "Track/entity_types.h"

CONTROL_STATE_SDRAM static vca_control_state_t
    g_vca_control[BRICK_ENTITY_CAPACITY];

void vca_control_state_init(void)
{
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        g_vca_control[entity] = (vca_control_state_t){
            .attack = 0.0f,
            .decay = 0.0f,
            .sustain = 127.0f,
            .release = 0.0f,
            .filter_mode = 0.0f,
            .retrigger = 1.0f
        };
    }
}

uint8_t vca_control_state_reset(uint8_t entity)
{
    const vca_control_state_t state = {
        .sustain = 127.0f, .retrigger = 1.0f
    };
    return vca_control_state_restore(entity, &state);
}

uint8_t vca_control_state_get_param(uint8_t entity, param_id_t id,
                                    float *out_value)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_value == NULL)) return 0U;
    const vca_control_state_t *const state = &g_vca_control[entity];
    switch (id)
    {
        case PARAM_VCA_ATTACK: *out_value = state->attack; return 1U;
        case PARAM_VCA_DECAY: *out_value = state->decay; return 1U;
        case PARAM_VCA_SUSTAIN: *out_value = state->sustain; return 1U;
        case PARAM_VCA_RELEASE: *out_value = state->release; return 1U;
        case PARAM_FILTER_MODE: *out_value = state->filter_mode; return 1U;
        case PARAM_ENV_RETRIG_VCA: *out_value = state->retrigger; return 1U;
        default: return 0U;
    }
}

uint8_t vca_control_state_set_param(uint8_t entity, param_id_t id, float value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    vca_control_state_t *const state = &g_vca_control[entity];
    switch (id)
    {
        case PARAM_VCA_ATTACK: state->attack = value; return 1U;
        case PARAM_VCA_DECAY: state->decay = value; return 1U;
        case PARAM_VCA_SUSTAIN: state->sustain = value; return 1U;
        case PARAM_VCA_RELEASE: state->release = value; return 1U;
        case PARAM_FILTER_MODE: state->filter_mode = value; return 1U;
        case PARAM_ENV_RETRIG_VCA:
            state->retrigger = (value >= 0.5f) ? 1.0f : 0.0f;
            return 1U;
        default: return 0U;
    }
}

uint8_t vca_control_state_capture(uint8_t entity, vca_control_state_t *out_state)
{ if(entity>=BRICK_ENTITY_CAPACITY||out_state==NULL)return 0U;*out_state=g_vca_control[entity];return 1U; }
uint8_t vca_control_state_validate(const vca_control_state_t *state)
{
    if(state==NULL)return 0U;
    const float*v=&state->attack;
    for(uint8_t i=0U;i<4U;++i)
        if(!isfinite(v[i])||v[i]<0.0f||v[i]>127.0f)return 0U;
    if(!isfinite(state->filter_mode)||state->filter_mode<0.0f
            ||state->filter_mode>2.0f
            ||state->filter_mode!=(float)(uint8_t)state->filter_mode)return 0U;
    if((state->retrigger!=0.0f)&&(state->retrigger!=1.0f))return 0U;
    return 1U;
}
uint8_t vca_control_state_restore(uint8_t entity,const vca_control_state_t *state)
{
    if(entity>=BRICK_ENTITY_CAPACITY||!vca_control_state_validate(state))return 0U;
    static const param_id_t ids[6U]={PARAM_VCA_ATTACK,PARAM_VCA_DECAY,
        PARAM_VCA_SUSTAIN,PARAM_VCA_RELEASE,PARAM_FILTER_MODE,
        PARAM_ENV_RETRIG_VCA};
    vca_control_state_t canonical_state=*state;
    float *const values=&canonical_state.attack;
    for(uint8_t i=0U;i<6U;++i){param_registry_prepared_value_t prepared;
        if(!param_registry_prepare_value(ids[i],values[i],&prepared))return 0U;
        values[i]=prepared.value;}
    live_parameter_audio_bulk_t bulk={.capture_tick=0U,
        .count=6U};
    for(uint8_t i=0U;i<6U;++i)bulk.item[i]=
        (live_parameter_audio_bulk_item_t){.parameter_id=(uint16_t)ids[i],
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=entity,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float(values[i])};
    if(!live_parameter_audio_publication_submit_bulk_now(&bulk))return 0U;
    g_vca_control[entity]=canonical_state;
    return 1U;
}
