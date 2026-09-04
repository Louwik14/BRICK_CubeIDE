#include "Param/param_global_control.h"

#include "Param/param_registry.h"
#include <math.h>
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/live_parameter_migration.h"

typedef enum
{
    GLOBAL_SEND0_FX,
    GLOBAL_SEND1_FX,
    GLOBAL_BUS_COMP_THRESHOLD,
    GLOBAL_BUS_COMP_RATIO,
    GLOBAL_BUS_COMP_ATTACK,
    GLOBAL_BUS_COMP_RELEASE,
    GLOBAL_BUS_COMP_MAKEUP,
    GLOBAL_BUS_COMP_AUTO_MAKEUP,
    GLOBAL_BUS_COMP_DRYWET,
    GLOBAL_BUS_COMP_HPF,
    GLOBAL_EQ_LOW,
    GLOBAL_EQ_MID,
    GLOBAL_EQ_HIGH,
    GLOBAL_SAT_TONE,
    GLOBAL_SAT_BIAS,
    GLOBAL_SAT_DRIVE,
    GLOBAL_SAT_MIX,
    GLOBAL_REVERB_WET,
    GLOBAL_REVERB_ROOM,
    GLOBAL_REVERB_DAMPING,
    GLOBAL_REVERB_WIDTH,
    GLOBAL_REVERB_HPF,
    GLOBAL_REVERB_LPF,
    GLOBAL_REVERB_DELAYS,
    GLOBAL_DELAY_TYPE,
    GLOBAL_DELAY_TIME,
    GLOBAL_DELAY_PINGPONG,
    GLOBAL_DELAY_MODE,
    GLOBAL_DELAY_TIME_R,
    GLOBAL_DELAY_WIDTH,
    GLOBAL_DELAY_FEEDBACK,
    GLOBAL_DELAY_SPECTRAL_POSITION,
    GLOBAL_DELAY_SPECTRAL_WIDTH,
    GLOBAL_DELAY_FBW,
    GLOBAL_DELAY_MOD,
    GLOBAL_DELAY_MOD_RATE,
    GLOBAL_DELAY_REV,
    GLOBAL_DELAY_VOL,
    GLOBAL_MODFX_MODEL,
    GLOBAL_MODFX_RATE,
    GLOBAL_MODFX_DEPTH,
    GLOBAL_MODFX_FEEDBACK,
    GLOBAL_MODFX_OFFSET,
    GLOBAL_MODFX_RATE_B,
    GLOBAL_MODFX_DELAY_B,
    GLOBAL_MODFX_DEPTH_B,
    GLOBAL_MODFX_WIDTH,
    GLOBAL_COMP_MODEL,
    GLOBAL_COMP_DETECT,
    GLOBAL_COMP_KNEE,
    GLOBAL_COMP_DELUGE_SAT,
    GLOBAL_MASTER_GAIN,
    GLOBAL_POST_GAIN,
    GLOBAL_OUTPUT_COMP,
    GLOBAL_CONTROL_VALUE_COUNT
} param_global_slot_t;

static float g_global_values[GLOBAL_CONTROL_VALUE_COUNT];

static const param_id_t g_global_param_ids[GLOBAL_CONTROL_VALUE_COUNT] = {
    [GLOBAL_SEND0_FX] = PARAM_MIX_SEND0_FX,
    [GLOBAL_SEND1_FX] = PARAM_MIX_SEND1_FX,
    [GLOBAL_BUS_COMP_THRESHOLD] = PARAM_BUS_COMP_THRESHOLD_DB,
    [GLOBAL_BUS_COMP_RATIO] = PARAM_BUS_COMP_RATIO,
    [GLOBAL_BUS_COMP_ATTACK] = PARAM_BUS_COMP_ATTACK_INDEX,
    [GLOBAL_BUS_COMP_RELEASE] = PARAM_BUS_COMP_RELEASE_INDEX,
    [GLOBAL_BUS_COMP_MAKEUP] = PARAM_BUS_COMP_MAKEUP_DB,
    [GLOBAL_BUS_COMP_AUTO_MAKEUP] = PARAM_BUS_COMP_AUTO_MAKEUP,
    [GLOBAL_BUS_COMP_DRYWET] = PARAM_BUS_COMP_DRYWET,
    [GLOBAL_BUS_COMP_HPF] = PARAM_BUS_COMP_HPF_HZ,
    [GLOBAL_EQ_LOW] = PARAM_EQ_LOW_DB,
    [GLOBAL_EQ_MID] = PARAM_EQ_MID_DB,
    [GLOBAL_EQ_HIGH] = PARAM_EQ_HIGH_DB,
    [GLOBAL_SAT_TONE] = PARAM_SAT_TONE,
    [GLOBAL_SAT_BIAS] = PARAM_SAT_BIAS,
    [GLOBAL_SAT_DRIVE] = PARAM_SAT_DRIVE,
    [GLOBAL_SAT_MIX] = PARAM_SAT_MIX,
    [GLOBAL_REVERB_WET] = PARAM_MIX_REVERB_WET,
    [GLOBAL_REVERB_ROOM] = PARAM_MIX_REVERB_ROOM_SIZE,
    [GLOBAL_REVERB_DAMPING] = PARAM_MIX_REVERB_DAMPING,
    [GLOBAL_REVERB_WIDTH] = PARAM_MIX_REVERB_WIDTH,
    [GLOBAL_REVERB_HPF] = PARAM_MIX_REVERB_HPF,
    [GLOBAL_REVERB_LPF] = PARAM_MIX_REVERB_LPF,
    [GLOBAL_REVERB_DELAYS] = PARAM_MIX_REVERB_DELAYS,
    [GLOBAL_DELAY_TYPE] = PARAM_MIX_DELAY_TYPE,
    [GLOBAL_DELAY_TIME] = PARAM_MIX_DELAY_TIME,
    [GLOBAL_DELAY_PINGPONG] = PARAM_MIX_DELAY_PINGPONG,
    [GLOBAL_DELAY_MODE] = PARAM_MIX_DELAY_MODE,
    [GLOBAL_DELAY_TIME_R] = PARAM_MIX_DELAY_TIME_R,
    [GLOBAL_DELAY_WIDTH] = PARAM_MIX_DELAY_WIDTH,
    [GLOBAL_DELAY_FEEDBACK] = PARAM_MIX_DELAY_FEEDBACK,
    [GLOBAL_DELAY_SPECTRAL_POSITION] = PARAM_MIX_DELAY_SPECTRAL_POSITION,
    [GLOBAL_DELAY_SPECTRAL_WIDTH] = PARAM_MIX_DELAY_SPECTRAL_WIDTH,
    [GLOBAL_DELAY_FBW] = PARAM_MIX_DELAY_FBW,
    [GLOBAL_DELAY_MOD] = PARAM_MIX_DELAY_MOD,
    [GLOBAL_DELAY_MOD_RATE] = PARAM_MIX_DELAY_MOD_RATE,
    [GLOBAL_DELAY_REV] = PARAM_MIX_DELAY_REV,
    [GLOBAL_DELAY_VOL] = PARAM_MIX_DELAY_VOL,
    [GLOBAL_MODFX_MODEL] = PARAM_MODFX_MODEL,
    [GLOBAL_MODFX_RATE] = PARAM_MODFX_RATE,
    [GLOBAL_MODFX_DEPTH] = PARAM_MODFX_DEPTH,
    [GLOBAL_MODFX_FEEDBACK] = PARAM_MODFX_FEEDBACK,
    [GLOBAL_MODFX_OFFSET] = PARAM_MODFX_OFFSET,
    [GLOBAL_MODFX_RATE_B] = PARAM_MODFX_RATE_B,
    [GLOBAL_MODFX_DELAY_B] = PARAM_MODFX_DELAY_B,
    [GLOBAL_MODFX_DEPTH_B] = PARAM_MODFX_DEPTH_B,
    [GLOBAL_MODFX_WIDTH] = PARAM_MODFX_WIDTH,
    [GLOBAL_COMP_MODEL] = PARAM_COMP_MODEL,
    [GLOBAL_COMP_DETECT] = PARAM_COMP_DETECT,
    [GLOBAL_COMP_KNEE] = PARAM_COMP_KNEE_DB,
    [GLOBAL_COMP_DELUGE_SAT] = PARAM_COMP_DELUGE_SAT,
    [GLOBAL_MASTER_GAIN] = PARAM_MASTER_GAIN,
    [GLOBAL_POST_GAIN] = PARAM_POST_GAIN,
    [GLOBAL_OUTPUT_COMP] = PARAM_OUTPUT_COMP,
};

static uint8_t global_slot(param_id_t id, param_global_slot_t *out)
{
    param_global_slot_t slot;
    switch (id)
    {
        case PARAM_MIX_SEND0_FX: slot = GLOBAL_SEND0_FX; break;
        case PARAM_MIX_SEND1_FX: slot = GLOBAL_SEND1_FX; break;
        case PARAM_BUS_COMP_THRESHOLD_DB: slot = GLOBAL_BUS_COMP_THRESHOLD; break;
        case PARAM_BUS_COMP_RATIO: slot = GLOBAL_BUS_COMP_RATIO; break;
        case PARAM_BUS_COMP_ATTACK_INDEX: slot = GLOBAL_BUS_COMP_ATTACK; break;
        case PARAM_BUS_COMP_RELEASE_INDEX: slot = GLOBAL_BUS_COMP_RELEASE; break;
        case PARAM_BUS_COMP_MAKEUP_DB: slot = GLOBAL_BUS_COMP_MAKEUP; break;
        case PARAM_BUS_COMP_AUTO_MAKEUP: slot = GLOBAL_BUS_COMP_AUTO_MAKEUP; break;
        case PARAM_BUS_COMP_DRYWET: slot = GLOBAL_BUS_COMP_DRYWET; break;
        case PARAM_BUS_COMP_HPF_HZ: slot = GLOBAL_BUS_COMP_HPF; break;
        case PARAM_EQ_LOW_DB: slot = GLOBAL_EQ_LOW; break;
        case PARAM_EQ_MID_DB: slot = GLOBAL_EQ_MID; break;
        case PARAM_EQ_HIGH_DB: slot = GLOBAL_EQ_HIGH; break;
        case PARAM_SAT_TONE: slot = GLOBAL_SAT_TONE; break;
        case PARAM_SAT_BIAS: slot = GLOBAL_SAT_BIAS; break;
        case PARAM_SAT_DRIVE: slot = GLOBAL_SAT_DRIVE; break;
        case PARAM_SAT_MIX: slot = GLOBAL_SAT_MIX; break;
        case PARAM_MIX_REVERB_WET: slot = GLOBAL_REVERB_WET; break;
        case PARAM_MIX_REVERB_ROOM_SIZE: slot = GLOBAL_REVERB_ROOM; break;
        case PARAM_MIX_REVERB_DAMPING: slot = GLOBAL_REVERB_DAMPING; break;
        case PARAM_MIX_REVERB_WIDTH: slot = GLOBAL_REVERB_WIDTH; break;
        case PARAM_MIX_REVERB_HPF: slot = GLOBAL_REVERB_HPF; break;
        case PARAM_MIX_REVERB_LPF: slot = GLOBAL_REVERB_LPF; break;
        case PARAM_MIX_REVERB_DELAYS: slot = GLOBAL_REVERB_DELAYS; break;
        case PARAM_MIX_DELAY_TYPE: slot = GLOBAL_DELAY_TYPE; break;
        case PARAM_MIX_DELAY_TIME: slot = GLOBAL_DELAY_TIME; break;
        case PARAM_MIX_DELAY_PINGPONG: slot = GLOBAL_DELAY_PINGPONG; break;
        case PARAM_MIX_DELAY_MODE: slot = GLOBAL_DELAY_MODE; break;
        case PARAM_MIX_DELAY_TIME_R: slot = GLOBAL_DELAY_TIME_R; break;
        case PARAM_MIX_DELAY_WIDTH: slot = GLOBAL_DELAY_WIDTH; break;
        case PARAM_MIX_DELAY_FEEDBACK: slot = GLOBAL_DELAY_FEEDBACK; break;
        case PARAM_MIX_DELAY_SPECTRAL_POSITION: slot = GLOBAL_DELAY_SPECTRAL_POSITION; break;
        case PARAM_MIX_DELAY_SPECTRAL_WIDTH: slot = GLOBAL_DELAY_SPECTRAL_WIDTH; break;
        case PARAM_MIX_DELAY_FBW: slot = GLOBAL_DELAY_FBW; break;
        case PARAM_MIX_DELAY_MOD: slot = GLOBAL_DELAY_MOD; break;
        case PARAM_MIX_DELAY_MOD_RATE: slot = GLOBAL_DELAY_MOD_RATE; break;
        case PARAM_MIX_DELAY_REV: slot = GLOBAL_DELAY_REV; break;
        case PARAM_MIX_DELAY_VOL: slot = GLOBAL_DELAY_VOL; break;
        case PARAM_MODFX_MODEL: slot = GLOBAL_MODFX_MODEL; break;
        case PARAM_MODFX_RATE: slot = GLOBAL_MODFX_RATE; break;
        case PARAM_MODFX_DEPTH: slot = GLOBAL_MODFX_DEPTH; break;
        case PARAM_MODFX_FEEDBACK: slot = GLOBAL_MODFX_FEEDBACK; break;
        case PARAM_MODFX_OFFSET: slot = GLOBAL_MODFX_OFFSET; break;
        case PARAM_MODFX_RATE_B: slot = GLOBAL_MODFX_RATE_B; break;
        case PARAM_MODFX_DELAY_B: slot = GLOBAL_MODFX_DELAY_B; break;
        case PARAM_MODFX_DEPTH_B: slot = GLOBAL_MODFX_DEPTH_B; break;
        case PARAM_MODFX_WIDTH: slot = GLOBAL_MODFX_WIDTH; break;
        case PARAM_COMP_MODEL: slot = GLOBAL_COMP_MODEL; break;
        case PARAM_COMP_DETECT: slot = GLOBAL_COMP_DETECT; break;
        case PARAM_COMP_KNEE_DB: slot = GLOBAL_COMP_KNEE; break;
        case PARAM_COMP_DELUGE_SAT: slot = GLOBAL_COMP_DELUGE_SAT; break;
        case PARAM_MASTER_GAIN: slot = GLOBAL_MASTER_GAIN; break;
        case PARAM_POST_GAIN: slot = GLOBAL_POST_GAIN; break;
        case PARAM_OUTPUT_COMP: slot = GLOBAL_OUTPUT_COMP; break;
        default: return 0U;
    }
    if (out != 0) *out = slot;
    return 1U;
}

void param_global_control_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)GLOBAL_CONTROL_VALUE_COUNT; ++i)
    {
        const param_id_t id = g_global_param_ids[i];
        g_global_values[i] = param_registry[id].default_value;
    }
}

uint8_t param_global_control_get(param_id_t id, float *out_value)
{
    param_global_slot_t slot;
    if ((out_value == 0) || (global_slot(id, &slot) == 0U)) return 0U;
    *out_value = g_global_values[slot];
    return 1U;
}

uint8_t param_global_control_set(param_id_t id, float value)
{
    param_global_slot_t slot;
    if (global_slot(id, &slot) == 0U) return 0U;
    g_global_values[slot] = value;
    return 1U;
}

uint8_t param_global_control_capture(param_global_control_state_t *out_state)
{
    if (out_state == 0) return 0U;
    float *const out = (float *)out_state;
    for (uint8_t i=0U;i<(uint8_t)GLOBAL_CONTROL_VALUE_COUNT;++i) out[i]=g_global_values[i];
    return 1U;
}

uint8_t param_global_control_restore(const param_global_control_state_t *state)
{
    if (state == 0) return 0U;
    param_global_control_state_t canonical_state = *state;
    float *const canonical = (float *)&canonical_state;
    for (uint8_t i = 0U; i < (uint8_t)GLOBAL_CONTROL_VALUE_COUNT; ++i)
    {
        const param_global_slot_t slot = (param_global_slot_t)i;
        const param_id_t id = g_global_param_ids[i];
        param_registry_prepared_value_t prepared;
        if (!isfinite(canonical[slot])
                || (param_registry_prepare_value(
                    id, canonical[slot], &prepared) == 0U)) return 0U;
        canonical[slot] = prepared.value;
    }
    const uint8_t modfx_model = (uint8_t)(
        canonical[GLOBAL_MODFX_MODEL] + 0.5f);
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = 0U,
        .count = 0U
    };
    for (uint8_t i = 0U; i < (uint8_t)GLOBAL_CONTROL_VALUE_COUNT; ++i)
    {
        const param_global_slot_t slot = (param_global_slot_t)i;
        const param_id_t id = g_global_param_ids[i];
        if (((live_parameter_is_audio_owned(id) == 0U)
                    && (id != PARAM_MASTER_GAIN))) continue;
        float command_value = canonical[slot];
        if (param_registry_prepare_global_audio_command(
                id, canonical[slot], modfx_model, &command_value) == 0U)
            return 0U;
        bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
            .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_GLOBAL,
            .track = 0U,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
            .value = live_parameter_event_encode_float(command_value)
        };
    }
    if ((bulk.count != 0U)
            && !live_parameter_audio_publication_submit_bulk_now(&bulk)) return 0U;
    for (uint8_t i=0U;i<(uint8_t)GLOBAL_CONTROL_VALUE_COUNT;++i)
        g_global_values[i]=canonical[i];
    return 1U;
}

_Static_assert(sizeof(param_global_control_state_t)
               == sizeof(float) * GLOBAL_CONTROL_VALUE_COUNT,
               "global control persistence layout mismatch");
