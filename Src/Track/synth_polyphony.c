#include "Track/synth_polyphony.h"
#include "Track/track_types.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Audio/mixer.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/acid_engine.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "stm32h7xx.h"

typedef struct
{
    uint8_t note;
    uint8_t state;
    uint8_t source;
    uint32_t age;
    uint32_t output_id;
} synth_poly_voice_t;

typedef struct
{
    uint8_t voice_count;
    uint8_t render_voice_count;
    uint16_t renderable_voice_mask;
    float spread;
    uint32_t age_counter;
    uint8_t active;
    uint8_t engine;
    uint8_t most_recent_voice;
    uint8_t slots[SYNTH_POLYPHONY_MAX_VOICES];
    float voice_pan[SYNTH_POLYPHONY_MAX_VOICES];
} synth_poly_track_t;

AUDIO_HOT static synth_poly_track_t
    g_synth_poly[SYNTH_POLYPHONY_TRACK_CAPACITY];
AUDIO_HOT static uint8_t g_synth_slot_owner[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
AUDIO_HOT static synth_poly_voice_t g_synth_voice[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
AUDIO_HOT static uint32_t g_synth_voice_age;
static uint8_t g_synth_displaced_track;
static uint32_t g_synth_displaced_output;

static uint8_t synth_polyphony_find_slot(uint8_t track, uint8_t voice)
{
    return ((track < SYNTH_POLYPHONY_TRACK_CAPACITY)
            && (voice < SYNTH_POLYPHONY_MAX_VOICES))
        ? g_synth_poly[track].slots[voice] : SYNTH_POLYPHONY_NO_VOICE;
}

static void synth_polyphony_release_slot(uint8_t slot)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
        g_synth_slot_owner[slot] = SYNTH_POLYPHONY_NO_VOICE;
}

static uint8_t synth_polyphony_acquire_slot(uint8_t track)
{
    /* Prefer the track-indexed slot, then consume every remaining physical slot. */
    if ((track < SYNTH_POLYPHONY_TRACK_CAPACITY)
            && (g_synth_slot_owner[track] == SYNTH_POLYPHONY_NO_VOICE))
    {
        g_synth_slot_owner[track] = track;
        return track;
    }
    for (uint8_t slot = SYNTH_POLYPHONY_TRACK_CAPACITY;
            slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++slot)
    {
        if (g_synth_slot_owner[slot] == SYNTH_POLYPHONY_NO_VOICE)
        {
            g_synth_slot_owner[slot] = track;
            return slot;
        }
    }
    for (uint8_t slot = 0U; slot < SYNTH_POLYPHONY_TRACK_CAPACITY; ++slot)
    {
        if (g_synth_slot_owner[slot] == SYNTH_POLYPHONY_NO_VOICE)
        {
            g_synth_slot_owner[slot] = track;
            return slot;
        }
    }
    return SYNTH_POLYPHONY_NO_VOICE;
}

static void synth_polyphony_reset_slot(uint8_t slot)
{
    if (slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) return;
    brick6_braids_runtime_all_notes_off(slot);
    brick6_stack_runtime_all_notes_off(slot);
    brick6_wave_runtime_all_notes_off(slot);
    brick6_fm_runtime_all_notes_off(slot);
    brick6_acid_runtime_all_notes_off(slot);
    mixer_synth_voice_slot_reset(slot);
    mod_lfo_v1_poly_voice_reset(slot);
}

static void synth_polyphony_silence_slot(uint8_t slot)
{
    if (slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) return;
    brick6_braids_runtime_all_notes_off(slot);
    brick6_braids_runtime_clear_trigger(slot);
    brick6_stack_runtime_all_notes_off(slot);
    brick6_stack_runtime_clear_trigger(slot);
    brick6_wave_runtime_all_notes_off(slot);
    brick6_wave_runtime_clear_trigger(slot);
    brick6_acid_runtime_all_notes_off(slot);
    mixer_synth_voice_slot_reset(slot);
    mod_lfo_v1_poly_voice_reset(slot);
}

static uint8_t synth_poly_valid_track(uint8_t track)
{
    return (track < SYNTH_POLYPHONY_TRACK_CAPACITY) ? 1U : 0U;
}

static void synth_polyphony_refresh_voice_pan(synth_poly_track_t *poly)
{
    if (poly == NULL) return;
    if (poly->voice_count <= 1U)
    {
        poly->voice_pan[0] = 0.0f;
        return;
    }
    const float step = 2.0f / (float)(poly->voice_count - 1U);
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        poly->voice_pan[voice] = (((float)voice * step) - 1.0f) * poly->spread;
}

static void synth_polyphony_reset_track_slots(uint8_t track)
{
    synth_poly_track_t *const poly = &g_synth_poly[track];
    mixer_track_poly_all_notes_off(track);
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        synth_polyphony_reset_slot(slot);
        if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
    }
}

void synth_polyphony_init(void)
{
    memset(g_synth_poly, 0, sizeof(g_synth_poly));
    memset(g_synth_slot_owner, SYNTH_POLYPHONY_NO_VOICE, sizeof(g_synth_slot_owner));
    memset(g_synth_voice, 0, sizeof(g_synth_voice));
    g_synth_voice_age = 0U;
    g_synth_displaced_track = SYNTH_POLYPHONY_NO_VOICE;
    g_synth_displaced_output = 0U;
    for (uint8_t track = 0U; track < SYNTH_POLYPHONY_TRACK_CAPACITY; ++track)
    {
        g_synth_poly[track].voice_count = 1U;
        g_synth_poly[track].render_voice_count = 0U;
        g_synth_poly[track].renderable_voice_mask = 0U;
        memset(g_synth_poly[track].slots, SYNTH_POLYPHONY_NO_VOICE,
               sizeof(g_synth_poly[track].slots));
    }
}

uint8_t synth_polyphony_set_track_active(uint8_t track, uint8_t active, uint8_t engine)
{
    if ((synth_poly_valid_track(track) == 0U) || (track >= SYNTH_POLYPHONY_TRACK_CAPACITY))
        return 0U;
    synth_poly_track_t *const poly = &g_synth_poly[track];
    if (active == 0U)
    {
        synth_polyphony_all_notes_off(track);
        poly->render_voice_count = 0U;
        poly->renderable_voice_mask = 0U;
        __DMB();
        for (uint8_t voice = 0U;
             voice < SYNTH_POLYPHONY_MAX_VOICES;
             ++voice)
        {
            if (voice >= poly->voice_count)
                break;
            const uint8_t slot = synth_polyphony_find_slot(track, voice);
            synth_polyphony_reset_slot(slot);
            synth_polyphony_release_slot(slot);
            poly->slots[voice] = SYNTH_POLYPHONY_NO_VOICE;
            if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
        }
        poly->active = 0U;
        poly->voice_count = 1U;
        poly->render_voice_count = 0U;
        poly->renderable_voice_mask = 0U;
        memset(poly->slots, SYNTH_POLYPHONY_NO_VOICE, sizeof(poly->slots));
        return 1U;
    }
    if ((poly->active != 0U) && (poly->engine != engine))
    {
        poly->render_voice_count = 0U;
        poly->renderable_voice_mask = 0U;
        __DMB();
        synth_polyphony_reset_track_slots(track);
    }
    if (poly->active == 0U)
    {
        const uint8_t slot = synth_polyphony_acquire_slot(track);
        if (slot == SYNTH_POLYPHONY_NO_VOICE)
            return 0U;
        synth_polyphony_reset_slot(slot);
        memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
        poly->slots[0] = slot;
        poly->voice_count = 1U;
        poly->render_voice_count = 1U;
        poly->renderable_voice_mask = 0U;
        poly->active = 1U;
    }
    poly->engine = engine;
    __DMB();
    poly->render_voice_count = poly->voice_count;
    return 1U;
}

uint8_t synth_polyphony_replace_renderer(uint8_t track, uint8_t engine)
{
    if ((synth_poly_valid_track(track) == 0U)
            || (g_synth_poly[track].active == 0U))
        return 0U;
    synth_poly_track_t *const poly = &g_synth_poly[track];
    poly->render_voice_count = 0U;
    __DMB();
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if (slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            return 0U;
        /* Renderer-local reset only.  Mixer envelopes, poly modulation and
         * output identity remain attached to the physical voice slot. */
        brick6_braids_runtime_all_notes_off(slot);
        brick6_stack_runtime_all_notes_off(slot);
        brick6_wave_runtime_all_notes_off(slot);
        brick6_fm_runtime_all_notes_off(slot);
        brick6_acid_runtime_all_notes_off(slot);
    }
    poly->engine = engine;
    __DMB();
    poly->render_voice_count = poly->voice_count;
    return 1U;
}

uint8_t synth_polyphony_validate_ownership(void)
{
    for (uint8_t track = 0U; track < SYNTH_POLYPHONY_TRACK_CAPACITY; ++track)
    {
        const synth_poly_track_t *const poly = &g_synth_poly[track];
        if (poly->active == 0U) continue;
        if ((poly->voice_count < 1U) || (poly->voice_count > SYNTH_POLYPHONY_MAX_VOICES)) return 0U;
        if ((poly->slots[0] >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                || (g_synth_slot_owner[poly->slots[0]] != track)) return 0U;
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            const uint8_t slot = poly->slots[voice];
            if ((slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                    || (g_synth_slot_owner[slot] != track)) return 0U;
            for (uint8_t other = 0U; other < voice; ++other)
                if (poly->slots[other] == slot) return 0U;
        }
        uint8_t owned = 0U;
        for (uint8_t slot = 0U; slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++slot)
            owned += (g_synth_slot_owner[slot] == track) ? 1U : 0U;
        if (owned != poly->voice_count) return 0U;
    }
    for (uint8_t slot = 0U; slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++slot)
        if ((g_synth_slot_owner[slot] != SYNTH_POLYPHONY_NO_VOICE)
                && ((g_synth_slot_owner[slot] >= SYNTH_POLYPHONY_TRACK_CAPACITY)
                    || (g_synth_poly[g_synth_slot_owner[slot]].active == 0U))) return 0U;
    return 1U;
}

void synth_polyphony_reset_track(uint8_t track)
{
    if ((synth_poly_valid_track(track) == 0U) || (g_synth_poly[track].active == 0U)) return;
    synth_poly_track_t *const poly = &g_synth_poly[track];
    poly->render_voice_count = 0U;
    poly->renderable_voice_mask = 0U;
    __DMB();
    synth_polyphony_reset_track_slots(track);
    __DMB();
    poly->render_voice_count = poly->voice_count;
}

void synth_polyphony_panic(void)
{
    for (uint8_t track = 0U; track < SYNTH_POLYPHONY_TRACK_CAPACITY; ++track)
    {
        synth_poly_track_t *const poly = &g_synth_poly[track];
        if (poly->active == 0U) continue;
        mixer_track_poly_all_notes_off(track);
        poly->render_voice_count = 0U;
        poly->renderable_voice_mask = 0U;
        __DMB();
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            const uint8_t slot = synth_polyphony_find_slot(track, voice);
            synth_polyphony_silence_slot(slot);
            if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
        }
        __DMB();
        poly->render_voice_count = poly->voice_count;
    }
}

uint8_t synth_polyphony_get_track_active(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U) ? g_synth_poly[track].active : 0U;
}

uint8_t synth_polyphony_get_slot(uint8_t track, uint8_t voice)
{
    if ((synth_poly_valid_track(track) == 0U) || (voice >= SYNTH_POLYPHONY_MAX_VOICES))
        return SYNTH_POLYPHONY_NO_VOICE;
    return synth_polyphony_find_slot(track, voice);
}

uint8_t synth_polyphony_get_render_voice_count(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U)
        ? g_synth_poly[track].render_voice_count : 1U;
}

uint16_t synth_polyphony_get_renderable_voice_mask(uint8_t track)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return 0U;
    }
    return g_synth_poly[track].renderable_voice_mask;
}

uint8_t synth_polyphony_get_voice_snapshot(uint8_t track, uint8_t voice,
                                           synth_poly_voice_snapshot_t *out)
{
    if ((out == NULL) || (synth_poly_valid_track(track) == 0U)
            || (voice >= g_synth_poly[track].voice_count))
        return 0U;
    const uint8_t slot = synth_polyphony_find_slot(track, voice);
    if (slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
        return 0U;
    *out = (synth_poly_voice_snapshot_t){
        .note = g_synth_voice[slot].note,
        .state = g_synth_voice[slot].state,
        .source = g_synth_voice[slot].source,
        .output_id = g_synth_voice[slot].output_id
    };
    return 1U;
}

uint8_t synth_polyphony_get_voice_count(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U)
        ? g_synth_poly[track].voice_count : 1U;
}

void synth_polyphony_set_spread(uint8_t track, float spread)
{
    if (synth_poly_valid_track(track) != 0U)
    {
        g_synth_poly[track].spread = (spread < 0.0f) ? 0.0f
            : ((spread > 1.0f) ? 1.0f : spread);
        synth_polyphony_refresh_voice_pan(&g_synth_poly[track]);
    }
}

float synth_polyphony_get_spread(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U) ? g_synth_poly[track].spread : 0.0f;
}

float synth_polyphony_get_voice_pan(uint8_t track, uint8_t voice)
{
    if ((synth_poly_valid_track(track) == 0U)
            || (voice >= g_synth_poly[track].voice_count)) return 0.0f;
    return g_synth_poly[track].voice_pan[voice];
}

static uint8_t synth_polyphony_voice_index_for_slot(uint8_t track, uint8_t slot)
{
    if (track >= SYNTH_POLYPHONY_TRACK_CAPACITY) return SYNTH_POLYPHONY_NO_VOICE;
    for (uint8_t voice = 0U; voice < g_synth_poly[track].voice_count; ++voice)
        if (g_synth_poly[track].slots[voice] == slot) return voice;
    return SYNTH_POLYPHONY_NO_VOICE;
}

static void synth_polyphony_detach_secondary(uint8_t track, uint8_t voice)
{
    synth_poly_track_t *const poly = &g_synth_poly[track];
    if ((voice == 0U) || (voice >= poly->voice_count)) return;
    const uint8_t old_count = poly->voice_count;
    poly->render_voice_count = 0U;
    __DMB();
    uint16_t mask = 0U;
    for (uint8_t i = 0U, out = 0U; i < old_count; ++i)
    {
        if (i == voice) continue;
        poly->slots[out] = poly->slots[i];
        if ((poly->renderable_voice_mask & (uint16_t)(1UL << i)) != 0U)
            mask |= (uint16_t)(1UL << out);
        ++out;
    }
    poly->voice_count = (uint8_t)(old_count - 1U);
    poly->slots[old_count - 1U] = SYNTH_POLYPHONY_NO_VOICE;
    poly->renderable_voice_mask = mask;
    poly->most_recent_voice = 0U;
    synth_polyphony_refresh_voice_pan(poly);
    __DMB();
    poly->render_voice_count = poly->voice_count;
}

static uint8_t synth_polyphony_append_global_voice(uint8_t track)
{
    synth_poly_track_t *const poly = &g_synth_poly[track];
    if (poly->voice_count >= SYNTH_POLYPHONY_MAX_VOICES)
        return SYNTH_POLYPHONY_NO_VOICE;
    uint8_t slot = synth_polyphony_acquire_slot(track);
    if (slot == SYNTH_POLYPHONY_NO_VOICE)
    {
        for (uint8_t candidate = 0U;
             candidate < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++candidate)
        {
            const uint8_t owner = g_synth_slot_owner[candidate];
            const uint8_t voice = synth_polyphony_voice_index_for_slot(owner, candidate);
            if ((owner < SYNTH_POLYPHONY_TRACK_CAPACITY) && (voice != 0U)
                    && (voice != SYNTH_POLYPHONY_NO_VOICE)
                    && (g_synth_voice[candidate].state == SYNTH_POLY_VOICE_FREE))
            {
                synth_polyphony_detach_secondary(owner, voice);
                g_synth_slot_owner[candidate] = track;
                slot = candidate;
                break;
            }
        }
    }
    if (slot == SYNTH_POLYPHONY_NO_VOICE)
    {
        uint8_t victim_slot = SYNTH_POLYPHONY_NO_VOICE;
        uint32_t oldest = UINT32_MAX;
        static const uint8_t wanted_state[] = {
            SYNTH_POLY_VOICE_RELEASE, SYNTH_POLY_VOICE_HELD
        };
        for (uint8_t pass = 0U; pass < 2U; ++pass)
        {
            const uint8_t wanted = wanted_state[pass];
            for (uint8_t candidate = 0U;
                 candidate < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++candidate)
            {
                const uint8_t owner = g_synth_slot_owner[candidate];
                const uint8_t voice = synth_polyphony_voice_index_for_slot(owner, candidate);
                if ((owner >= SYNTH_POLYPHONY_TRACK_CAPACITY) || (voice == 0U)
                        || (g_synth_voice[candidate].state != wanted)) continue;
                if ((victim_slot == SYNTH_POLYPHONY_NO_VOICE)
                        || ((int32_t)(g_synth_voice[candidate].age - oldest) < 0))
                {
                    victim_slot = candidate;
                    oldest = g_synth_voice[candidate].age;
                }
            }
            if (victim_slot != SYNTH_POLYPHONY_NO_VOICE) break;
        }
        if (victim_slot == SYNTH_POLYPHONY_NO_VOICE)
            return SYNTH_POLYPHONY_NO_VOICE;
        const uint8_t owner = g_synth_slot_owner[victim_slot];
        const uint8_t voice = synth_polyphony_voice_index_for_slot(owner, victim_slot);
        if (g_synth_voice[victim_slot].state == SYNTH_POLY_VOICE_HELD)
        {
            g_synth_displaced_track = owner;
            g_synth_displaced_output = g_synth_voice[victim_slot].output_id;
        }
        synth_polyphony_detach_secondary(owner, voice);
        synth_polyphony_silence_slot(victim_slot);
        g_synth_slot_owner[victim_slot] = track;
        memset(&g_synth_voice[victim_slot], 0, sizeof(g_synth_voice[victim_slot]));
        slot = victim_slot;
    }
    else
    {
        synth_polyphony_reset_slot(slot);
        memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
    }
    const uint8_t voice = poly->voice_count++;
    poly->slots[voice] = slot;
    poly->render_voice_count = poly->voice_count;
    synth_polyphony_refresh_voice_pan(poly);
    return voice;
}

uint8_t synth_polyphony_note_on_output_from(uint8_t track, uint8_t note,
                                            synth_poly_source_t source,
                                            uint32_t output_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (output_id == 0U))
    {
        return SYNTH_POLYPHONY_NO_VOICE;
    }
    synth_poly_track_t *const poly = &g_synth_poly[track];
    g_synth_displaced_track = SYNTH_POLYPHONY_NO_VOICE;
    g_synth_displaced_output = 0U;
    uint8_t selected = SYNTH_POLYPHONY_NO_VOICE;
    uint32_t oldest = UINT32_MAX;
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_FREE))
        {
            selected = voice;
            break;
        }
    }
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
        selected = synth_polyphony_append_global_voice(track);
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
    {
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            const uint8_t slot = synth_polyphony_find_slot(track, voice);
            if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                    && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_RELEASE)
                    && ((selected == SYNTH_POLYPHONY_NO_VOICE)
                        || ((int32_t)(g_synth_voice[slot].age - oldest) < 0)))
            {
                oldest = g_synth_voice[slot].age;
                selected = voice;
            }
        }
    }
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
    {
        oldest = UINT32_MAX;
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            const uint8_t slot = synth_polyphony_find_slot(track, voice);
            if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                    && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                    && ((selected == SYNTH_POLYPHONY_NO_VOICE)
                        || ((int32_t)(g_synth_voice[slot].age - oldest) < 0)))
            {
                oldest = g_synth_voice[slot].age;
                selected = voice;
            }
        }
    }
    /* SEQ owns logical lifetimes independently from the configured AUDIO
     * polyphony.  Once every physical voice is HELD, reuse the oldest one;
     * the adapter retires its output identity before publishing the new one. */
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
        return SYNTH_POLYPHONY_NO_VOICE;
    const uint8_t selected_slot = synth_polyphony_find_slot(track, selected);
    if (selected_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) return SYNTH_POLYPHONY_NO_VOICE;
    synth_poly_voice_t *const target = &g_synth_voice[selected_slot];
    target->note = note;
    target->state = SYNTH_POLY_VOICE_HELD;
    target->source = (uint8_t)source;
    target->output_id = output_id;
    target->age = ++g_synth_voice_age;
    poly->most_recent_voice = selected;
    __DMB();
    poly->renderable_voice_mask |= (uint16_t)(1UL << selected);
    return selected;
}

uint8_t synth_polyphony_take_displaced_output(uint8_t *out_track,
                                              uint32_t *out_output_id)
{
    if ((out_track == NULL) || (out_output_id == NULL)
            || (g_synth_displaced_track >= SYNTH_POLYPHONY_TRACK_CAPACITY)
            || (g_synth_displaced_output == 0U))
        return 0U;
    *out_track = g_synth_displaced_track;
    *out_output_id = g_synth_displaced_output;
    g_synth_displaced_track = SYNTH_POLYPHONY_NO_VOICE;
    g_synth_displaced_output = 0U;
    return 1U;
}

uint8_t synth_polyphony_get_most_recent_renderable_voice(uint8_t track)
{
    if (synth_poly_valid_track(track) == 0U) return SYNTH_POLYPHONY_NO_VOICE;
    const synth_poly_track_t *const poly = &g_synth_poly[track];
    const uint8_t recent = poly->most_recent_voice;
    if ((recent < poly->render_voice_count)
            && ((poly->renderable_voice_mask & (uint16_t)(1UL << recent)) != 0U))
    {
        return recent;
    }
    return (poly->renderable_voice_mask != 0U)
        ? (uint8_t)__builtin_ctz((unsigned int)poly->renderable_voice_mask)
        : SYNTH_POLYPHONY_NO_VOICE;
}

uint8_t synth_polyphony_note_off_output_from(uint8_t track,
                                             synth_poly_source_t source,
                                             uint32_t output_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (output_id == 0U))
        return SYNTH_POLYPHONY_NO_VOICE;

    synth_poly_track_t *const poly = &g_synth_poly[track];
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && (g_synth_voice[slot].output_id == output_id))
        {
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
            return voice;
        }
    }
    return SYNTH_POLYPHONY_NO_VOICE;
}

uint8_t synth_polyphony_output_is_active(uint8_t track,
                                         synth_poly_source_t source,
                                         uint32_t output_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (output_id == 0U))
        return 0U;

    const synth_poly_track_t *const poly = &g_synth_poly[track];
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && (g_synth_voice[slot].output_id == output_id))
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t synth_polyphony_voice_for_output(uint8_t track,
                                         synth_poly_source_t source,
                                         uint32_t output_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (output_id == 0U))
        return SYNTH_POLYPHONY_NO_VOICE;
    const synth_poly_track_t *const poly = &g_synth_poly[track];
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && (g_synth_voice[slot].output_id == output_id))
            return voice;
    }
    return SYNTH_POLYPHONY_NO_VOICE;
}

uint8_t synth_polyphony_bind_held_output(uint8_t track,
                                         uint8_t note,
                                         synth_poly_source_t source,
                                         uint32_t output_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (output_id == 0U))
        return 0U;
    if (synth_polyphony_voice_for_output(track, source, output_id)
            != SYNTH_POLYPHONY_NO_VOICE)
        return 1U;
    return synth_polyphony_note_on_output_from(track, note, source, output_id)
        != SYNTH_POLYPHONY_NO_VOICE;
}

void synth_polyphony_all_notes_off(uint8_t track)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return;
    }
    for (uint8_t voice = 0U; voice < g_synth_poly[track].voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD))
        {
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
        }
    }
}

uint8_t synth_polyphony_voice_is_renderable(uint8_t track, uint8_t voice)
{
    if ((synth_poly_valid_track(track) == 0U)
            || (voice >= g_synth_poly[track].render_voice_count)) return 0U;
    return (uint8_t)((g_synth_poly[track].renderable_voice_mask
            & (uint16_t)(1UL << voice)) != 0U);
}

void synth_polyphony_voice_release_complete(uint8_t track, uint8_t voice)
{
    if ((synth_poly_valid_track(track) != 0U) && (voice < SYNTH_POLYPHONY_MAX_VOICES))
    {
        uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                || (g_synth_voice[slot].state != SYNTH_POLY_VOICE_RELEASE)) return;
        g_synth_voice[slot].state = SYNTH_POLY_VOICE_FREE;
        mod_lfo_v1_poly_voice_reset(slot);
        __DMB();
        g_synth_poly[track].renderable_voice_mask &= (uint16_t)~(1UL << voice);
        while ((g_synth_poly[track].render_voice_count
                    > g_synth_poly[track].voice_count)
                && ((slot = synth_polyphony_find_slot(track,
                        (uint8_t)(g_synth_poly[track].render_voice_count - 1U)))
                    < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_FREE))
        {
            g_synth_poly[track].render_voice_count--;
        }
    }
}
