#include "Audio/brick6_audio_boot.h"

#include "Audio/audio_float.h"
#include "Audio/audio.h"
#include "Audio/drum_synth.h"
#include "Audio/fx_pool.h"
#include "Audio/mixer.h"
#include "Core/brick6_audio_runtime.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_fm_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"

static uint8_t brick6_audio_boot_intent_valid(const brick6_audio_boot_intent_t *intent)
{
    return (uint8_t)((intent != 0)
            && (intent->sample_rate_hz > 0.0f)
            && (intent->fx_slot_count <= BRICK6_AUDIO_BOOT_FX_SLOT_COUNT));
}

uint8_t brick6_audio_boot_apply_early(const brick6_audio_boot_intent_t *intent)
{
    if ((intent == 0)
            || (intent->sample_rate_hz <= 0.0f)
            || (intent->fx_slot_count > BRICK6_AUDIO_BOOT_FX_SLOT_COUNT))
    {
        return 0U;
    }

    mixer_init();
    fx_pool_init();
    for (uint8_t i = 0U; i < intent->fx_slot_count; ++i)
    {
        fx_type_t type;
        switch ((brick6_audio_boot_fx_type_t)intent->fx_slots[i].type)
        {
            case BRICK6_AUDIO_BOOT_FX_EQ3: type = FX_EQ3; break;
            case BRICK6_AUDIO_BOOT_FX_COMP_LAB: type = FX_COMP_LAB; break;
            default: return 0U;
        }
        if (fx_pool_activate_slot(intent->fx_slots[i].slot, type) == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t brick6_audio_boot_apply_output_tracks(const brick6_audio_boot_intent_t *intent)
{
    if (brick6_audio_boot_intent_valid(intent) == 0U) return 0U;
    audio_float_set_postgain(intent->postgain);
    audio_float_set_output_compensation(intent->output_compensation);
    audio_tracks_init();
    return 1U;
}

uint8_t brick6_audio_boot_apply_drum(const brick6_audio_boot_intent_t *intent)
{
    if (brick6_audio_boot_intent_valid(intent) == 0U) return 0U;
    drum_synth_init(intent->sample_rate_hz);
    return 1U;
}

uint8_t brick6_audio_boot_apply_engines(const brick6_audio_boot_intent_t *intent)
{
    if (brick6_audio_boot_intent_valid(intent) == 0U) return 0U;
    brick6_braids_runtime_init();
    brick6_stack_runtime_init();
    brick6_wave_runtime_init();
    mixer_set_master(intent->master_gain);
    brick6_fm_runtime_init();
    brick6_audio_runtime_init();
    return 1U;
}

void brick6_audio_boot_apply_binding_io(void)
{
    audio_boot_init_binding_io();
}
