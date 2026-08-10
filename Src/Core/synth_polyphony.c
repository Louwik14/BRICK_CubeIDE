#include "Core/synth_polyphony.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Audio/mixer.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "stm32h7xx.h"

typedef struct
{
    uint8_t note;
    uint8_t state;
    uint8_t source;
    uint32_t age;
    uint32_t occurrence_id;
} synth_poly_voice_t;

typedef struct
{
    uint8_t voice_count;
    uint8_t render_voice_count;
    uint8_t renderable_voice_mask;
    float spread;
    uint32_t age_counter;
    uint8_t active;
    uint8_t engine;
    uint8_t slots[SYNTH_POLYPHONY_MAX_VOICES];
    float voice_pan[SYNTH_POLYPHONY_MAX_VOICES];
} synth_poly_track_t;

AUDIO_HOT static synth_poly_track_t g_synth_poly[SEQ_TRACK_COUNT];
AUDIO_HOT static uint8_t g_synth_slot_owner[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
AUDIO_HOT static synth_poly_voice_t g_synth_voice[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];

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
    brick6_braids_runtime_reset_instance(slot);
    brick6_stack_runtime_reset_instance(slot);
    brick6_wave_runtime_reset_instance(slot);
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
    mixer_synth_voice_slot_reset(slot);
    mod_lfo_v1_poly_voice_reset(slot);
}

static uint8_t synth_poly_valid_track(uint8_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
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
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
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
        while (poly->voice_count > 0U)
        {
            const uint8_t slot = synth_polyphony_find_slot(track, (uint8_t)(poly->voice_count - 1U));
            synth_polyphony_reset_slot(slot);
            synth_polyphony_release_slot(slot);
            poly->slots[poly->voice_count - 1U] = SYNTH_POLYPHONY_NO_VOICE;
            if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
            poly->voice_count--;
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

uint8_t synth_polyphony_set_voice_count(uint8_t track, uint8_t count)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return 0U;
    }
    count = (count < 1U) ? 1U : ((count > SYNTH_POLYPHONY_MAX_VOICES)
        ? SYNTH_POLYPHONY_MAX_VOICES : count);
    if (g_synth_poly[track].active == 0U)
        return 0U;
    const uint8_t maximum = synth_polyphony_get_available_for_track(track);
    if (count > maximum)
        count = maximum;
    g_synth_poly[track].render_voice_count = 0U;
    g_synth_poly[track].renderable_voice_mask = 0U;
    __DMB();
    synth_polyphony_reset_track_slots(track);
    for (uint8_t voice = g_synth_poly[track].voice_count; voice < count; ++voice)
    {
        const uint8_t slot = synth_polyphony_acquire_slot(track);
        if (slot == SYNTH_POLYPHONY_NO_VOICE) { count = voice; break; }
        g_synth_poly[track].slots[voice] = slot;
        synth_polyphony_reset_slot(slot);
        memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
    }
    uint8_t owned = g_synth_poly[track].voice_count;
    while (owned > count)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, (uint8_t)(owned - 1U));
        synth_polyphony_reset_slot(slot);
        synth_polyphony_release_slot(slot);
        g_synth_poly[track].slots[owned - 1U] = SYNTH_POLYPHONY_NO_VOICE;
        if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            memset(&g_synth_voice[slot], 0, sizeof(g_synth_voice[slot]));
        owned--;
    }
    g_synth_poly[track].voice_count = count;
    synth_polyphony_refresh_voice_pan(&g_synth_poly[track]);
    __DMB();
    g_synth_poly[track].render_voice_count = count;
    return count;
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

uint8_t synth_polyphony_get_free_count(void)
{
    uint8_t free_count = 0U;
    for (uint8_t slot = 0U; slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++slot)
        free_count += (g_synth_slot_owner[slot] == SYNTH_POLYPHONY_NO_VOICE) ? 1U : 0U;
    return free_count;
}

uint8_t synth_polyphony_get_available_for_track(uint8_t track)
{
    const uint8_t owned = ((synth_poly_valid_track(track) != 0U)
            && (g_synth_poly[track].active != 0U)) ? g_synth_poly[track].voice_count : 0U;
    uint8_t maximum = (uint8_t)(owned + synth_polyphony_get_free_count());
    return (maximum > SYNTH_POLYPHONY_MAX_VOICES) ? SYNTH_POLYPHONY_MAX_VOICES : maximum;
}

uint8_t synth_polyphony_get_render_voice_count(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U)
        ? g_synth_poly[track].render_voice_count : 1U;
}

uint8_t synth_polyphony_get_renderable_voice_mask(uint8_t track)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return 0U;
    }
    return g_synth_poly[track].renderable_voice_mask;
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

uint8_t synth_polyphony_note_on(uint8_t track, uint8_t note)
{
    return synth_polyphony_note_on_from(track, note, SYNTH_POLY_SOURCE_MANUAL);
}

uint8_t synth_polyphony_note_on_from(uint8_t track, uint8_t note, synth_poly_source_t source)
{
    return synth_polyphony_note_on_occurrence_from(track, note, source, 0U);
}

uint8_t synth_polyphony_note_on_occurrence_from(uint8_t track, uint8_t note,
                                                synth_poly_source_t source,
                                                uint32_t occurrence_id)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return SYNTH_POLYPHONY_NO_VOICE;
    }
    synth_poly_track_t *const poly = &g_synth_poly[track];
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
    {
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            const uint8_t slot = synth_polyphony_find_slot(track, voice);
            if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                    && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_RELEASE)
                    && (g_synth_voice[slot].age < oldest))
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
                    && (g_synth_voice[slot].age < oldest))
            {
                oldest = g_synth_voice[slot].age;
                selected = voice;
            }
        }
    }
    const uint8_t selected_slot = synth_polyphony_find_slot(track, selected);
    if (selected_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) return SYNTH_POLYPHONY_NO_VOICE;
    synth_poly_voice_t *const target = &g_synth_voice[selected_slot];
    target->note = note;
    target->state = SYNTH_POLY_VOICE_HELD;
    target->source = (uint8_t)source;
    target->occurrence_id = occurrence_id;
    target->age = ++poly->age_counter;
    __DMB();
    poly->renderable_voice_mask |= (uint8_t)(1U << selected);
    return selected;
}

uint8_t synth_polyphony_note_off(uint8_t track, uint8_t note)
{
    return synth_polyphony_note_off_from(track, note, SYNTH_POLY_SOURCE_MANUAL);
}

uint8_t synth_polyphony_note_off_from(uint8_t track, uint8_t note, synth_poly_source_t source)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return SYNTH_POLYPHONY_NO_VOICE;
    }
    synth_poly_track_t *const poly = &g_synth_poly[track];
    uint8_t selected = SYNTH_POLYPHONY_NO_VOICE;
    uint32_t newest = 0U;
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].note == note)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && ((selected == SYNTH_POLYPHONY_NO_VOICE)
                    || (g_synth_voice[slot].age >= newest)))
        {
            selected = voice;
            newest = g_synth_voice[slot].age;
        }
    }
    if (selected != SYNTH_POLYPHONY_NO_VOICE)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, selected);
        if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
    }
    return selected;
}

uint8_t synth_polyphony_note_off_occurrence_from(uint8_t track,
                                                 synth_poly_source_t source,
                                                 uint32_t occurrence_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (occurrence_id == 0U))
        return SYNTH_POLYPHONY_NO_VOICE;

    synth_poly_track_t *const poly = &g_synth_poly[track];
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && (g_synth_voice[slot].occurrence_id == occurrence_id))
        {
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
            return voice;
        }
    }
    return SYNTH_POLYPHONY_NO_VOICE;
}

uint8_t synth_polyphony_occurrence_is_active(uint8_t track,
                                             synth_poly_source_t source,
                                             uint32_t occurrence_id)
{
    if ((synth_poly_valid_track(track) == 0U) || (occurrence_id == 0U))
        return 0U;

    const synth_poly_track_t *const poly = &g_synth_poly[track];
    for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source)
                && (g_synth_voice[slot].occurrence_id == occurrence_id))
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t synth_polyphony_release_source(uint8_t track,
                                      synth_poly_source_t source,
                                      synth_poly_release_t *out,
                                      uint8_t capacity)
{
    if ((synth_poly_valid_track(track) == 0U) || (out == NULL) || (capacity == 0U))
    {
        return 0U;
    }

    synth_poly_track_t *const poly = &g_synth_poly[track];
    uint8_t count = 0U;
    for (uint8_t voice = 0U; (voice < poly->voice_count) && (count < capacity); ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD)
                && (g_synth_voice[slot].source == (uint8_t)source))
        {
            out[count].voice = voice;
            out[count].note = g_synth_voice[slot].note;
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
            count++;
        }
    }
    return count;
}

uint8_t synth_polyphony_release_all(uint8_t track,
                                   synth_poly_release_t *out,
                                   uint8_t capacity)
{
    if ((synth_poly_valid_track(track) == 0U) || (out == NULL) || (capacity == 0U))
    {
        return 0U;
    }

    synth_poly_track_t *const poly = &g_synth_poly[track];
    uint8_t count = 0U;
    for (uint8_t voice = 0U; (voice < poly->voice_count) && (count < capacity); ++voice)
    {
        const uint8_t slot = synth_polyphony_find_slot(track, voice);
        if ((slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                && (g_synth_voice[slot].state == SYNTH_POLY_VOICE_HELD))
        {
            out[count].voice = voice;
            out[count].note = g_synth_voice[slot].note;
            g_synth_voice[slot].state = SYNTH_POLY_VOICE_RELEASE;
            count++;
        }
    }
    return count;
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
            & (uint8_t)(1U << voice)) != 0U);
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
        g_synth_poly[track].renderable_voice_mask &= (uint8_t)~(1U << voice);
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
