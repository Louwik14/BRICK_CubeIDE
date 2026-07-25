/******************************************************************************
 * @file    keyboard_engine.c
 * @brief   Moteur de sortie des notes du clavier.
 *
 * Ce module centralise l’émission réelle des notes :
 * - envoi MIDI
 * - déclenchement des synthés internes
 * - routage éventuel vers le filtre / mixer
 * - extinction globale des notes
 *
 * Il sert de couche de sortie unique pour les autres modules, notamment
 * l’arpégiateur et les entrées clavier directes.
 ******************************************************************************/

#include "Keyboard/keyboard_engine.h"

#include "Audio/mixer.h"
#include "Audio/drum_synth.h"
#include "Keyboard/keyboard_params.h"
#include "MIDI/midi.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "ui_core.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Mod/mod_lfo_v1.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include <string.h>

static bool g_keyboard_engine_sounding_active = false;
static uint8_t g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
static uint8_t g_keyboard_engine_sounding_drum_instance = 0U;

#define KBD_REC_NOTE_STACK_DEPTH 8U
#define KEYBOARD_ENGINE_MONO_HELD_MAX 8U
#define KEYBOARD_ENGINE_GROUP_NOTE_STACK_DEPTH 8U
static uint8_t g_kbd_rec_note_stack_ch[128U][KBD_REC_NOTE_STACK_DEPTH];
static uint8_t g_kbd_rec_note_stack_count[128U];
static uint8_t g_keyboard_engine_group_note_track[UI_TRACK_COUNT][128U][KEYBOARD_ENGINE_GROUP_NOTE_STACK_DEPTH];
static uint8_t g_keyboard_engine_group_note_count[UI_TRACK_COUNT][128U];
static uint8_t g_keyboard_engine_group_track_active_count[UI_TRACK_COUNT];
static uint8_t g_keyboard_engine_group_rr_cursor[UI_TRACK_COUNT];

typedef struct
{
    uint8_t note;
    uint8_t velocity;
} keyboard_engine_mono_note_t;

static keyboard_engine_mono_note_t g_keyboard_engine_mono_held[KEYBOARD_ENGINE_MONO_HELD_MAX];
static uint8_t g_keyboard_engine_mono_held_count = 0U;
static uint8_t g_keyboard_engine_mono_active_valid = 0U;
static uint8_t g_keyboard_engine_mono_active_note = 0U;

static int8_t keyboard_engine_mono_find(uint8_t note)
{
    for (uint8_t i = 0U; i < g_keyboard_engine_mono_held_count; ++i)
    {
        if (g_keyboard_engine_mono_held[i].note == note)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

static void keyboard_engine_mono_remove_at(uint8_t index)
{
    if (index >= g_keyboard_engine_mono_held_count)
    {
        return;
    }

    for (uint8_t i = index; i + 1U < g_keyboard_engine_mono_held_count; ++i)
    {
        g_keyboard_engine_mono_held[i] = g_keyboard_engine_mono_held[i + 1U];
    }
    g_keyboard_engine_mono_held_count--;
}

static void keyboard_engine_mono_clear(void)
{
    g_keyboard_engine_mono_held_count = 0U;
    g_keyboard_engine_mono_active_valid = 0U;
    g_keyboard_engine_mono_active_note = 0U;
}

static uint8_t keyboard_engine_get_play_owner_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t owner_track = active_track;
    if (track_runtime_get_voice_group_effective_master(active_track, &owner_track) == 0U)
    {
        return active_track;
    }
    return owner_track;
}

static bool keyboard_engine_active_track_has_midi_note_path(void)
{
    const uint8_t owner_track = keyboard_engine_get_play_owner_track();
    const ui_track_config_t config = ui_get_track_config(owner_track);
    return (ui_track_family_is_engine(config.family) != 0) || (config.type == UI_TRACK_TYPE_HYBRID);
}

static bool keyboard_engine_track_has_midi_note_path(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return false;
    }

    const ui_track_config_t config = ui_get_track_config(track);
    return (ui_track_family_is_engine(config.family) != 0) || (config.type == UI_TRACK_TYPE_HYBRID);
}

static bool keyboard_engine_active_track_accepts_internal_source(void)
{
    const ui_track_midi_source_t source = ui_get_track_midi_source(keyboard_engine_get_play_owner_track());
    return (source == UI_TRACK_MIDI_SRC_INT) || (source == UI_TRACK_MIDI_SRC_ALL);
}

static bool keyboard_engine_track_accepts_internal_source(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return false;
    }

    const ui_track_midi_source_t source = ui_get_track_midi_source(track);
    return (source == UI_TRACK_MIDI_SRC_INT) || (source == UI_TRACK_MIDI_SRC_ALL);
}

static uint8_t keyboard_engine_get_track_midi_channel_zero_based(uint8_t track)
{
    const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
    return (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
}

static void keyboard_engine_group_note_push(uint8_t owner_track, uint8_t note, uint8_t track)
{
    if ((owner_track >= UI_TRACK_COUNT) || (note >= 128U) || (track >= UI_TRACK_COUNT))
    {
        return;
    }

    uint8_t count = g_keyboard_engine_group_note_count[owner_track][note];
    if (count >= KEYBOARD_ENGINE_GROUP_NOTE_STACK_DEPTH)
    {
        for (uint8_t i = 1U; i < KEYBOARD_ENGINE_GROUP_NOTE_STACK_DEPTH; ++i)
        {
            g_keyboard_engine_group_note_track[owner_track][note][i - 1U] =
                g_keyboard_engine_group_note_track[owner_track][note][i];
        }
        count = (uint8_t)(KEYBOARD_ENGINE_GROUP_NOTE_STACK_DEPTH - 1U);
    }

    g_keyboard_engine_group_note_track[owner_track][note][count] = track;
    g_keyboard_engine_group_note_count[owner_track][note] = (uint8_t)(count + 1U);
}

static uint8_t keyboard_engine_group_note_pop(uint8_t owner_track, uint8_t note, uint8_t fallback_track)
{
    if ((owner_track >= UI_TRACK_COUNT) || (note >= 128U))
    {
        return fallback_track;
    }

    const uint8_t count = g_keyboard_engine_group_note_count[owner_track][note];
    if (count == 0U)
    {
        return fallback_track;
    }

    const uint8_t index = (uint8_t)(count - 1U);
    const uint8_t track = g_keyboard_engine_group_note_track[owner_track][note][index];
    g_keyboard_engine_group_note_count[owner_track][note] = index;
    return (track < UI_TRACK_COUNT) ? track : fallback_track;
}

static void keyboard_engine_all_notes_off_local_track(uint8_t track)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;
    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return;
    }
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }
    if ((track_runtime_supports_vca_gate(ctx) != 0U)
            && (track_runtime_get_mix_target_track(track, &mix_track) != 0U))
    {
        mixer_track_vca_all_notes_off(mix_track);
    }
    if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        brick6_sampler_runtime_stop(track);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
    {
        brick6_braids_runtime_all_notes_off(ctx->instance_id);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        drum_synth_all_notes_off_for_instance(ctx->instance_id);
    }
    mod_lfo_v1_all_notes_off(track);
}

static void keyboard_engine_all_notes_off_for_owner(uint8_t owner_track)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(owner_track, &role_u8);
    uint8_t members[UI_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
            && (track_runtime_collect_voice_group_members(owner_track, members, (uint8_t)UI_TRACK_COUNT, &member_count) != 0U)
            && (member_count > 0U))
    {
        for (uint8_t i = 0U; i < member_count; ++i)
        {
            keyboard_engine_all_notes_off_local_track(members[i]);
        }
        return;
    }

    keyboard_engine_all_notes_off_local_track(owner_track);
}

static void keyboard_engine_emit_note_for_track(uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return;
    }

    if (is_note_on != 0U)
    {
        mod_lfo_v1_note_trigger(track);
    }
    else
    {
        mod_lfo_v1_note_release(track);
    }

    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        if (is_note_on != 0U)
        {
            mixer_track_filter_note_on(filter_track, note, velocity);
        }
        else
        {
            mixer_track_filter_note_off(filter_track, note);
        }
    }
    if ((track_runtime_supports_vca_gate(ctx) != 0U)
            && (track_runtime_get_mix_target_track(track, &mix_track) != 0U))
    {
        if (is_note_on != 0U)
        {
            mixer_track_vca_note_on(mix_track, note, velocity);
        }
        else
        {
            mixer_track_vca_note_off(mix_track, note);
        }
    }

    g_keyboard_engine_sounding_active = true;
    g_keyboard_engine_sounding_engine = ctx->engine;

    if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        g_keyboard_engine_sounding_drum_instance = ctx->instance_id;
        if (is_note_on != 0U)
        {
            drum_synth_note_on_for_instance(g_keyboard_engine_sounding_drum_instance, note, velocity);
        }
        else
        {
            drum_synth_note_off_for_instance(g_keyboard_engine_sounding_drum_instance, note);
        }
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        if (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
        {
            if (is_note_on != 0U)
            {
                (void)brick6_sampler_runtime_trigger_multi_track_note_velocity(track, note, velocity);
            }
            else
            {
                brick6_sampler_runtime_note_off_multi_track_note(track, note);
            }
            return;
        }

        if (is_note_on != 0U)
        {
            brick6_sampler_runtime_trigger_note_velocity(track, note, velocity);
        }
        else
        {
            brick6_sampler_runtime_note_off_note(track, note);
        }
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
    {
        if (is_note_on != 0U)
        {
            brick6_braids_runtime_note_on(ctx->instance_id, (float)note, (float)velocity / 127.0f);
        }
        else
        {
            brick6_braids_runtime_note_off(ctx->instance_id, note);
        }
    }
}

static void keyboard_engine_send_note_for_owner_track(uint8_t owner_track,
                                                      uint8_t note,
                                                      uint8_t velocity,
                                                      uint8_t is_note_on)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(owner_track, &role_u8);

    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        keyboard_engine_emit_note_for_track(owner_track, note, velocity, is_note_on);
        return;
    }

    uint8_t members[UI_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((track_runtime_collect_voice_group_members(owner_track, members, (uint8_t)UI_TRACK_COUNT, &member_count) == 0U)
            || (member_count == 0U))
    {
        keyboard_engine_emit_note_for_track(owner_track, note, velocity, is_note_on);
        return;
    }

    if (is_note_on != 0U)
    {
        const uint8_t start = (uint8_t)(g_keyboard_engine_group_rr_cursor[owner_track] % member_count);
        uint8_t target_track = members[start];
        uint8_t found_free = 0U;
        for (uint8_t i = 0U; i < member_count; ++i)
        {
            const uint8_t candidate = members[(uint8_t)((start + i) % member_count)];
            if (g_keyboard_engine_group_track_active_count[candidate] == 0U)
            {
                target_track = candidate;
                found_free = 1U;
                break;
            }
        }
        if (found_free == 0U)
        {
            target_track = members[start];
        }
        g_keyboard_engine_group_rr_cursor[owner_track] = (uint8_t)((start + 1U) % member_count);
        keyboard_engine_group_note_push(owner_track, note, target_track);
        if (g_keyboard_engine_group_track_active_count[target_track] < 255U)
        {
            g_keyboard_engine_group_track_active_count[target_track]++;
        }
        keyboard_engine_emit_note_for_track(target_track, note, velocity, 1U);
        return;
    }

    uint8_t target_track = keyboard_engine_group_note_pop(owner_track, note, owner_track);
    if ((target_track < UI_TRACK_COUNT) && (g_keyboard_engine_group_track_active_count[target_track] > 0U))
    {
        g_keyboard_engine_group_track_active_count[target_track]--;
    }
    keyboard_engine_emit_note_for_track(target_track, note, velocity, 0U);
}

static void keyboard_engine_dispatch_note_to_matching_tracks(uint8_t channel,
                                                             uint8_t note,
                                                             uint8_t velocity,
                                                             uint8_t source_internal,
                                                             uint8_t is_note_on)
{
    uint8_t owner_handled[UI_TRACK_COUNT] = {0U};
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && !(ui_track_family_is_input(cfg.family) && (cfg.type == UI_TRACK_TYPE_HYBRID)))
        {
            continue;
        }

        const ui_track_midi_source_t source = ui_get_track_midi_source(track);
        if (source_internal != 0U)
        {
            if ((source != UI_TRACK_MIDI_SRC_INT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }
        else
        {
            if ((source != UI_TRACK_MIDI_SRC_EXT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }

        if (keyboard_engine_get_track_midi_channel_zero_based(track) != channel)
        {
            continue;
        }

        uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
        (void)track_runtime_get_voice_group_role(track, &role_u8);
        if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            continue;
        }

        uint8_t owner_track = track;
        (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
        if ((owner_track >= UI_TRACK_COUNT) || (owner_handled[owner_track] != 0U))
        {
            continue;
        }
        owner_handled[owner_track] = 1U;

        keyboard_engine_send_note_for_owner_track(owner_track, note, velocity, is_note_on);
    }
}

static void keyboard_engine_all_notes_off_matching_tracks(uint8_t channel, uint8_t source_internal)
{
    uint8_t owner_handled[UI_TRACK_COUNT] = {0U};
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && !(ui_track_family_is_input(cfg.family) && (cfg.type == UI_TRACK_TYPE_HYBRID)))
        {
            continue;
        }

        const ui_track_midi_source_t source = ui_get_track_midi_source(track);
        if (source_internal != 0U)
        {
            if ((source != UI_TRACK_MIDI_SRC_INT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }
        else
        {
            if ((source != UI_TRACK_MIDI_SRC_EXT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }

        if (keyboard_engine_get_track_midi_channel_zero_based(track) != channel)
        {
            continue;
        }

        uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
        (void)track_runtime_get_voice_group_role(track, &role_u8);
        if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            continue;
        }

        uint8_t owner_track = track;
        (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
        if ((owner_track >= UI_TRACK_COUNT) || (owner_handled[owner_track] != 0U))
        {
            continue;
        }
        owner_handled[owner_track] = 1U;

        keyboard_engine_all_notes_off_for_owner(owner_track);
    }
}

static void keyboard_engine_live_rec_push_internal_channel(uint8_t note, uint8_t channel)
{
    if (note >= 128U)
    {
        return;
    }

    uint8_t count = g_kbd_rec_note_stack_count[note];
    if (count >= KBD_REC_NOTE_STACK_DEPTH)
    {
        for (uint8_t i = 1U; i < KBD_REC_NOTE_STACK_DEPTH; ++i)
        {
            g_kbd_rec_note_stack_ch[note][i - 1U] = g_kbd_rec_note_stack_ch[note][i];
        }
        count = (uint8_t)(KBD_REC_NOTE_STACK_DEPTH - 1U);
    }

    g_kbd_rec_note_stack_ch[note][count] = channel;
    g_kbd_rec_note_stack_count[note] = (uint8_t)(count + 1U);
}

static uint8_t keyboard_engine_live_rec_pop_internal_channel(uint8_t note, uint8_t fallback_channel)
{
    if (note >= 128U)
    {
        return fallback_channel;
    }

    const uint8_t count = g_kbd_rec_note_stack_count[note];
    if (count == 0U)
    {
        return fallback_channel;
    }

    const uint8_t index = (uint8_t)(count - 1U);
    const uint8_t channel = g_kbd_rec_note_stack_ch[note][index];
    g_kbd_rec_note_stack_count[note] = index;
    return channel;
}

static void keyboard_engine_note_on_internal(seq_live_rec_source_t source,
                                             uint8_t channel_zero_based,
                                             uint8_t note,
                                             uint8_t velocity)
{
    keyboard_engine_live_rec_push_internal_channel(note, channel_zero_based);
    seq_runtime_live_rec_note_on(source, channel_zero_based, note, velocity);

    if ((source == SEQ_LIVE_REC_SRC_INTERNAL) && keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_on(MIDI_DEST_USB,
                     keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track()),
                     note,
                     velocity);
    }

    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        if (!keyboard_engine_active_track_has_midi_note_path())
        {
            return;
        }

        if (!keyboard_engine_active_track_accepts_internal_source())
        {
            return;
        }

        if (keyboard_params_get_mono_last() == false)
        {
            keyboard_engine_dispatch_note_to_matching_tracks(channel_zero_based, note, velocity, 1U, 1U);
            return;
        }

        const int8_t existing = keyboard_engine_mono_find(note);
        if (existing >= 0)
        {
            keyboard_engine_mono_remove_at((uint8_t)existing);
        }
        else if (g_keyboard_engine_mono_held_count >= KEYBOARD_ENGINE_MONO_HELD_MAX)
        {
            keyboard_engine_mono_remove_at(0U);
        }

        g_keyboard_engine_mono_held[g_keyboard_engine_mono_held_count].note = note;
        g_keyboard_engine_mono_held[g_keyboard_engine_mono_held_count].velocity = velocity;
        g_keyboard_engine_mono_held_count++;
        g_keyboard_engine_mono_active_valid = 1U;
        g_keyboard_engine_mono_active_note = note;
        keyboard_engine_dispatch_note_to_matching_tracks(channel_zero_based, note, velocity, 1U, 1U);
        return;
    }
}

static void keyboard_engine_note_off_internal(seq_live_rec_source_t source,
                                              uint8_t channel_zero_based,
                                              uint8_t note)
{
    const uint8_t note_on_channel = keyboard_engine_live_rec_pop_internal_channel(note, channel_zero_based);
    seq_runtime_live_rec_note_off(source, note_on_channel, note);

    if ((source == SEQ_LIVE_REC_SRC_INTERNAL) && keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_off(MIDI_DEST_USB,
                      keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track()),
                      note,
                      0U);
    }

    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        if (!keyboard_engine_active_track_has_midi_note_path() && !g_keyboard_engine_sounding_active)
        {
            return;
        }

        if (keyboard_params_get_mono_last() == false)
        {
            keyboard_engine_dispatch_note_to_matching_tracks(note_on_channel, note, 0U, 1U, 0U);
            return;
        }

        const int8_t existing = keyboard_engine_mono_find(note);
        if (existing < 0)
        {
            return;
        }

        const uint8_t was_active = (g_keyboard_engine_mono_active_valid != 0U) && (g_keyboard_engine_mono_active_note == note);
        keyboard_engine_mono_remove_at((uint8_t)existing);

        if (was_active == 0U)
        {
            return;
        }

        if (g_keyboard_engine_mono_held_count > 0U)
        {
            const keyboard_engine_mono_note_t *const fallback = &g_keyboard_engine_mono_held[g_keyboard_engine_mono_held_count - 1U];
            g_keyboard_engine_mono_active_valid = 1U;
            g_keyboard_engine_mono_active_note = fallback->note;
            keyboard_engine_dispatch_note_to_matching_tracks(note_on_channel, fallback->note, fallback->velocity, 1U, 1U);
            return;
        }

        g_keyboard_engine_mono_active_valid = 0U;
        g_keyboard_engine_mono_active_note = 0U;
        keyboard_engine_dispatch_note_to_matching_tracks(note_on_channel, note, 0U, 1U, 0U);
    }
}

void keyboard_engine_note_on_from_source(seq_live_rec_source_t source,
                                         uint8_t channel_zero_based,
                                         uint8_t note,
                                         uint8_t velocity)
{
    keyboard_engine_note_on_internal(source, channel_zero_based, note, velocity);
}

void keyboard_engine_note_off_from_source(seq_live_rec_source_t source,
                                         uint8_t channel_zero_based,
                                         uint8_t note)
{
    keyboard_engine_note_off_internal(source, channel_zero_based, note);
}

void keyboard_engine_note_on(uint8_t note, uint8_t velocity)
{
    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track());
    keyboard_engine_note_on_internal(SEQ_LIVE_REC_SRC_INTERNAL, active_channel, note, velocity);
}

void keyboard_engine_note_off(uint8_t note)
{
    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track());
    keyboard_engine_note_off_internal(SEQ_LIVE_REC_SRC_INTERNAL, active_channel, note);
}

void keyboard_engine_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    uint8_t owner_track = track;
    (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
    if (owner_track >= UI_TRACK_COUNT)
    {
        owner_track = track;
    }

    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    keyboard_engine_live_rec_push_internal_channel(note, channel);
    seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_INTERNAL, channel, note, velocity);

    if (keyboard_engine_track_has_midi_note_path(owner_track))
    {
        midi_note_on(MIDI_DEST_USB, channel, note, velocity);
    }

    if ((keyboard_engine_track_has_midi_note_path(owner_track) == false)
            || (keyboard_engine_track_accepts_internal_source(owner_track) == false))
    {
        return;
    }

    keyboard_engine_send_note_for_owner_track(owner_track, note, velocity, 1U);
}

void keyboard_engine_note_off_for_track(uint8_t track, uint8_t note)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    uint8_t owner_track = track;
    (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
    if (owner_track >= UI_TRACK_COUNT)
    {
        owner_track = track;
    }

    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    const uint8_t note_on_channel = keyboard_engine_live_rec_pop_internal_channel(note, channel);
    seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_INTERNAL, note_on_channel, note);

    if (keyboard_engine_track_has_midi_note_path(owner_track))
    {
        midi_note_off(MIDI_DEST_USB, channel, note, 0U);
    }

    if ((keyboard_engine_track_has_midi_note_path(owner_track) == false)
            || (keyboard_engine_track_accepts_internal_source(owner_track) == false))
    {
        return;
    }

    keyboard_engine_send_note_for_owner_track(owner_track, note, 0U, 0U);
}

void keyboard_engine_all_notes_off_for_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    uint8_t owner_track = track;
    (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
    if (owner_track >= UI_TRACK_COUNT)
    {
        owner_track = track;
    }

    keyboard_engine_mono_clear();
    keyboard_engine_all_notes_off_for_owner(owner_track);
    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    if (keyboard_engine_track_has_midi_note_path(owner_track))
    {
        midi_all_notes_off(MIDI_DEST_USB, channel);
    }
    g_keyboard_engine_sounding_active = false;
    g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    g_keyboard_engine_sounding_drum_instance = 0U;
    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
}

void keyboard_engine_all_notes_off(void)
{
    keyboard_engine_mono_clear();
    drum_synth_all_notes_off_all();
    memset(g_keyboard_engine_group_note_count, 0, sizeof(g_keyboard_engine_group_note_count));
    memset(g_keyboard_engine_group_track_active_count, 0, sizeof(g_keyboard_engine_group_track_active_count));

    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track());
    keyboard_engine_all_notes_off_matching_tracks(active_channel, 1U);

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, active_channel);
    }

    g_keyboard_engine_sounding_active = false;
    g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    g_keyboard_engine_sounding_drum_instance = 0U;
    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
}

void keyboard_engine_clear_state_silent(void)
{
    keyboard_engine_mono_clear();
    g_keyboard_engine_sounding_active = false;
    g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    g_keyboard_engine_sounding_drum_instance = 0U;
    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
    memset(g_keyboard_engine_group_note_count, 0, sizeof(g_keyboard_engine_group_note_count));
    memset(g_keyboard_engine_group_track_active_count, 0, sizeof(g_keyboard_engine_group_track_active_count));
}

void keyboard_engine_midi_receive(const uint8_t *msg, size_t len)
{
    if ((msg == NULL) || (len < 2U))
    {
        return;
    }

    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0U;
    const uint8_t channel = status & 0x0FU;
    const uint8_t data1 = msg[1] & 0x7FU;
    const uint8_t data2 = (len >= 3U) ? (msg[2] & 0x7FU) : 0U;

    const uint8_t is_note_msg = ((type == 0x90U) || (type == 0x80U)) ? 1U : 0U;
    const uint8_t is_cc_msg = (type == 0xB0U) ? 1U : 0U;
    if ((is_note_msg == 0U) && (is_cc_msg == 0U))
    {
        return;
    }

    const uint8_t note = data1;
    const uint8_t velocity = data2;
    const uint8_t cc = data1;
    const uint8_t is_note_on = ((type == 0x90U) && (velocity > 0U)) ? 1U : 0U;
    const uint8_t is_note_off = ((type == 0x80U) || ((type == 0x90U) && (velocity == 0U))) ? 1U : 0U;
    const uint8_t is_all_notes_off = ((is_cc_msg != 0U) && ((cc == 123U) || (cc == 120U))) ? 1U : 0U;

    if (is_note_on != 0U)
    {
        seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_EXTERNAL, channel, note, velocity);
    }
    else if (is_note_off != 0U)
    {
        seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_EXTERNAL, channel, note);
    }

    uint8_t owner_handled[UI_TRACK_COUNT] = {0U};
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && !(ui_track_family_is_input(cfg.family) && (cfg.type == UI_TRACK_TYPE_HYBRID)))
        {
            continue;
        }
        const ui_track_midi_source_t source = ui_get_track_midi_source(track);
        if ((source != UI_TRACK_MIDI_SRC_EXT) && (source != UI_TRACK_MIDI_SRC_ALL))
        {
            continue;
        }
        if (keyboard_engine_get_track_midi_channel_zero_based(track) != channel)
        {
            continue;
        }

        uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
        (void)track_runtime_get_voice_group_role(track, &role_u8);
        if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            continue;
        }

        uint8_t owner_track = track;
        (void)track_runtime_get_voice_group_effective_master(track, &owner_track);
        if ((owner_track >= UI_TRACK_COUNT) || (owner_handled[owner_track] != 0U))
        {
            continue;
        }
        owner_handled[owner_track] = 1U;

        if (is_note_on != 0U)
        {
            keyboard_engine_send_note_for_owner_track(owner_track, note, velocity, 1U);
            continue;
        }
        if (is_note_off != 0U)
        {
            keyboard_engine_send_note_for_owner_track(owner_track, note, 0U, 0U);
            continue;
        }

        if (is_all_notes_off != 0U)
        {
            keyboard_engine_all_notes_off_for_owner(owner_track);
        }
    }

}
