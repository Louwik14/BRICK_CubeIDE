#include "IPC/multi_sample_audio_projection_contract.h"
#include "Audio/multi_sample_audio_projection_audio.h"

#include <string.h>

#include "stm32h7xx_hal.h"

uint8_t multi_sample_audio_projection_is_ready(uint16_t instrument_id)
{
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS) return 0U;
    __DMB();
    return g_multi_audio_instruments[instrument_id].ready;
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
    const multi_audio_instrument_t snap = g_multi_audio_instruments[instrument_id];
    __DMB();
    if (snap.ready == 0U) return 0U;

    const multi_audio_zone_t *best = NULL;
    const multi_audio_zone_t *fallback = NULL;
    uint16_t best_zone = MULTI_SAMPLE_POOL_INVALID_ID;
    uint8_t best_delta = UINT8_MAX, fallback_delta = UINT8_MAX;
    for (uint32_t i = snap.first_zone_id;
         i < (uint32_t)snap.first_zone_id + snap.zone_count; ++i)
    {
        const multi_audio_zone_t *const z = &g_multi_audio_zones[i];
        if ((note < z->note_low) || (note > z->note_high)) continue;
        const uint8_t d = abs_delta(z->root_note, note);
        if ((fallback == NULL) || (d < fallback_delta))
        {
            fallback = z;
            fallback_delta = d;
        }
        if ((velocity >= z->vel_low) && (velocity <= z->vel_high)
            && ((best == NULL) || (d < best_delta)))
        {
            best = z;
            best_zone = (uint16_t)i;
            best_delta = d;
        }
    }
    const uint8_t used_fallback = (best == NULL) ? 1U : 0U;
    if (best == NULL)
    {
        best = fallback;
        if (best != NULL) best_zone = (uint16_t)(best - g_multi_audio_zones);
    }
    if ((best == NULL) || (best->multi_sample_id >= MULTI_SAMPLE_POOL_MAX_SAMPLES))
        return 0U;
    uint8_t layer_count = 0U;
    for (uint32_t i = snap.first_zone_id;
         i < (uint32_t)snap.first_zone_id + snap.zone_count; ++i)
    {
        const multi_audio_zone_t *const z = &g_multi_audio_zones[i];
        if ((note >= z->note_low) && (note <= z->note_high)
            && (z->root_note == best->root_note) && (layer_count != UINT8_MAX))
            layer_count++;
    }
    if ((used_fallback != 0U) && (layer_count != 1U)) return 0U;
    *out = g_multi_audio_samples[best->multi_sample_id];
    out->zone_id = best_zone;
    out->root_note = best->root_note;
    out->pitch_semitones = (int8_t)((int16_t)note - (int16_t)best->root_note);
    out->vel_low = best->vel_low;
    out->vel_high = best->vel_high;
    out->velocity_layer_count_for_note = layer_count;
    out->zone_is_single_velocity_layer = (layer_count == 1U) ? 1U : 0U;
    __DMB();
    return (snap.sequence == g_multi_audio_instruments[instrument_id].sequence
            && g_multi_audio_instruments[instrument_id].ready != 0U) ? 1U : 0U;
}
