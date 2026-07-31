#include "Core/synth_polyphony.h"

#include <string.h>

#include "Storage/memory_layout.h"

typedef struct
{
    uint8_t note;
    uint8_t state;
    uint8_t source;
    uint32_t age;
} synth_poly_voice_t;

typedef struct
{
    synth_poly_voice_t voice[SYNTH_POLYPHONY_MAX_VOICES];
    uint8_t voice_count;
    uint8_t render_voice_count;
    float spread;
    uint32_t age_counter;
} synth_poly_track_t;

SEQ_STATE_D2 static synth_poly_track_t g_synth_poly[SEQ_TRACK_COUNT];

static uint8_t synth_poly_valid_track(uint8_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

void synth_polyphony_init(void)
{
    memset(g_synth_poly, 0, sizeof(g_synth_poly));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_synth_poly[track].voice_count = 1U;
        g_synth_poly[track].render_voice_count = 1U;
    }
}

void synth_polyphony_set_voice_count(uint8_t track, uint8_t count)
{
    if (synth_poly_valid_track(track) == 0U)
    {
        return;
    }
    count = (count < 1U) ? 1U : ((count > SYNTH_POLYPHONY_MAX_VOICES)
        ? SYNTH_POLYPHONY_MAX_VOICES : count);
    const uint8_t previous_count = g_synth_poly[track].render_voice_count;
    synth_polyphony_all_notes_off(track);
    g_synth_poly[track].voice_count = count;
    g_synth_poly[track].render_voice_count =
        (previous_count > count) ? previous_count : count;
}

uint8_t synth_polyphony_get_render_voice_count(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U)
        ? g_synth_poly[track].render_voice_count : 1U;
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
    }
}

float synth_polyphony_get_spread(uint8_t track)
{
    return (synth_poly_valid_track(track) != 0U) ? g_synth_poly[track].spread : 0.0f;
}

float synth_polyphony_get_voice_pan(uint8_t track, uint8_t voice)
{
    const uint8_t count = synth_polyphony_get_voice_count(track);
    if ((voice >= count) || (count <= 1U))
    {
        return 0.0f;
    }
    return ((((float)voice / (float)(count - 1U)) * 2.0f) - 1.0f)
        * synth_polyphony_get_spread(track);
}

uint8_t synth_polyphony_note_on(uint8_t track, uint8_t note)
{
    return synth_polyphony_note_on_from(track, note, SYNTH_POLY_SOURCE_MANUAL);
}

uint8_t synth_polyphony_note_on_from(uint8_t track, uint8_t note, synth_poly_source_t source)
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
        if (poly->voice[voice].state == SYNTH_POLY_VOICE_FREE)
        {
            selected = voice;
            break;
        }
    }
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
    {
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            if ((poly->voice[voice].state == SYNTH_POLY_VOICE_RELEASE)
                    && (poly->voice[voice].age < oldest))
            {
                oldest = poly->voice[voice].age;
                selected = voice;
            }
        }
    }
    if (selected == SYNTH_POLYPHONY_NO_VOICE)
    {
        oldest = UINT32_MAX;
        for (uint8_t voice = 0U; voice < poly->voice_count; ++voice)
        {
            if (poly->voice[voice].age < oldest)
            {
                oldest = poly->voice[voice].age;
                selected = voice;
            }
        }
    }
    synth_poly_voice_t *const target = &poly->voice[selected];
    target->note = note;
    target->state = SYNTH_POLY_VOICE_HELD;
    target->source = (uint8_t)source;
    target->age = ++poly->age_counter;
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
        if ((poly->voice[voice].state == SYNTH_POLY_VOICE_HELD)
                && (poly->voice[voice].note == note)
                && (poly->voice[voice].source == (uint8_t)source)
                && ((selected == SYNTH_POLYPHONY_NO_VOICE)
                    || (poly->voice[voice].age >= newest)))
        {
            selected = voice;
            newest = poly->voice[voice].age;
        }
    }
    if (selected != SYNTH_POLYPHONY_NO_VOICE)
    {
        poly->voice[selected].state = SYNTH_POLY_VOICE_RELEASE;
    }
    return selected;
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
        if ((poly->voice[voice].state == SYNTH_POLY_VOICE_HELD)
                && (poly->voice[voice].source == (uint8_t)source))
        {
            out[count].voice = voice;
            out[count].note = poly->voice[voice].note;
            poly->voice[voice].state = SYNTH_POLY_VOICE_RELEASE;
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
        if (poly->voice[voice].state == SYNTH_POLY_VOICE_HELD)
        {
            out[count].voice = voice;
            out[count].note = poly->voice[voice].note;
            poly->voice[voice].state = SYNTH_POLY_VOICE_RELEASE;
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
    for (uint8_t voice = 0U; voice < SYNTH_POLYPHONY_MAX_VOICES; ++voice)
    {
        if (g_synth_poly[track].voice[voice].state == SYNTH_POLY_VOICE_HELD)
        {
            g_synth_poly[track].voice[voice].state = SYNTH_POLY_VOICE_RELEASE;
        }
    }
}

uint8_t synth_polyphony_voice_is_renderable(uint8_t track, uint8_t voice)
{
    return (uint8_t)((synth_poly_valid_track(track) != 0U)
        && (voice < g_synth_poly[track].render_voice_count)
        && (g_synth_poly[track].voice[voice].state != SYNTH_POLY_VOICE_FREE));
}

void synth_polyphony_voice_release_complete(uint8_t track, uint8_t voice)
{
    if ((synth_poly_valid_track(track) != 0U)
            && (voice < SYNTH_POLYPHONY_MAX_VOICES)
            && (g_synth_poly[track].voice[voice].state == SYNTH_POLY_VOICE_RELEASE))
    {
        g_synth_poly[track].voice[voice].state = SYNTH_POLY_VOICE_FREE;
        while ((g_synth_poly[track].render_voice_count
                    > g_synth_poly[track].voice_count)
                && (g_synth_poly[track]
                        .voice[g_synth_poly[track].render_voice_count - 1U]
                        .state == SYNTH_POLY_VOICE_FREE))
        {
            g_synth_poly[track].render_voice_count--;
        }
    }
}
