#include "Audio/tb3_synth.h"

#include <stddef.h>
#include <string.h>

namespace
{
constexpr uint8_t TB3_SYNTH_MAX_INSTANCES = 8U;
constexpr uint8_t TB3_PARAM_COUNT = 8U;

struct tb3_synth_instance_t
{
    uint8_t note_active;
    uint8_t current_note;
    uint8_t velocity;
    float params[TB3_PARAM_COUNT];
};

static tb3_synth_instance_t g_tb3_instances[TB3_SYNTH_MAX_INSTANCES];
static float g_tb3_sample_rate = 48000.0f;

static uint8_t tb3_synth_instance_valid(uint8_t instance_id)
{
    return (instance_id < TB3_SYNTH_MAX_INSTANCES) ? 1U : 0U;
}

static uint8_t tb3_synth_param_index(param_id_t param_id)
{
    switch (param_id)
    {
        case PARAM_TB3_WAVEFORM:
            return 0U;
        case PARAM_TB3_CUTOFF:
            return 1U;
        case PARAM_TB3_RESONANCE:
            return 2U;
        case PARAM_TB3_ENV_MOD:
            return 3U;
        case PARAM_TB3_DECAY:
            return 4U;
        case PARAM_TB3_ACCENT:
            return 5U;
        case PARAM_TB3_VOLUME:
            return 6U;
        case PARAM_TB3_SLIDE_TIME:
            return 7U;
        default:
            return TB3_PARAM_COUNT;
    }
}
}

extern "C" {

void tb3_synth_init(float sample_rate)
{
    g_tb3_sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    (void)g_tb3_sample_rate;
    (void)memset(g_tb3_instances, 0, sizeof(g_tb3_instances));
}

uint8_t tb3_synth_instance_count(void)
{
    return TB3_SYNTH_MAX_INSTANCES;
}

void tb3_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_tb3_instances[instance_id].note_active = 1U;
    g_tb3_instances[instance_id].current_note = midi_note;
    g_tb3_instances[instance_id].velocity = velocity;
}

void tb3_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    if (g_tb3_instances[instance_id].current_note != midi_note)
    {
        return;
    }

    g_tb3_instances[instance_id].note_active = 0U;
}

void tb3_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_tb3_instances[instance_id].note_active = 0U;
    g_tb3_instances[instance_id].velocity = 0U;
}

void tb3_synth_all_notes_off_all(void)
{
    for (uint8_t instance = 0U; instance < TB3_SYNTH_MAX_INSTANCES; ++instance)
    {
        tb3_synth_all_notes_off_for_instance(instance);
    }
}

void tb3_synth_set_param_for_instance(uint8_t instance_id, param_id_t param_id, float value)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    const uint8_t param_index = tb3_synth_param_index(param_id);
    if (param_index >= TB3_PARAM_COUNT)
    {
        return;
    }

    g_tb3_instances[instance_id].params[param_index] = value;
}

void tb3_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    (void)instance_id;

    if (mono_out == NULL)
    {
        return;
    }

    (void)memset(mono_out, 0, sizeof(float) * frames);
}

} // extern "C"
