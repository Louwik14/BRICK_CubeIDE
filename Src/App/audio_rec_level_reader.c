#include "IPC/audio_rec_level_reader.h"
#include "stm32h7xx.h"

void audio_rec_level_control_publish_trigger_config(
    uint8_t enabled,
    uint32_t arm_epoch,
    uint32_t threshold_peak_abs_pcm24)
{
    uint32_t sequence = g_audio_rec_level_layout.trigger_config_sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_audio_rec_level_layout.trigger_config_sequence = sequence + 1U;
    __DMB();
    g_audio_rec_level_layout.trigger_arm_epoch = arm_epoch;
    g_audio_rec_level_layout.trigger_threshold_peak_abs_pcm24 =
        threshold_peak_abs_pcm24;
    g_audio_rec_level_layout.trigger_enabled = (enabled != 0U) ? 1U : 0U;
    __DMB();
    g_audio_rec_level_layout.trigger_config_sequence = sequence + 2U;
    if (g_audio_rec_level_layout.trigger_config_sequence == 0U)
        g_audio_rec_level_layout.trigger_config_sequence = 2U;
    __DMB();
}

uint8_t audio_rec_level_reader_read(audio_rec_level_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0) return 0U;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_audio_rec_level_layout.sequence;
        if ((before == 0U) || ((before & 1U) != 0U)) continue;
        __DMB();
        audio_rec_level_snapshot_t next = {
            .generation = g_audio_rec_level_layout.generation,
            .peak_abs_pcm24 = g_audio_rec_level_layout.peak_abs_pcm24
        };
        __DMB();
        const uint32_t after = g_audio_rec_level_layout.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            *out_snapshot = next;
            return (uint8_t)(next.generation != 0U);
        }
    }
    return 0U;
}

uint8_t audio_rec_level_reader_read_trigger_event(
    audio_rec_trigger_event_t *out_event)
{
    if (out_event == 0) return 0U;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t sequence = g_audio_rec_level_layout.trigger_event_sequence;
        const uint32_t acknowledged =
            g_audio_rec_level_layout.trigger_event_ack_sequence;
        if ((sequence == 0U) || (sequence == acknowledged)) continue;
        __DMB();
        audio_rec_trigger_event_t event = {
            .sequence = sequence,
            .arm_epoch = g_audio_rec_level_layout.trigger_event_arm_epoch,
            .generation = g_audio_rec_level_layout.trigger_event_generation,
            .peak_abs_pcm24 = g_audio_rec_level_layout.trigger_event_peak_abs_pcm24
        };
        __DMB();
        if ((sequence == g_audio_rec_level_layout.trigger_event_sequence)
                && (sequence != g_audio_rec_level_layout.trigger_event_ack_sequence))
        {
            *out_event = event;
            return 1U;
        }
    }
    return 0U;
}

void audio_rec_level_reader_ack_trigger_event(uint32_t sequence)
{
    if (sequence == 0U) return;
    if (g_audio_rec_level_layout.trigger_event_sequence != sequence)
        return;
    __DMB();
    g_audio_rec_level_layout.trigger_event_ack_sequence = sequence;
    __DMB();
}
