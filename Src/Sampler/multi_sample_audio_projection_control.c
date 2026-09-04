#include "IPC/multi_sample_audio_projection_contract.h"
#include "IPC/multi_sample_audio_projection_control.h"

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
    multi_audio_instrument_t *const dst = &g_multi_audio_instruments[instrument_id];
    uint32_t sequence = dst->sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    dst->sequence = sequence + 1U;
    dst->ready = 0U;
    __DMB();
    dst->sequence = sequence + 2U;
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
            > MULTI_SAMPLE_POOL_MAX_SAMPLES)) return 0U;

    for (uint32_t i = instrument->first_sample_id;
         i < (uint32_t)instrument->first_sample_id + instrument->sample_count; ++i)
    {
        const multi_sample_desc_t *const s = multi_sample_pool_get_sample((uint16_t)i);
        if ((s == NULL) || (s->instrument_id != instrument_id)) return 0U;
    }
    for (uint32_t i = instrument->first_zone_id;
         i < (uint32_t)instrument->first_zone_id + instrument->zone_count; ++i)
    {
        multi_sample_zone_t z;
        if (multi_sample_pool_copy_zone((uint16_t)i, &z) == 0U) return 0U;
    }

    multi_audio_instrument_t *const dst = &g_multi_audio_instruments[instrument_id];
    uint32_t sequence = dst->sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    dst->sequence = sequence + 1U;
    dst->ready = 0U;
    __DMB();
    for (uint32_t i = instrument->first_sample_id;
         i < (uint32_t)instrument->first_sample_id + instrument->sample_count; ++i)
    {
        const multi_sample_desc_t *const s = multi_sample_pool_get_sample((uint16_t)i);
        if ((s == NULL) || (s->instrument_id != instrument_id))
        {
            dst->sequence = sequence + 2U;
            __DMB();
            return 0U;
        }
        g_multi_audio_samples[i] = (multi_sample_audio_source_t){
            .multi_sample_id = (uint16_t)i, .instrument_id = instrument_id,
            .root_note = s->root_note, .vel_low = s->vel_low, .vel_high = s->vel_high,
            .total_frames = s->total_frames, .data_offset = s->data_offset,
            .data_size = s->data_size, .sample_rate = s->sample_rate,
            .registration_epoch = s->registration_epoch, .loop_begin = s->loop_begin,
            .loop_end = s->loop_end, .format = (uint8_t)s->format,
            .channels = s->channels,
            .bits_per_sample = s->bits_per_sample, .block_align = s->block_align,
            .stride_floats = s->stride_floats, .frames_per_page = s->frames_per_page,
            .has_loop = s->has_loop
        };
    }
    for (uint32_t i = instrument->first_zone_id;
         i < (uint32_t)instrument->first_zone_id + instrument->zone_count; ++i)
    {
        multi_sample_zone_t z;
        if (multi_sample_pool_copy_zone((uint16_t)i, &z) == 0U)
        {
            dst->sequence = sequence + 2U;
            __DMB();
            return 0U;
        }
        g_multi_audio_zones[i] = (multi_audio_zone_t){ z.note_low, z.note_high,
            z.vel_low, z.vel_high, z.root_note, 0U, z.multi_sample_id };
    }
    dst->first_zone_id = instrument->first_zone_id;
    dst->zone_count = instrument->zone_count;
    dst->first_sample_id = instrument->first_sample_id;
    dst->sample_count = instrument->sample_count;
    __DMB();
    dst->ready = 1U;
    __DMB();
    dst->sequence = sequence + 2U;
    __DMB();
    return 1U;
}
