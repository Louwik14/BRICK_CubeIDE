#include "Track/audio_fx_control_state.h"

#include <string.h>
#include <math.h>
#include "IPC/control_audio_command.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Track/track_runtime.h"
#include "Track/polyphony_control.h"

typedef struct
{
    audio_fx_control_config_t config;
    uint8_t model_a;
    float p1_a;
    float p2_a;
    float p3_a;
    uint8_t model_b;
    float p1_b;
    float p2_b;
    float p3_b;
    float group_level_a;
    float group_level_b;
} audio_fx_control_values_t;

static audio_fx_control_values_t g_audio_fx_control[BRICK_ENTITY_CAPACITY];

static uint8_t audio_fx_control_model_is_valid(uint8_t model)
{
    return (uint8_t)((model == AUDIO_FX_MODEL_OFF)
        || (model == AUDIO_FX_MODEL_LOFI) || (model == AUDIO_FX_MODEL_FOLD)
        || (model == AUDIO_FX_MODEL_DRIVE) || (model == AUDIO_FX_MODEL_POINT)
        || (model == AUDIO_FX_MODEL_SUB) || (model == AUDIO_FX_MODEL_SUB_LIGHT)
        || (model == AUDIO_FX_MODEL_RING) || (model == AUDIO_FX_MODEL_VIBE)
        || (model == AUDIO_FX_MODEL_DRIFT));
}

static uint8_t audio_fx_control_prepare_filter_position_for_voices(
    brick_entity_id_t entity, audio_fx_filter_pos_t requested,
    uint8_t candidate_voice_count,
    audio_fx_filter_pos_t *out_canonical)
{
    track_runtime_descriptor_t descriptor;
    if ((out_canonical == NULL) || (entity >= BRICK_ENTITY_TOP_LEVEL_COUNT)
            || (requested >= AUDIO_FX_FILTER_POS_COUNT)
            || (track_runtime_get_descriptor(entity, &descriptor) == 0U))
        return 0U;
    const uint8_t per_voice = (uint8_t)(
        (descriptor.type == TRACK_RUNTIME_TYPE_GROUP)
        || ((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor.type == TRACK_RUNTIME_TYPE_MULTI))
        || ((descriptor.family == TRACK_RUNTIME_FAMILY_SYNTH)
            && (candidate_voice_count > 1U)));
    *out_canonical = (per_voice != 0U)
        ? AUDIO_FX_FILTER_POS_PRE : requested;
    return 1U;
}

static uint8_t audio_fx_control_prepare_filter_position(
    brick_entity_id_t entity, audio_fx_filter_pos_t requested,
    audio_fx_filter_pos_t *out_canonical)
{
    return audio_fx_control_prepare_filter_position_for_voices(entity,
        requested, polyphony_control_get_voice_count(entity), out_canonical);
}

uint8_t audio_fx_control_state_validate(const audio_fx_control_state_t *state)
{
    if ((state == NULL)
            || (state->config.filter_position >= AUDIO_FX_FILTER_POS_COUNT)
            || (state->config.order >= AUDIO_FX_ORDER_COUNT)
            || (state->config.spatial_mode[0] >= 4U)
            || (state->config.spatial_mode[1] >= 4U)
            || (audio_fx_control_model_is_valid(state->model[0]) == 0U)
            || (audio_fx_control_model_is_valid(state->model[1]) == 0U)
            || ((state->model[0] != AUDIO_FX_MODEL_OFF)
                && (state->model[0] == state->model[1])))
        return 0U;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
        if (!isfinite(state->p1[slot]) || !isfinite(state->p2[slot])
                || !isfinite(state->p3[slot]) || !isfinite(state->group_level[slot])
                || (state->p1[slot] < 0.0f) || (state->p1[slot] > 1.0f)
                || (state->p2[slot] < 0.0f) || (state->p2[slot] > 1.0f)
                || (state->p3[slot] < 0.0f) || (state->p3[slot] > 127.0f)
                || (state->group_level[slot] < 0.0f)
                || (state->group_level[slot] > 1.0f))
            return 0U;
    return 1U;
}

uint8_t audio_fx_control_prepare_context_init(
    brick_entity_id_t entity, audio_fx_control_prepare_context_t *context)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (context == NULL)) return 0U;
    context->model[0] = g_audio_fx_control[entity].model_a;
    context->model[1] = g_audio_fx_control[entity].model_b;
    context->initialized = 1U;
    context->finalized = 0U;
    return 1U;
}

uint8_t audio_fx_control_prepare_project_model(
    brick_entity_id_t entity, param_id_t id, float value,
    audio_fx_control_prepare_context_t *context)
{
    if ((context == NULL) || !isfinite(value)
            || ((id != PARAM_AUDIO_FX_MODEL) && (id != PARAM_AUDIO_FX_B_MODEL)))
        return 0U;
    if ((context->initialized == 0U)
            && (audio_fx_control_prepare_context_init(entity, context) == 0U))
        return 0U;
    const uint8_t model = (uint8_t)(value + 0.5f);
    if (audio_fx_control_model_is_valid(model) == 0U) return 0U;
    context->model[(id == PARAM_AUDIO_FX_MODEL) ? 0U : 1U] = model;
    context->finalized = 0U;
    return 1U;
}

uint8_t audio_fx_control_prepare_finalize(
    brick_entity_id_t entity, audio_fx_control_prepare_context_t *context)
{
    if (context == NULL) return 0U;
    if ((context->initialized == 0U)
            && (audio_fx_control_prepare_context_init(entity, context) == 0U))
        return 0U;
    if ((context->model[0] != AUDIO_FX_MODEL_OFF)
            && (context->model[0] == context->model[1])) return 0U;
    context->finalized = 1U;
    return 1U;
}

uint8_t audio_fx_control_prepare_param(
    brick_entity_id_t entity, param_id_t id, float value,
    audio_fx_control_prepare_context_t *context, float *out_value)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (context == NULL)
            || (out_value == NULL) || !isfinite(value)) return 0U;
    if ((context->initialized == 0U)
            && (audio_fx_control_prepare_context_init(entity, context) == 0U))
        return 0U;
    if ((id == PARAM_AUDIO_FX_MODEL) || (id == PARAM_AUDIO_FX_B_MODEL))
    {
        const uint8_t slot = (id == PARAM_AUDIO_FX_MODEL) ? 0U : 1U;
        const uint8_t model = (uint8_t)(value + 0.5f);
        if (context->finalized == 0U)
        {
            if ((audio_fx_control_prepare_project_model(entity, id, value, context) == 0U)
                    || (audio_fx_control_prepare_finalize(entity, context) == 0U))
                return 0U;
        }
        if ((audio_fx_control_model_is_valid(model) == 0U)
                || (model != context->model[slot])) return 0U;
        *out_value = (float)model;
        return 1U;
    }
    switch (id)
    {
        case PARAM_AUDIO_FX_P1: case PARAM_AUDIO_FX_P2:
        case PARAM_AUDIO_FX_B_P1: case PARAM_AUDIO_FX_B_P2:
            *out_value = value; return 1U;
        case PARAM_AUDIO_FX_P3: case PARAM_AUDIO_FX_B_P3:
            *out_value = (value < 0.0f) ? 0.0f : ((value > 127.0f) ? 127.0f : value);
            return 1U;
        case PARAM_GROUP_FX_A_LEVEL: case PARAM_GROUP_FX_B_LEVEL:
            *out_value = (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
            return 1U;
        default: return 0U;
    }
}

uint8_t audio_fx_control_install_prepared_param(
    brick_entity_id_t entity, param_id_t id, float value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    audio_fx_control_values_t *const state = &g_audio_fx_control[entity];
    switch (id)
    {
        case PARAM_AUDIO_FX_MODEL: state->model_a=(uint8_t)value; return 1U;
        case PARAM_AUDIO_FX_P1: state->p1_a=value; return 1U;
        case PARAM_AUDIO_FX_P2: state->p2_a=value; return 1U;
        case PARAM_AUDIO_FX_P3: state->p3_a=value; return 1U;
        case PARAM_AUDIO_FX_B_MODEL: state->model_b=(uint8_t)value; return 1U;
        case PARAM_AUDIO_FX_B_P1: state->p1_b=value; return 1U;
        case PARAM_AUDIO_FX_B_P2: state->p2_b=value; return 1U;
        case PARAM_AUDIO_FX_B_P3: state->p3_b=value; return 1U;
        case PARAM_GROUP_FX_A_LEVEL: state->group_level_a=value; return 1U;
        case PARAM_GROUP_FX_B_LEVEL: state->group_level_b=value; return 1U;
        default: return 0U;
    }
}

static uint8_t audio_fx_control_publish(brick_entity_id_t entity,
                                        uint16_t command, uint8_t index, float value)
{
    const live_parameter_audio_bulk_t bulk={.capture_tick=live_clock_capture_tick(),
        .count=1U,.item={{
        .parameter_id=command,.scope=LIVE_PARAMETER_EVENT_SCOPE_SLOT,.track=entity,
        .slot=index,.flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value=live_parameter_event_encode_float(value)}}};
    return live_parameter_audio_publication_submit_bulk(&bulk)?1U:0U;
}

void audio_fx_control_state_init(void)
{
    memset(g_audio_fx_control,0,sizeof(g_audio_fx_control));
    for(uint8_t entity=0U;entity<BRICK_ENTITY_CAPACITY;++entity){
        g_audio_fx_control[entity].config.filter_position=AUDIO_FX_FILTER_POS_PRE;
        g_audio_fx_control[entity].config.order=AUDIO_FX_ORDER_A_B;
        g_audio_fx_control[entity].config.spatial_mode[0]=1U;
        g_audio_fx_control[entity].config.spatial_mode[1]=1U;}
}

uint8_t audio_fx_control_state_reset(brick_entity_id_t entity)
{
    audio_fx_control_state_t state = {0};
    state.config.filter_position = AUDIO_FX_FILTER_POS_PRE;
    state.config.order = AUDIO_FX_ORDER_A_B;
    state.config.spatial_mode[0] = 1U;
    state.config.spatial_mode[1] = 1U;
    return audio_fx_control_state_restore(entity, &state);
}

uint8_t audio_fx_control_state_get(brick_entity_id_t entity,audio_fx_control_config_t*out){if(entity>=BRICK_ENTITY_TOP_LEVEL_COUNT||out==0)return 0U;*out=g_audio_fx_control[entity].config;return 1U;}
uint8_t audio_fx_control_set_filter_position(brick_entity_id_t entity,audio_fx_filter_pos_t position){audio_fx_filter_pos_t canonical;if(!audio_fx_control_prepare_filter_position(entity,position,&canonical))return 0U;if(!audio_fx_control_publish(entity,CONTROL_AUDIO_FX_FILTER_POSITION,0U,(float)canonical))return 0U;g_audio_fx_control[entity].config.filter_position=canonical;return 1U;}
uint8_t audio_fx_control_set_order(brick_entity_id_t entity,audio_fx_order_t order){if(entity>=BRICK_ENTITY_TOP_LEVEL_COUNT||order>=AUDIO_FX_ORDER_COUNT)return 0U;if(!audio_fx_control_publish(entity,CONTROL_AUDIO_FX_ORDER,0U,(float)order))return 0U;g_audio_fx_control[entity].config.order=order;return 1U;}
uint8_t audio_fx_control_set_spatial_mode(brick_entity_id_t entity,audio_fx_slot_t slot,uint8_t mode){if(entity>=BRICK_ENTITY_TOP_LEVEL_COUNT||slot>=AUDIO_FX_SLOT_COUNT||mode>=4U)return 0U;if(!audio_fx_control_publish(entity,CONTROL_AUDIO_FX_SPATIAL_MODE,(uint8_t)slot,(float)mode))return 0U;g_audio_fx_control[entity].config.spatial_mode[slot]=mode;return 1U;}

uint8_t audio_fx_control_state_get_param(brick_entity_id_t entity,
                                         param_id_t id, float *out_value)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_value == NULL)) return 0U;
    const audio_fx_control_values_t *const state = &g_audio_fx_control[entity];
    switch (id)
    {
        case PARAM_AUDIO_FX_MODEL: *out_value = (float)state->model_a; return 1U;
        case PARAM_AUDIO_FX_P1: *out_value = state->p1_a; return 1U;
        case PARAM_AUDIO_FX_P2: *out_value = state->p2_a; return 1U;
        case PARAM_AUDIO_FX_P3: *out_value = state->p3_a; return 1U;
        case PARAM_AUDIO_FX_B_MODEL: *out_value = (float)state->model_b; return 1U;
        case PARAM_AUDIO_FX_B_P1: *out_value = state->p1_b; return 1U;
        case PARAM_AUDIO_FX_B_P2: *out_value = state->p2_b; return 1U;
        case PARAM_AUDIO_FX_B_P3: *out_value = state->p3_b; return 1U;
        case PARAM_GROUP_FX_A_LEVEL: *out_value = state->group_level_a; return 1U;
        case PARAM_GROUP_FX_B_LEVEL: *out_value = state->group_level_b; return 1U;
        default: return 0U;
    }
}

uint8_t audio_fx_control_state_capture(brick_entity_id_t entity,
                                       audio_fx_control_state_t *out_state)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_state == NULL)) return 0U;
    const audio_fx_control_values_t *const s = &g_audio_fx_control[entity];
    out_state->config = s->config;
    out_state->model[0]=s->model_a;out_state->model[1]=s->model_b;
    out_state->p1[0]=s->p1_a;out_state->p1[1]=s->p1_b;
    out_state->p2[0]=s->p2_a;out_state->p2[1]=s->p2_b;
    out_state->p3[0]=s->p3_a;out_state->p3[1]=s->p3_b;
    out_state->group_level[0]=s->group_level_a;
    out_state->group_level[1]=s->group_level_b;
    return 1U;
}

uint8_t audio_fx_control_state_prepare_for_polyphony(
    brick_entity_id_t entity, const audio_fx_control_state_t *state,
    uint8_t candidate_voice_count, audio_fx_control_state_t *out_prepared)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_prepared == NULL)
            || (candidate_voice_count < 1U)
            || (audio_fx_control_state_validate(state) == 0U)) return 0U;
    *out_prepared=*state;
    if((entity<BRICK_ENTITY_TOP_LEVEL_COUNT)
            &&!audio_fx_control_prepare_filter_position_for_voices(entity,
                state->config.filter_position,
                candidate_voice_count,
                &out_prepared->config.filter_position))return 0U;
    static const param_id_t ids[10U]={PARAM_AUDIO_FX_MODEL,PARAM_AUDIO_FX_P1,
        PARAM_AUDIO_FX_P2,PARAM_AUDIO_FX_P3,PARAM_AUDIO_FX_B_MODEL,
        PARAM_AUDIO_FX_B_P1,PARAM_AUDIO_FX_B_P2,PARAM_AUDIO_FX_B_P3,
        PARAM_GROUP_FX_A_LEVEL,PARAM_GROUP_FX_B_LEVEL};
    const float values[10U]={(float)state->model[0],state->p1[0],state->p2[0],state->p3[0],
        (float)state->model[1],state->p1[1],state->p2[1],state->p3[1],
        state->group_level[0],state->group_level[1]};
    audio_fx_control_prepare_context_t context={0};
    if((audio_fx_control_prepare_context_init(entity,&context)==0U)
            ||(audio_fx_control_prepare_project_model(entity,ids[0],values[0],&context)==0U)
            ||(audio_fx_control_prepare_project_model(entity,ids[4],values[4],&context)==0U)
            ||(audio_fx_control_prepare_finalize(entity,&context)==0U))return 0U;
    for(uint8_t i=0U;i<10U;++i)
    {
        float canonical=0.0f;
        if(audio_fx_control_prepare_param(entity,ids[i],values[i],
                &context,&canonical)==0U)return 0U;
        switch(i)
        {
            case 0U: out_prepared->model[0]=(uint8_t)canonical; break;
            case 1U: out_prepared->p1[0]=canonical; break;
            case 2U: out_prepared->p2[0]=canonical; break;
            case 3U: out_prepared->p3[0]=canonical; break;
            case 4U: out_prepared->model[1]=(uint8_t)canonical; break;
            case 5U: out_prepared->p1[1]=canonical; break;
            case 6U: out_prepared->p2[1]=canonical; break;
            case 7U: out_prepared->p3[1]=canonical; break;
            case 8U: out_prepared->group_level[0]=canonical; break;
            default: out_prepared->group_level[1]=canonical; break;
        }
    }
    return 1U;
}

uint8_t audio_fx_control_state_bulk_add_prepared(
    brick_entity_id_t entity, const audio_fx_control_state_t *prepared,
    live_parameter_audio_bulk_t *bulk)
{
    if((entity>=BRICK_ENTITY_CAPACITY)||(prepared==NULL)||(bulk==NULL)
            ||(bulk->count>(LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS-14U)))return 0U;
    static const param_id_t ids[10U]={PARAM_AUDIO_FX_MODEL,PARAM_AUDIO_FX_P1,
        PARAM_AUDIO_FX_P2,PARAM_AUDIO_FX_P3,PARAM_AUDIO_FX_B_MODEL,
        PARAM_AUDIO_FX_B_P1,PARAM_AUDIO_FX_B_P2,PARAM_AUDIO_FX_B_P3,
        PARAM_GROUP_FX_A_LEVEL,PARAM_GROUP_FX_B_LEVEL};
    const float values[10U]={(float)prepared->model[0],prepared->p1[0],
        prepared->p2[0],prepared->p3[0],(float)prepared->model[1],
        prepared->p1[1],prepared->p2[1],prepared->p3[1],
        prepared->group_level[0],prepared->group_level[1]};
    if(entity<BRICK_ENTITY_TOP_LEVEL_COUNT)
    {
        const uint16_t commands[4U]={CONTROL_AUDIO_FX_FILTER_POSITION,
            CONTROL_AUDIO_FX_ORDER,CONTROL_AUDIO_FX_SPATIAL_MODE,
            CONTROL_AUDIO_FX_SPATIAL_MODE};
        const uint8_t slots[4U]={0U,0U,AUDIO_FX_SLOT_A,AUDIO_FX_SLOT_B};
        const float config_values[4U]={(float)prepared->config.filter_position,
            (float)prepared->config.order,(float)prepared->config.spatial_mode[0],
            (float)prepared->config.spatial_mode[1]};
        for(uint8_t i=0U;i<4U;++i)bulk->item[bulk->count++]=
            (live_parameter_audio_bulk_item_t){.parameter_id=commands[i],
            .scope=LIVE_PARAMETER_EVENT_SCOPE_SLOT,.track=entity,.slot=slots[i],
            .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
            .value=live_parameter_event_encode_float(config_values[i])};
    }
    for(uint8_t i=0U;i<10U;++i)
    {
        if(track_runtime_get_effective_param_status(entity,ids[i])
                !=TRACK_RUNTIME_PARAM_ALLOWED)continue;
        bulk->item[bulk->count++]=(live_parameter_audio_bulk_item_t){
            .parameter_id=(uint16_t)ids[i],.scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track=entity,.slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
            .value=live_parameter_event_encode_float(values[i])};
    }
    return 1U;
}

uint8_t audio_fx_control_state_install_prepared(
    brick_entity_id_t entity, const audio_fx_control_state_t *prepared)
{
    if((entity>=BRICK_ENTITY_CAPACITY)||(prepared==NULL))return 0U;
    audio_fx_control_values_t *const target=&g_audio_fx_control[entity];
    target->config=prepared->config;
    target->model_a=prepared->model[0];target->model_b=prepared->model[1];
    target->p1_a=prepared->p1[0];target->p2_a=prepared->p2[0];target->p3_a=prepared->p3[0];
    target->p1_b=prepared->p1[1];target->p2_b=prepared->p2[1];target->p3_b=prepared->p3[1];
    target->group_level_a=prepared->group_level[0];
    target->group_level_b=prepared->group_level[1];
    return 1U;
}

uint8_t audio_fx_control_state_restore(brick_entity_id_t entity,
                                       const audio_fx_control_state_t *state)
{
    audio_fx_control_state_t prepared;
    live_parameter_audio_bulk_t bulk={.capture_tick=live_clock_capture_tick(),
        .count=0U};
    if(!audio_fx_control_state_prepare_for_polyphony(entity,state,
            polyphony_control_get_voice_count(entity),&prepared)
            ||!audio_fx_control_state_bulk_add_prepared(entity,&prepared,&bulk)
            ||((bulk.count!=0U)&&!live_parameter_audio_publication_submit_bulk(&bulk)))return 0U;
    return audio_fx_control_state_install_prepared(entity,&prepared);
}
