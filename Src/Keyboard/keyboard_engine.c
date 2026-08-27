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

#include "Audio/control_audio_queue.h"
#include "Core/control_music_output.h"
#include "Keyboard/keyboard_params.h"
#include "MIDI/midi.h"
#include "ui_core.h"
#include "Core/track_runtime.h"
#include "Core/live_clock.h"
#include "Core/track_mute.h"
#include "Mod/mod_lfo_v1.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "NoteFx/note_fx_pipeline.h"
#include <string.h>

#define KBD_REC_NOTE_STACK_DEPTH 8U
#define KEYBOARD_ENGINE_MONO_HELD_MAX 8U
#define KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY 128U
static uint8_t g_kbd_rec_note_stack_ch[128U][KBD_REC_NOTE_STACK_DEPTH];
static uint8_t g_kbd_rec_note_stack_count[128U];
static uint8_t g_kbd_rec_track_note_channel[BRICK_ENTITY_TOP_LEVEL_COUNT][128U];
static uint8_t g_kbd_rec_track_note_count[BRICK_ENTITY_TOP_LEVEL_COUNT][128U];

typedef struct
{
    uint8_t active;
    uint8_t track;
    uint8_t note;
    uint8_t provenance;
    uint32_t occurrence_id;
} keyboard_engine_source_occurrence_t;

static keyboard_engine_source_occurrence_t
    g_keyboard_engine_source_occurrence[KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY];
static uint32_t g_keyboard_engine_next_occurrence_id;
static uint8_t g_keyboard_engine_timed_context_active;
static uint32_t g_keyboard_engine_capture_tick;
static uint32_t g_keyboard_engine_ingress_serial;

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
    return ui_get_active_lane();
}

static bool keyboard_engine_active_track_has_midi_note_path(void)
{
    const uint8_t owner_track = keyboard_engine_get_play_owner_track();
    return track_runtime_has_capability(owner_track, TRACK_CAPABILITY_MIDI) != 0U;
}

static bool keyboard_engine_track_has_midi_note_path(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return false;
    }

    return track_runtime_has_capability(track, TRACK_CAPABILITY_MIDI) != 0U;
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

static uint8_t keyboard_engine_all_notes_off_local_track(uint8_t track)
{
    uint64_t due_sample = 0U;
    if (!live_clock_read_audio_sample(&due_sample))
        return 0U;
    due_sample = control_music_output_first_unpublished_sample(due_sample);
    return control_music_output_close_entity(track, due_sample);
}

static uint8_t keyboard_engine_all_notes_off_for_owner(uint8_t owner_track)
{
    return keyboard_engine_all_notes_off_local_track(owner_track);
}

static uint32_t keyboard_engine_next_occurrence_id(void)
{
    for (uint16_t attempt = 0U;
            attempt <= KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY; ++attempt)
    {
        g_keyboard_engine_next_occurrence_id =
            (g_keyboard_engine_next_occurrence_id + 1U)
            & NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
        if (g_keyboard_engine_next_occurrence_id == 0U)
            g_keyboard_engine_next_occurrence_id = 1U;
        uint8_t collision = 0U;
        for (uint8_t i = 0U; i < KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY; ++i)
        {
            collision |= (uint8_t)(
                (g_keyboard_engine_source_occurrence[i].active != 0U)
                && ((g_keyboard_engine_source_occurrence[i].occurrence_id
                        & NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
                    == g_keyboard_engine_next_occurrence_id));
        }
        if (collision == 0U)
            return g_keyboard_engine_next_occurrence_id;
    }
    return 0U;
}

static int8_t keyboard_engine_find_source_occurrence(uint8_t owner_track,
                                                      uint8_t note,
                                                      note_event_provenance_t provenance)
{
    int8_t selected = -1;
    for (uint8_t i = 0U; i < KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY; ++i)
    {
        const keyboard_engine_source_occurrence_t *const occurrence =
            &g_keyboard_engine_source_occurrence[i];
        if ((occurrence->active != 0U)
                && (occurrence->track == owner_track)
                && (occurrence->note == note)
                && (occurrence->provenance == (uint8_t)provenance)
                && ((selected < 0)
                    || ((int32_t)(occurrence->occurrence_id
                        - g_keyboard_engine_source_occurrence[(uint8_t)selected]
                            .occurrence_id) < 0)))
        {
            selected = (int8_t)i;
        }
    }
    return selected;
}

static void keyboard_engine_send_note_for_owner_track_with_capture(
    uint8_t owner_track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    note_event_provenance_t provenance, uint8_t capture_tick_valid,
    uint32_t capture_tick, uint32_t ingress_serial)
{
    int8_t index = -1;
    uint32_t occurrence_id = 0U;
    if (is_note_on != 0U)
    {
        for (uint8_t i = 0U; i < KEYBOARD_ENGINE_SOURCE_OCCURRENCE_CAPACITY; ++i)
        {
            if (g_keyboard_engine_source_occurrence[i].active == 0U)
            {
                index = (int8_t)i;
                break;
            }
        }
        if (index < 0)
        {
            return;
        }
        const uint32_t counter = keyboard_engine_next_occurrence_id();
        if (counter == 0U)
            return;
        occurrence_id = note_event_occurrence_namespace(provenance)
                      | (counter & NOTE_EVENT_OCCURRENCE_COUNTER_MASK);
    }
    else
    {
        index = keyboard_engine_find_source_occurrence(owner_track, note,
                                                        provenance);
        if (index < 0)
        {
            return;
        }
        occurrence_id =
            g_keyboard_engine_source_occurrence[(uint8_t)index].occurrence_id;
    }

    const note_fx_result_t result = (capture_tick_valid != 0U)
        ? note_fx_pipeline_submit_source_capture_tick(
            owner_track, note, velocity, is_note_on, capture_tick,
            ingress_serial, provenance, occurrence_id)
        : note_fx_pipeline_submit_source_occurrence(
            owner_track, note, velocity, is_note_on,
            NOTE_FX_SAMPLE_TIME_CONTROL_ANCHOR, provenance, occurrence_id);
    if (result != NOTE_EVENT_RESULT_ACCEPTED)
    {
        return;
    }

    if (is_note_on != 0U)
    {
        g_keyboard_engine_source_occurrence[(uint8_t)index] =
            (keyboard_engine_source_occurrence_t){
                .active = 1U,
                .track = owner_track,
                .note = note,
                .provenance = (uint8_t)provenance,
                .occurrence_id = occurrence_id
            };
    }
    else
    {
        g_keyboard_engine_source_occurrence[(uint8_t)index].active = 0U;
    }
}

static void keyboard_engine_send_note_for_owner_track(uint8_t owner_track,
                                                      uint8_t note,
                                                      uint8_t velocity,
                                                      uint8_t is_note_on,
                                                      note_event_provenance_t provenance)
{
    keyboard_engine_send_note_for_owner_track_with_capture(
        owner_track, note, velocity, is_note_on, provenance, 0U, 0U, 0U);
}

static void keyboard_engine_send_note_for_current_context(
    uint8_t owner_track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    note_event_provenance_t provenance)
{
    if (g_keyboard_engine_timed_context_active != 0U)
    {
        keyboard_engine_send_note_for_owner_track_with_capture(
            owner_track, note, velocity, is_note_on, provenance, 1U,
            g_keyboard_engine_capture_tick,
            g_keyboard_engine_ingress_serial);
    }
    else
    {
        keyboard_engine_send_note_for_owner_track(owner_track, note, velocity,
                                                   is_note_on, provenance);
    }
}

static void keyboard_engine_dispatch_note_to_matching_tracks(uint8_t channel,
                                                             uint8_t note,
                                                             uint8_t velocity,
                                                             uint8_t source_internal,
                                                             uint8_t is_note_on)
{
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && (cfg.family != UI_TRACK_FAMILY_EXTERNAL))
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

        keyboard_engine_send_note_for_current_context(
            track, note, velocity, is_note_on, NOTE_EVENT_SOURCE_KEY);
    }
}

static uint8_t keyboard_engine_all_notes_off_matching_tracks(
    uint8_t channel, uint8_t source_internal)
{
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && (cfg.family != UI_TRACK_FAMILY_EXTERNAL))
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

        if (keyboard_engine_all_notes_off_for_owner(track) == 0U)
            return 0U;
    }
    return 1U;
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

static void keyboard_engine_live_rec_push_track_channel(uint8_t owner_track,
                                                        uint8_t note,
                                                        uint8_t channel)
{
    if ((owner_track >= BRICK_ENTITY_TOP_LEVEL_COUNT) || (note >= 128U))
    {
        return;
    }

    if (g_kbd_rec_track_note_count[owner_track][note] < 0xFFU)
    {
        g_kbd_rec_track_note_count[owner_track][note]++;
    }
    g_kbd_rec_track_note_channel[owner_track][note] = channel;
}

static uint8_t keyboard_engine_live_rec_pop_track_channel(uint8_t owner_track,
                                                          uint8_t note,
                                                          uint8_t fallback_channel)
{
    if ((owner_track >= BRICK_ENTITY_TOP_LEVEL_COUNT) || (note >= 128U))
    {
        return fallback_channel;
    }

    const uint8_t count = g_kbd_rec_track_note_count[owner_track][note];
    if (count == 0U)
    {
        return fallback_channel;
    }

    g_kbd_rec_track_note_count[owner_track][note] = (uint8_t)(count - 1U);
    return g_kbd_rec_track_note_channel[owner_track][note];
}

static void keyboard_engine_note_on_internal(seq_live_rec_source_t source,
                                             uint8_t channel_zero_based,
                                             uint8_t note,
                                             uint8_t velocity)
{
    keyboard_engine_live_rec_push_internal_channel(note, channel_zero_based);
    seq_runtime_live_rec_note_on(source, channel_zero_based, note, velocity);

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

    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        if (!keyboard_engine_active_track_has_midi_note_path())
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

static void keyboard_engine_note_on_for_track_internal(uint8_t track,
                                                        uint8_t note,
                                                        uint8_t velocity,
                                                        uint8_t capture_tick_valid,
                                                        uint32_t capture_tick,
                                                        uint32_t ingress_serial)
{
    if ((track >= UI_TRACK_COUNT) || (entity_topology_is_active(track) == 0U))
    {
        return;
    }

    const uint8_t owner_track = track;
    if (track_mute_should_suppress_note_on(owner_track) != 0U)
    {
        return;
    }

    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    keyboard_engine_live_rec_push_track_channel(owner_track, note, channel);
    if (capture_tick_valid == 0U)
    {
        seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_INTERNAL, channel, note, velocity);
    }

    if ((keyboard_engine_track_has_midi_note_path(owner_track) == false)
            || (keyboard_engine_track_accepts_internal_source(owner_track) == false))
    {
        return;
    }

    keyboard_engine_send_note_for_owner_track_with_capture(
        owner_track, note, velocity, 1U, NOTE_EVENT_SOURCE_KEY,
        capture_tick_valid, capture_tick, ingress_serial);
}

void keyboard_engine_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity)
{
    keyboard_engine_note_on_for_track_internal(track, note, velocity, 0U, 0U, 0U);
}

void keyboard_engine_note_on_for_track_timed(uint8_t track, uint8_t note,
                                             uint8_t velocity,
                                             uint32_t capture_tick,
                                             uint32_t ingress_serial)
{
    keyboard_engine_note_on_for_track_internal(track, note, velocity, 1U,
                                                capture_tick, ingress_serial);
}

static void keyboard_engine_note_off_for_track_internal(uint8_t track,
                                                         uint8_t note,
                                                         uint8_t capture_tick_valid,
                                                         uint32_t capture_tick,
                                                         uint32_t ingress_serial)
{
    if ((track >= UI_TRACK_COUNT) || (entity_topology_is_active(track) == 0U))
    {
        return;
    }

    const uint8_t owner_track = track;

    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    const uint8_t note_on_channel =
        keyboard_engine_live_rec_pop_track_channel(owner_track, note, channel);
    if (capture_tick_valid == 0U)
    {
        seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_INTERNAL,
                                      note_on_channel, note);
    }

    if ((keyboard_engine_track_has_midi_note_path(owner_track) == false)
            || (keyboard_engine_track_accepts_internal_source(owner_track) == false))
    {
        return;
    }

    keyboard_engine_send_note_for_owner_track_with_capture(
        owner_track, note, 0U, 0U, NOTE_EVENT_SOURCE_KEY,
        capture_tick_valid, capture_tick, ingress_serial);
}

void keyboard_engine_note_off_for_track(uint8_t track, uint8_t note)
{
    keyboard_engine_note_off_for_track_internal(track, note, 0U, 0U, 0U);
}

void keyboard_engine_note_off_for_track_timed(uint8_t track, uint8_t note,
                                              uint32_t capture_tick,
                                              uint32_t ingress_serial)
{
    keyboard_engine_note_off_for_track_internal(track, note, 1U, capture_tick,
                                                 ingress_serial);
}

uint8_t keyboard_engine_all_notes_off_for_track(uint8_t track)
{
    if ((track >= UI_TRACK_COUNT) || (entity_topology_is_active(track) == 0U))
    {
        return 0U;
    }

    const uint8_t owner_track = track;

    if (keyboard_engine_all_notes_off_for_owner(owner_track) == 0U)
        return 0U;
    const uint8_t channel = keyboard_engine_get_track_midi_channel_zero_based(owner_track);
    if (keyboard_engine_track_has_midi_note_path(owner_track))
    {
        for (uint16_t note = 0U; note < 128U; ++note)
        {
            if (g_kbd_rec_track_note_count[owner_track][note] != 0U)
            {
                midi_note_off(MIDI_DEST_USB,
                              g_kbd_rec_track_note_channel[owner_track][note],
                              (uint8_t)note,
                              0U);
            }
        }
        midi_all_notes_off(MIDI_DEST_USB, channel);
    }
    memset(g_kbd_rec_track_note_count[owner_track],
           0,
           sizeof(g_kbd_rec_track_note_count[owner_track]));
    return 1U;
}

uint8_t keyboard_engine_all_notes_off(void)
{
    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(keyboard_engine_get_play_owner_track());
    if (keyboard_engine_all_notes_off_matching_tracks(
            active_channel, 1U) == 0U)
        return 0U;
    keyboard_engine_mono_clear();

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, active_channel);
    }

    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
    memset(g_kbd_rec_track_note_count, 0, sizeof(g_kbd_rec_track_note_count));
    return 1U;
}

void keyboard_engine_clear_source_occurrences_silent(void)
{
    memset(g_keyboard_engine_source_occurrence, 0,
           sizeof(g_keyboard_engine_source_occurrence));
}

void keyboard_engine_clear_state_silent(void)
{
    keyboard_engine_mono_clear();
    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
    memset(g_kbd_rec_track_note_count, 0, sizeof(g_kbd_rec_track_note_count));
}

static void keyboard_engine_midi_receive_internal(const uint8_t *msg, size_t len)
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

    if (is_all_notes_off != 0U)
    {
        if (note_fx_pipeline_request_panic() == 0U)
            return;
        keyboard_engine_clear_source_occurrences_silent();
        return;
    }

    if (is_note_on != 0U)
    {
        if (g_keyboard_engine_timed_context_active == 0U)
        {
            seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_EXTERNAL,
                                         channel, note, velocity);
        }
    }
    else if (is_note_off != 0U)
    {
        if (g_keyboard_engine_timed_context_active == 0U)
        {
            seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_EXTERNAL,
                                          channel, note);
        }
    }

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        if ((ui_track_family_is_engine(cfg.family) == 0)
                && (cfg.family != UI_TRACK_FAMILY_EXTERNAL))
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

        if (is_note_on != 0U)
        {
            keyboard_engine_send_note_for_current_context(
                track, note, velocity, 1U, NOTE_EVENT_SOURCE_MIDI);
            continue;
        }
        if (is_note_off != 0U)
        {
            keyboard_engine_send_note_for_current_context(
                track, note, 0U, 0U, NOTE_EVENT_SOURCE_MIDI);
            continue;
        }

    }

}

void keyboard_engine_midi_receive(const uint8_t *msg, size_t len)
{
    keyboard_engine_midi_receive_internal(msg, len);
}

void keyboard_engine_midi_receive_timed(const uint8_t *msg, size_t len,
                                        uint32_t capture_tick,
                                        uint32_t ingress_serial)
{
    g_keyboard_engine_timed_context_active = 1U;
    g_keyboard_engine_capture_tick = capture_tick;
    g_keyboard_engine_ingress_serial = ingress_serial;
    keyboard_engine_midi_receive_internal(msg, len);
    g_keyboard_engine_timed_context_active = 0U;
}
