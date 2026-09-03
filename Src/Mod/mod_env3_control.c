#include "Mod/mod_env3_control.h"

#include <math.h>
#include <stddef.h>

#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Platform/memory_layout.h"
#include "Track/entity_types.h"

CONTROL_STATE_SDRAM static mod_env3_control_state_t
    g_mod_env3_control[BRICK_ENTITY_CAPACITY];

void mod_env3_control_init(void)
{
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        g_mod_env3_control[entity] = (mod_env3_control_state_t){
            .attack = 0.0f,
            .decay = 32.0f,
            .sustain = 127.0f,
            .release = 32.0f,
            .retrigger = 1.0f
        };
    }
}

uint8_t mod_env3_control_reset(uint8_t entity)
{
    const mod_env3_control_state_t state = {
        .decay = 32.0f, .sustain = 127.0f, .release = 32.0f,
        .retrigger = 1.0f
    };
    return mod_env3_control_restore(entity, &state);
}

uint8_t mod_env3_control_get_param(uint8_t entity, param_id_t id,
                                   float *out_value)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_value == NULL)) return 0U;
    const mod_env3_control_state_t *const state = &g_mod_env3_control[entity];
    switch (id)
    {
        case PARAM_ENV3_ATTACK: *out_value = state->attack; return 1U;
        case PARAM_ENV3_DECAY: *out_value = state->decay; return 1U;
        case PARAM_ENV3_SUSTAIN: *out_value = state->sustain; return 1U;
        case PARAM_ENV3_RELEASE: *out_value = state->release; return 1U;
        case PARAM_ENV_RETRIG_MOD: *out_value = state->retrigger; return 1U;
        default: return 0U;
    }
}

uint8_t mod_env3_control_set_param(uint8_t entity, param_id_t id, float value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    mod_env3_control_state_t *const state = &g_mod_env3_control[entity];
    switch (id)
    {
        case PARAM_ENV3_ATTACK: state->attack = value; return 1U;
        case PARAM_ENV3_DECAY: state->decay = value; return 1U;
        case PARAM_ENV3_SUSTAIN: state->sustain = value; return 1U;
        case PARAM_ENV3_RELEASE: state->release = value; return 1U;
        case PARAM_ENV_RETRIG_MOD:
            state->retrigger = (value >= 0.5f) ? 1.0f : 0.0f;
            return 1U;
        default: return 0U;
    }
}

uint8_t mod_env3_control_capture(uint8_t entity,mod_env3_control_state_t*out_state)
{if(entity>=BRICK_ENTITY_CAPACITY||out_state==NULL)return 0U;*out_state=g_mod_env3_control[entity];return 1U;}
uint8_t mod_env3_control_prepare(const mod_env3_control_state_t*state,
                                 mod_env3_control_state_t*out)
{
    if(state==NULL||out==NULL)return 0U;
    const float input[5U]={state->attack,state->decay,state->sustain,
        state->release,state->retrigger};
    float *const values=&out->attack;
    for(uint8_t i=0U;i<5U;++i){if(!isfinite(input[i]))return 0U;
        values[i]=(i==4U)?((input[i]>=0.5f)?1.0f:0.0f)
            :((input[i]<0.0f)?0.0f:((input[i]>127.0f)?127.0f:input[i]));}
    return 1U;
}
uint8_t mod_env3_control_restore(uint8_t entity,const mod_env3_control_state_t*state)
{
    if(entity>=BRICK_ENTITY_CAPACITY||state==NULL)return 0U;
    const param_id_t ids[5U]={PARAM_ENV3_ATTACK,PARAM_ENV3_DECAY,
        PARAM_ENV3_SUSTAIN,PARAM_ENV3_RELEASE,PARAM_ENV_RETRIG_MOD};
    mod_env3_control_state_t canonical;
    if(!mod_env3_control_prepare(state,&canonical))return 0U;
    float *const values=&canonical.attack;
    live_parameter_audio_bulk_t bulk={.capture_tick=live_clock_capture_tick(),
        .count=0U};
    for(uint8_t i=0U;i<5U;++i)bulk.item[bulk.count++]=
        (live_parameter_audio_bulk_item_t){.parameter_id=(uint16_t)ids[i],
        .scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,.track=entity,
        .slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float(values[i])};
    if(!live_parameter_audio_publication_submit_bulk(&bulk))return 0U;
    g_mod_env3_control[entity]=canonical;
    return 1U;
}
