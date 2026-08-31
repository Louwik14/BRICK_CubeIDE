#include "IPC/multi_sample_audio_projection_contract.h"
#include "IPC/multi_sample_audio_projection_control.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/multi_sample_pool.h"
#include "stm32h7xx_hal.h"

D2_IPC multi_audio_instrument_t
    g_multi_audio_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
AUDIO_SHARED_MULTI_SDRAM multi_audio_zone_t
    g_multi_audio_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
AUDIO_SHARED_MULTI_SDRAM multi_sample_audio_source_t
    g_multi_audio_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];

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
    g_multi_audio_instruments[instrument_id].sequence++;
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

    multi_audio_instrument_t *const dst = &g_multi_audio_instruments[instrument_id];
    dst->ready = 0U;
    __DMB();
    for (uint32_t i = instrument->first_sample_id;
         i < (uint32_t)instrument->first_sample_id + instrument->sample_count; ++i)
    {
        const multi_sample_desc_t *const s = multi_sample_pool_get_sample((uint16_t)i);
        if ((s == NULL) || (s->instrument_id != instrument_id)) return 0U;
        g_multi_audio_samples[i] = (multi_sample_audio_source_t){
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
    for (uint32_t i = instrument->first_zone_id;
         i < (uint32_t)instrument->first_zone_id + instrument->zone_count; ++i)
    {
        multi_sample_zone_t z;
        if (multi_sample_pool_copy_zone((uint16_t)i, &z) == 0U) return 0U;
        g_multi_audio_zones[i] = (multi_audio_zone_t){ z.note_low, z.note_high,
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
