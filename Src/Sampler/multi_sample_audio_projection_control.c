#include "Sampler/multi_sample_audio_projection_contract.h"
#include "Sampler/multi_sample_audio_projection_control.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/multi_sample_pool.h"
#include "stm32h7xx_hal.h"

void multi_sample_audio_projection_init(void)
{
    memset(g_multi_audio_instruments, 0, sizeof(g_multi_audio_instruments));
    memset(g_multi_audio_zones, 0, sizeof(g_multi_audio_zones));
    memset(g_multi_audio_samples, 0, sizeof(g_multi_audio_samples));
    __DMB();
}

void multi_sample_audio_projection_withdraw(uint16_t instrument_id)
{
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS) return;
    g_multi_audio_instruments[instrument_id].ready = 0U;
    __DMB();
}

uint8_t multi_sample_audio_projection_publish(uint16_t instrument_id)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((instrument == NULL) || (instrument->state != MULTI_SAMPLE_INSTRUMENT_READY)
        || (instrument->zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
        || (instrument->sample_count > MULTI_SAMPLE_POOL_MAX_SAMPLES)) return 0U;

    multi_audio_instrument_t *const dst = &g_multi_audio_instruments[instrument_id];
    dst->ready = 0U;
    __DMB();
    uint16_t sample_id = instrument->sample_head_id;
    for (uint16_t visited = 0U; visited < instrument->sample_count; ++visited)
    {
        if (sample_id >= MULTI_SAMPLE_POOL_MAX_SAMPLES) return 0U;
        const multi_sample_desc_t *const s = multi_sample_pool_get_sample(sample_id);
        if ((s == NULL) || (s->instrument_id != instrument_id)) return 0U;
        g_multi_audio_samples[sample_id] = (multi_sample_audio_source_t){
            .multi_sample_id = sample_id, .instrument_id = instrument_id,
            .root_note = s->root_note, .vel_low = s->vel_low, .vel_high = s->vel_high,
            .total_frames = s->total_frames,
            .registration_epoch = s->registration_epoch, .loop_begin = s->loop_begin,
            .loop_end = s->loop_end, .format = s->format,
            .stride_floats = s->stride_floats, .frames_per_page = s->frames_per_page,
            .has_loop = s->has_loop
        };
        sample_id = multi_sample_pool_next_sample_id(sample_id);
    }
    uint16_t zone_id = instrument->zone_head_id;
    for (uint16_t visited = 0U; visited < instrument->zone_count; ++visited)
    {
        if (zone_id >= MULTI_SAMPLE_POOL_MAX_ZONES) return 0U;
        multi_sample_zone_t z;
        if (multi_sample_pool_copy_zone(zone_id, &z) == 0U) return 0U;
        const uint16_t next = multi_sample_pool_next_zone_id(zone_id);
        g_multi_audio_zones[zone_id] = (multi_audio_zone_t){ z.note_low, z.note_high,
            z.vel_low, z.vel_high, z.root_note, 0U, z.multi_sample_id, next };
        zone_id = next;
    }
    dst->zone_head_id = instrument->zone_head_id;
    dst->zone_count = instrument->zone_count;
    dst->sample_count = instrument->sample_count;
    __DMB();
    dst->ready = 1U;
    __DMB();
    return 1U;
}
