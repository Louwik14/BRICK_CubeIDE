#include "Core/track_mute.h"

#include <string.h>

#include "Audio/fx_master_macro.h"
#include "Core/track_runtime.h"
#include "Core/track_sound_state.h"
#include "Keyboard/keyboard_engine.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_types.h"
#include "mixer.h"

static uint8_t g_track_mute_state[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY];

static uint8_t track_mute_resolve_mix_target(uint8_t track, uint8_t *out_mix_track)
{
    track_runtime_resolved_track_t resolved;
    if ((out_mix_track == NULL)
            || (track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (resolved.has_mix_target == 0U))
    {
        return 0U;
    }
    *out_mix_track = resolved.mix_track_id;
    return 1U;
}

void track_mute_init(void)
{
    memset(g_track_mute_state, 0, sizeof(g_track_mute_state));
    fx_master_macro_set_mute(0U);
}

track_mute_kind_t track_mute_get_kind(uint8_t track)
{
    track_runtime_descriptor_t descriptor;
    if ((track >= track_topology_get_logical_track_count())
            || (track_runtime_get_descriptor(track, &descriptor) == 0U)
            || (descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (descriptor.family == TRACK_RUNTIME_FAMILY_OFF)
            || ((descriptor.topology_capabilities & TRACK_CAPABILITY_MUTE) == 0U))
    {
        return TRACK_MUTE_KIND_NONE;
    }

    switch (descriptor.topology_role)
    {
        case TRACK_TOPOLOGY_ROLE_MASTER: return TRACK_MUTE_KIND_NONE;
        case TRACK_TOPOLOGY_ROLE_FX: return TRACK_MUTE_KIND_FX;
        default: break;
    }

    if (descriptor.family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        return TRACK_MUTE_KIND_MIDI;
    }
    if (descriptor.family == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return TRACK_MUTE_KIND_EXTERNAL;
    }
    if ((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor.type == TRACK_RUNTIME_TYPE_LOOPER))
    {
        return TRACK_MUTE_KIND_LOOPER;
    }
    return TRACK_MUTE_KIND_AUDIO;
}

uint8_t track_mute_is_available(uint8_t track)
{
    const track_mute_kind_t kind = track_mute_get_kind(track);
    if (kind == TRACK_MUTE_KIND_NONE)
    {
        return 0U;
    }
    if ((kind == TRACK_MUTE_KIND_MIDI) || (kind == TRACK_MUTE_KIND_FX))
    {
        return 1U;
    }
    uint8_t mix_track = 0U;
    return track_mute_resolve_mix_target(track, &mix_track);
}

uint8_t track_mute_get(uint8_t track)
{
    return (track < TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY) ? g_track_mute_state[track] : 0U;
}

uint8_t track_mute_set(uint8_t track, uint8_t muted)
{
    return track_mute_apply(track, muted, 1U);
}

uint8_t track_mute_apply(uint8_t track, uint8_t muted, uint8_t update_base_state)
{
    const track_mute_kind_t kind = track_mute_get_kind(track);
    if ((kind == TRACK_MUTE_KIND_NONE) || (track_mute_is_available(track) == 0U))
    {
        return 0U;
    }

    muted = (muted != 0U) ? 1U : 0U;
    g_track_mute_state[track] = muted;
    track_sound_state_t *const sound_state = track_sound_state_get(track);
    if ((update_base_state != 0U) && (sound_state != NULL))
    {
        sound_state->mix_mute = (float)muted;
    }

    if (kind == TRACK_MUTE_KIND_FX)
    {
        fx_master_macro_set_mute(muted);
        return 1U;
    }

    if ((kind == TRACK_MUTE_KIND_AUDIO)
            || (kind == TRACK_MUTE_KIND_MIDI)
            || (kind == TRACK_MUTE_KIND_EXTERNAL))
    {
        const seq_track_id_t seq_track = (seq_track_id_t)track;
        if (muted != 0U)
        {
            seq_runtime_set_tracks_muted(&seq_track, 1U, 1U);
        }
        else
        {
            seq_runtime_set_tracks_muted(&seq_track, 1U, 0U);
        }
    }

    if (kind != TRACK_MUTE_KIND_MIDI)
    {
        uint8_t mix_track = 0U;
        if (track_mute_resolve_mix_target(track, &mix_track) == 0U)
        {
            return 0U;
        }
        mixer_set_track_mute(mix_track, muted);
    }
    return 1U;
}

uint8_t track_mute_should_suppress_note_on(uint8_t track)
{
    return track_mute_get(track);
}
