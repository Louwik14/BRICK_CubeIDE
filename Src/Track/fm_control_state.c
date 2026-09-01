#include "Track/fm_control_state.h"

#include <math.h>
#include <string.h>

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "Platform/memory_layout.h"

CONTROL_STATE_SDRAM static fm_control_state_t
    g_fm_control_state[BRICK_ENTITY_CAPACITY];

static float fm_clampf(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float fm_operator_frequency(const track_tone_fm_operator_base_t *op)
{
    if (op->mode == 0U)
    {
        const float coarse = (op->coarse == 0U) ? 0.5f : (float)(op->coarse & 31U);
        return coarse * (1.0f + 0.01f * (float)op->fine);
    }
    return 0.01f * (float)(((uint16_t)(op->coarse & 3U) * 100U) + op->fine);
}

static void fm_operator_set_frequency(track_tone_fm_operator_base_t *op,
                                      float value)
{
    value = fm_clampf(value, 0.25f, 16.0f);
    if (op->mode != 0U)
    {
        uint16_t code = (uint16_t)(value * 100.0f + 0.5f);
        if (code > 399U) code = 399U;
        op->coarse = (uint8_t)(code / 100U);
        op->fine = (uint8_t)(code % 100U);
        return;
    }
    float best_error = 1000.0f;
    uint8_t best_coarse = 0U;
    uint8_t best_fine = 0U;
    for (uint8_t coarse = 0U; coarse < 32U; ++coarse)
    {
        const float base = (coarse == 0U) ? 0.5f : (float)coarse;
        int16_t fine = (int16_t)(((value / base) - 1.0f) * 100.0f + 0.5f);
        if (fine < 0) fine = 0;
        if (fine > 99) fine = 99;
        const float represented = base * (1.0f + 0.01f * (float)fine);
        const float error = (represented > value)
            ? represented - value : value - represented;
        if (error < best_error)
        {
            best_error = error;
            best_coarse = coarse;
            best_fine = (uint8_t)fine;
        }
    }
    op->coarse = best_coarse;
    op->fine = best_fine;
}

static void fm_control_state_make_default(fm_control_state_t *state)
{
    static const uint8_t s_default_output_level[TRACK_TONE_FM_OPERATOR_COUNT] = {
        99U, 82U, 76U, 70U, 64U, 58U
    };

    memset(state, 0, sizeof(*state));
    state->base.transpose = 24U;
    state->base.key_sync = 1U;
    for (uint8_t op = 0U; op < TRACK_TONE_FM_OPERATOR_COUNT; ++op)
    {
        track_tone_fm_operator_base_t *const base = &state->base.operators[op];
        base->rates[0] = 99U;
        base->rates[1] = 92U;
        base->rates[2] = 80U;
        base->rates[3] = 72U;
        base->levels[0] = 99U;
        base->levels[1] = 92U;
        base->levels[2] = 80U;
        base->breakpoint = 39U;
        base->right_curve = 3U;
        base->coarse = (uint8_t)(op + 1U);
        base->output_level = s_default_output_level[op];
        base->enabled = 1U;
    }
    for (uint8_t i = 0U; i < 4U; ++i) state->base.pitch_levels[i] = 49U;
    state->macros.play_vel = 1.0f;
    state->macros.pitch_time = 0.5f;
}

void fm_control_state_init(void)
{
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        fm_control_state_make_default(&g_fm_control_state[entity]);
}

uint8_t fm_control_state_reset(uint8_t entity)
{
    fm_control_state_t state;
    fm_control_state_make_default(&state);
    return fm_control_state_restore(entity, &state);
}

uint8_t fm_control_state_get(uint8_t entity, fm_control_state_t *out_state)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (out_state == NULL)) return 0U;
    *out_state = g_fm_control_state[entity];
    return 1U;
}

static uint8_t fm_control_state_get_public_param_from(
    const fm_control_state_t *state, param_id_t id, float *out_value);

static uint8_t fm_control_state_publish_value(
    uint8_t entity, const fm_control_state_t *state)
{
    if ((entity >= BRICK_ENTITY_CAPACITY) || (state == NULL)) return 0U;
    control_audio_command_t commands[160U];
    uint16_t count = 0U;
    for (param_id_t id = PARAM_FM_RATIO;
         id <= PARAM_FM_ENV_RELEASE; ++id)
    {
        float value;
        if (fm_control_state_get_public_param_from(state, id, &value) == 0U) continue;
        if (count >= (uint16_t)(sizeof(commands) / sizeof(commands[0]))) return 0U;
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        commands[count++] = (control_audio_command_t){
            .value = bits, .id = (uint16_t)id,
            .entity = entity,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    for (param_id_t id = PARAM_FM_PLAY_VEL;
         id <= PARAM_FM_PLAY_PITCH_TIME; ++id)
    {
        float value;
        if (fm_control_state_get_public_param_from(state, id, &value) == 0U) continue;
        if (count >= (uint16_t)(sizeof(commands) / sizeof(commands[0]))) return 0U;
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        commands[count++] = (control_audio_command_t){
            .value = bits, .id = (uint16_t)id,
            .entity = entity,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    for (param_id_t id = PARAM_FM_OPERATOR_FIRST;
         id <= PARAM_FM_OPERATOR_LAST; ++id)
    {
        float value;
        if (fm_control_state_get_public_param_from(state, id, &value) == 0U) continue;
        if (count >= (uint16_t)(sizeof(commands) / sizeof(commands[0]))) return 0U;
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        commands[count++] = (control_audio_command_t){
            .value = bits, .id = (uint16_t)id,
            .entity = entity,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    const uint8_t *const base = (const uint8_t *)&state->base;
    const uint16_t words = (uint16_t)((sizeof(state->base) + 3U) / 4U);
    for (uint16_t word = 0U; word < words; ++word)
    {
        uint32_t bits = 0U;
        const uint16_t offset = (uint16_t)(word * 4U);
        uint16_t bytes = (uint16_t)(sizeof(state->base) - offset);
        if (bytes > 4U) bytes = 4U;
        memcpy(&bits, &base[offset], bytes);
        commands[count++] = (control_audio_command_t){
            .value = bits,
            .id = (uint16_t)(CONTROL_AUDIO_FM_BASE_WORD_FIRST + word),
            .entity = entity,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    return control_rt_publish_batch_now(commands, count);
}

uint8_t fm_control_state_validate(const fm_control_state_t *state)
{
    if (state == NULL) return 0U;
    for (uint8_t op = 0U; op < TRACK_TONE_FM_OPERATOR_COUNT; ++op)
    {
        const track_tone_fm_operator_base_t *const v = &state->base.operators[op];
        for (uint8_t i = 0U; i < 4U; ++i)
            if ((v->rates[i] > 99U) || (v->levels[i] > 99U)) return 0U;
        if ((v->breakpoint > 99U) || (v->left_depth > 99U)
                || (v->right_depth > 99U) || (v->left_curve > 3U)
                || (v->right_curve > 3U) || (v->rate_scaling > 7U)
                || (v->output_level > 99U) || (v->mode > 1U)
                || (v->coarse > 31U) || (v->fine > 99U)
                || (v->detune < -7) || (v->detune > 7)
                || (v->velocity_sensitivity > 7U) || (v->enabled > 1U))
            return 0U;
    }
    for (uint8_t i = 0U; i < 4U; ++i)
        if ((state->base.pitch_rates[i] > 99U)
                || (state->base.pitch_levels[i] > 99U)) return 0U;
    if ((state->base.transpose > 48U) || (state->base.algorithm > 31U)
            || (state->base.feedback > 7U) || (state->base.key_sync > 1U))
        return 0U;
    const float *const macro = &state->macros.ratio;
    for (uint8_t i = 0U; i < 13U; ++i) if (!isfinite(macro[i])) return 0U;
    return 1U;
}

uint8_t fm_control_state_restore(uint8_t entity,
                                 const fm_control_state_t *state)
{
    if ((entity >= BRICK_ENTITY_CAPACITY)
            || (fm_control_state_validate(state) == 0U)) return 0U;
    if (fm_control_state_publish_value(entity, state) == 0U) return 0U;
    g_fm_control_state[entity] = *state;
    return 1U;
}

uint8_t fm_control_state_set_public_param(uint8_t entity,
                                          param_id_t id,
                                          float value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    fm_control_state_t *const state = &g_fm_control_state[entity];
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        const uint8_t op = (uint8_t)(offset / PARAM_FM_OPERATOR_PARAM_COUNT);
        const uint8_t field = (uint8_t)(offset % PARAM_FM_OPERATOR_PARAM_COUNT);
        track_tone_fm_operator_base_t *const base = &state->base.operators[op];
        switch (field)
        {
            case 0U: base->output_level = (uint8_t)(fm_clampf(value, 0.0f, 99.0f) + 0.5f); break;
            case 1U: fm_operator_set_frequency(base, value); break;
            case 2U: base->detune = (int8_t)(value + ((value < 0.0f) ? -0.5f : 0.5f)); break;
            case 3U: base->rates[0] = (uint8_t)(value + 0.5f); break;
            case 4U: base->rates[1] = (uint8_t)(value + 0.5f); break;
            case 5U: base->levels[2] = (uint8_t)(value + 0.5f); break;
            case 6U: base->rates[3] = (uint8_t)(value + 0.5f); break;
            case 7U: base->enabled = (value >= 0.5f) ? 1U : 0U; break;
            case 8U: base->mode = (value >= 0.5f) ? 1U : 0U; break;
            case 9U: base->velocity_sensitivity = (uint8_t)(fm_clampf(value, 0.0f, 1.0f) * 7.0f + 0.5f); break;
            case 10U:
                base->left_depth = (uint8_t)(fm_clampf(value, 0.0f, 1.0f) * 99.0f + 0.5f);
                base->right_depth = base->left_depth;
                break;
            default: break;
        }
        return 1U;
    }
    switch (id)
    {
        case PARAM_FM_ALGORITHM: state->base.algorithm = (uint8_t)(value + 0.5f); break;
        case PARAM_FM_FEEDBACK: state->base.feedback = (uint8_t)(value + 0.5f); break;
        case PARAM_FM_SYNC: state->base.key_sync = (value >= 0.5f) ? 1U : 0U; break;
        case PARAM_FM_TRANSPOSE: state->base.transpose = (uint8_t)(value + 24.5f); break;
        case PARAM_FM_PITCH_R1: case PARAM_FM_PITCH_R2:
        case PARAM_FM_PITCH_R3: case PARAM_FM_PITCH_R4:
            state->base.pitch_rates[id - PARAM_FM_PITCH_R1] = (uint8_t)(value + 0.5f); break;
        case PARAM_FM_PITCH_L1: case PARAM_FM_PITCH_L2:
        case PARAM_FM_PITCH_L3: case PARAM_FM_PITCH_L4:
            state->base.pitch_levels[id - PARAM_FM_PITCH_L1] = (uint8_t)(value + 49.5f); break;
        case PARAM_FM_RATIO: state->macros.ratio = value; break;
        case PARAM_FM_BRIGHT: state->macros.bright = value; break;
        case PARAM_FM_BODY: state->macros.body = value; break;
        case PARAM_FM_DETAIL: state->macros.detail = value; break;
        case PARAM_FM_METAL: state->macros.metal = value; break;
        case PARAM_FM_ENV_ATTACK: state->macros.env_attack = value; break;
        case PARAM_FM_ENV_DECAY: state->macros.env_decay = value; break;
        case PARAM_FM_ENV_SUSTAIN: state->macros.env_sustain = value; break;
        case PARAM_FM_ENV_RELEASE: state->macros.env_release = value; break;
        case PARAM_FM_PLAY_VEL: state->macros.play_vel = value; break;
        case PARAM_FM_PLAY_KEY: state->macros.play_key = value; break;
        case PARAM_FM_PLAY_PITCH_ENV: state->macros.pitch_env = value; break;
        case PARAM_FM_PLAY_PITCH_TIME: state->macros.pitch_time = value; break;
        default: return 0U;
    }
    return 1U;
}

static uint8_t fm_control_state_get_public_param_from(
    const fm_control_state_t *state, param_id_t id, float *out_value)
{
    if ((state == NULL) || (out_value == NULL)) return 0U;
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        const uint8_t op = (uint8_t)(offset / PARAM_FM_OPERATOR_PARAM_COUNT);
        const uint8_t field = (uint8_t)(offset % PARAM_FM_OPERATOR_PARAM_COUNT);
        const track_tone_fm_operator_base_t *const base = &state->base.operators[op];
        switch (field)
        {
            case 0U: *out_value = (float)base->output_level; break;
            case 1U: *out_value = fm_operator_frequency(base); break;
            case 2U: *out_value = (float)base->detune; break;
            case 3U: *out_value = (float)base->rates[0]; break;
            case 4U: *out_value = (float)base->rates[1]; break;
            case 5U: *out_value = (float)base->levels[2]; break;
            case 6U: *out_value = (float)base->rates[3]; break;
            case 7U: *out_value = (float)base->enabled; break;
            case 8U: *out_value = (float)base->mode; break;
            case 9U: *out_value = (float)base->velocity_sensitivity / 7.0f; break;
            case 10U:
                *out_value = ((float)base->left_depth + (float)base->right_depth)
                    / 198.0f;
                break;
            default: return 0U;
        }
        return 1U;
    }
    switch (id)
    {
        case PARAM_FM_ALGORITHM: *out_value = (float)state->base.algorithm; break;
        case PARAM_FM_FEEDBACK: *out_value = (float)state->base.feedback; break;
        case PARAM_FM_SYNC: *out_value = (float)state->base.key_sync; break;
        case PARAM_FM_TRANSPOSE: *out_value = (float)state->base.transpose - 24.0f; break;
        case PARAM_FM_PITCH_R1: case PARAM_FM_PITCH_R2:
        case PARAM_FM_PITCH_R3: case PARAM_FM_PITCH_R4:
            *out_value = (float)state->base.pitch_rates[id - PARAM_FM_PITCH_R1]; break;
        case PARAM_FM_PITCH_L1: case PARAM_FM_PITCH_L2:
        case PARAM_FM_PITCH_L3: case PARAM_FM_PITCH_L4:
            *out_value = (float)state->base.pitch_levels[id - PARAM_FM_PITCH_L1] - 49.0f; break;
        case PARAM_FM_RATIO: *out_value = state->macros.ratio; break;
        case PARAM_FM_BRIGHT: *out_value = state->macros.bright; break;
        case PARAM_FM_BODY: *out_value = state->macros.body; break;
        case PARAM_FM_DETAIL: *out_value = state->macros.detail; break;
        case PARAM_FM_METAL: *out_value = state->macros.metal; break;
        case PARAM_FM_ENV_ATTACK: *out_value = state->macros.env_attack; break;
        case PARAM_FM_ENV_DECAY: *out_value = state->macros.env_decay; break;
        case PARAM_FM_ENV_SUSTAIN: *out_value = state->macros.env_sustain; break;
        case PARAM_FM_ENV_RELEASE: *out_value = state->macros.env_release; break;
        case PARAM_FM_PLAY_VEL: *out_value = state->macros.play_vel; break;
        case PARAM_FM_PLAY_KEY: *out_value = state->macros.play_key; break;
        case PARAM_FM_PLAY_PITCH_ENV: *out_value = state->macros.pitch_env; break;
        case PARAM_FM_PLAY_PITCH_TIME: *out_value = state->macros.pitch_time; break;
        default: return 0U;
    }
    return 1U;
}

uint8_t fm_control_state_get_public_param(uint8_t entity, param_id_t id,
                                          float *out_value)
{
    return (entity < BRICK_ENTITY_CAPACITY)
        ? fm_control_state_get_public_param_from(
            &g_fm_control_state[entity], id, out_value)
        : 0U;
}
