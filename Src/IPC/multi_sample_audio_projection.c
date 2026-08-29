#include "IPC/multi_sample_audio_projection.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t ready;
    uint8_t reserved;
    uint16_t first_zone_id;
    uint16_t zone_count;
    uint16_t first_sample_id;
    uint16_t sample_count;
} multi_audio_instrument_t;

typedef struct
{
    uint8_t note_low;
    uint8_t note_high;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t root_note;
    uint8_t reserved;
    uint16_t multi_sample_id;
} multi_audio_zone_t;

D2_IPC static multi_audio_instrument_t
    g_audio_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
AUDIO_SHARED_MULTI_SDRAM static multi_audio_zone_t g_audio_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
AUDIO_SHARED_MULTI_SDRAM static multi_sample_audio_source_t
    g_audio_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];

void multi_sample_audio_projection_init(void)
{
    memset(g_audio_instruments, 0, sizeof(g_audio_instruments));
    memset(g_audio_zones, 0, sizeof(g_audio_zones));
    memset(g_audio_samples, 0, sizeof(g_audio_samples));
    __DMB();
}

void multi_sample_audio_projection_withdraw(uint16_t instrument_id)
{
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
        return;
    g_audio_instruments[instrument_id].ready = 0U;
    __DMB();
    g_audio_instruments[instrument_id].sequence++;
    __DMB();
}

uint8_t multi_sample_audio_projection_publish(uint16_t instrument_id)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((instrument == NULL) || (instrument->state != MULTI_SAMPLE_INSTRUMENT_READY)
        || ((uint32_t)instrument->first_zone_id + instrument->zone_count
            > MULTI_SAMPLE_POOL_MAX_ZONES)
        || ((uint32_t)instrument->first_sample_id + instrument->sample_count
            > MULTI_SAMPLE_POOL_MAX_SAMPLES))
        return 0U;

    multi_audio_instrument_t *const dst = &g_audio_instruments[instrument_id];
    dst->ready = 0U;
    __DMB();
    for (uint32_t i = instrument->first_sample_id;
         i < (uint32_t)instrument->first_sample_id + instrument->sample_count; ++i)
    {
        const multi_sample_desc_t *const s = multi_sample_pool_get_sample((uint16_t)i);
        if ((s == NULL) || (s->instrument_id != instrument_id))
            return 0U;
        g_audio_samples[i] = (multi_sample_audio_source_t){
            .multi_sample_id = (uint16_t)i, .instrument_id = instrument_id,
            .root_note = s->root_note, .vel_low = s->vel_low, .vel_high = s->vel_high,
            .total_frames = s->total_frames, .data_offset = s->data_offset,
            .data_size = s->data_size, .sample_rate = s->sample_rate,
            .registration_epoch = s->registration_epoch, .loop_begin = s->loop_begin,
            .loop_end = s->loop_end, .format = s->format, .channels = s->channels,
            .bits_per_sample = s->bits_per_sample, .block_align = s->block_align,
            .stride_floats = s->stride_floats, .frames_per_page = s->frames_per_page,
            .has_loop = s->has_loop
        };
    }

    /* The pool keeps zones private; resolve each zone indirectly is not
     * sufficient to preserve overlapping velocity-layer semantics.  Export
     * the compact immutable zone through the dedicated accessor. */
    for (uint32_t i = instrument->first_zone_id;
         i < (uint32_t)instrument->first_zone_id + instrument->zone_count; ++i)
    {
        multi_sample_zone_t z;
        if (multi_sample_pool_copy_zone((uint16_t)i, &z) == 0U)
            return 0U;
        g_audio_zones[i] = (multi_audio_zone_t){ z.note_low, z.note_high,
            z.vel_low, z.vel_high, z.root_note, 0U, z.multi_sample_id };
    }
    dst->first_zone_id = instrument->first_zone_id;
    dst->zone_count = instrument->zone_count;
    dst->first_sample_id = instrument->first_sample_id;
    dst->sample_count = instrument->sample_count;
    dst->sequence++;
    __DMB();
    dst->ready = 1U;
    __DMB();
    return 1U;
}

uint8_t multi_sample_audio_projection_is_ready(uint16_t instrument_id)
{
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
        return 0U;
    __DMB();
    __DMB();
    return g_audio_instruments[instrument_id].ready;
}

static uint8_t abs_delta(uint8_t a, uint8_t b)
{
    return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

uint8_t multi_sample_audio_projection_resolve(uint16_t instrument_id,
                                              uint8_t note,
                                              uint8_t velocity,
                                              multi_sample_audio_source_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
        return 0U;
    __DMB();
    const multi_audio_instrument_t snap = g_audio_instruments[instrument_id];
    __DMB();
    if (snap.ready == 0U) return 0U;

    __DMB();

    const multi_audio_zone_t *best = NULL;
    const multi_audio_zone_t *fallback = NULL;
    uint16_t best_zone = MULTI_SAMPLE_POOL_INVALID_ID;
    uint8_t best_delta = UINT8_MAX, fallback_delta = UINT8_MAX;
    for (uint32_t i = snap.first_zone_id; i < (uint32_t)snap.first_zone_id + snap.zone_count; ++i)
    {
        const multi_audio_zone_t *const z = &g_audio_zones[i];
        if ((note < z->note_low) || (note > z->note_high)) continue;
        const uint8_t d = abs_delta(z->root_note, note);
        if ((fallback == NULL) || (d < fallback_delta)) { fallback = z; fallback_delta = d; }
        if ((velocity >= z->vel_low) && (velocity <= z->vel_high))
        {
            if ((best == NULL) || (d < best_delta)) { best = z; best_zone = (uint16_t)i; best_delta = d; }
        }
    }
    const uint8_t used_fallback = (best == NULL) ? 1U : 0U;
    if (best == NULL) { best = fallback; if (best != NULL) best_zone = (uint16_t)(best - g_audio_zones); }
    if ((best == NULL) || (best->multi_sample_id >= MULTI_SAMPLE_POOL_MAX_SAMPLES)) return 0U;
    uint8_t layer_count = 0U;
    for (uint32_t i = snap.first_zone_id; i < (uint32_t)snap.first_zone_id + snap.zone_count; ++i)
    {
        const multi_audio_zone_t *const z = &g_audio_zones[i];
        if ((note >= z->note_low) && (note <= z->note_high) && (z->root_note == best->root_note)
            && (layer_count != UINT8_MAX)) layer_count++;
    }
    if ((used_fallback != 0U) && (layer_count != 1U)) return 0U;
    *out = g_audio_samples[best->multi_sample_id];
    out->zone_id = best_zone;
    out->root_note = best->root_note;
    out->pitch_semitones = (int8_t)((int16_t)note - (int16_t)best->root_note);
    out->vel_low = best->vel_low;
    out->vel_high = best->vel_high;
    out->velocity_layer_count_for_note = layer_count;
    out->zone_is_single_velocity_layer = (layer_count == 1U) ? 1U : 0U;
    __DMB();
    return (snap.sequence == g_audio_instruments[instrument_id].sequence
            && g_audio_instruments[instrument_id].ready != 0U) ? 1U : 0U;
}
