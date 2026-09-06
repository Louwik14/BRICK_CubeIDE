#include "Audio/audio_rec_level_producer.h"
#include "IPC/audio_rec_level_contract.h"
#include "App/control_rt_wakeup.h"
#include "stm32h7xx.h"

static uint32_t g_trigger_config_sequence;
static uint32_t g_trigger_arm_epoch;
static uint8_t g_trigger_enabled;
static uint8_t g_trigger_above;

static uint8_t audio_rec_level_producer_read_trigger_config(
    uint32_t *out_sequence,
    uint32_t *out_arm_epoch,
    uint32_t *out_threshold,
    uint8_t *out_enabled)
{
    if ((out_sequence == 0) || (out_arm_epoch == 0)
            || (out_threshold == 0) || (out_enabled == 0))
        return 0U;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_audio_rec_level_layout.trigger_config_sequence;
        if ((before == 0U) || ((before & 1U) != 0U)) continue;
        __DMB();
        const uint32_t arm_epoch = g_audio_rec_level_layout.trigger_arm_epoch;
        const uint32_t threshold =
            g_audio_rec_level_layout.trigger_threshold_peak_abs_pcm24;
        const uint8_t enabled =
            (g_audio_rec_level_layout.trigger_enabled != 0U) ? 1U : 0U;
        __DMB();
        const uint32_t after = g_audio_rec_level_layout.trigger_config_sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            *out_sequence = after;
            *out_arm_epoch = arm_epoch;
            *out_threshold = threshold;
            *out_enabled = enabled;
            return 1U;
        }
    }
    return 0U;
}

void audio_rec_level_producer_init(void)
{
    g_audio_rec_level_layout.sequence = 0U;
    g_audio_rec_level_layout.generation = 0U;
    g_audio_rec_level_layout.peak_abs_pcm24 = 0U;
    g_audio_rec_level_layout.trigger_config_sequence = 0U;
    g_audio_rec_level_layout.trigger_arm_epoch = 0U;
    g_audio_rec_level_layout.trigger_threshold_peak_abs_pcm24 = 0U;
    g_audio_rec_level_layout.trigger_enabled = 0U;
    g_audio_rec_level_layout.trigger_event_sequence = 0U;
    g_audio_rec_level_layout.trigger_event_arm_epoch = 0U;
    g_audio_rec_level_layout.trigger_event_generation = 0U;
    g_audio_rec_level_layout.trigger_event_peak_abs_pcm24 = 0U;
    g_audio_rec_level_layout.trigger_event_ack_sequence = 0U;
    g_trigger_config_sequence = 0U;
    g_trigger_arm_epoch = 0U;
    g_trigger_enabled = 0U;
    g_trigger_above = 0U;
    __DMB();
}

void audio_rec_level_producer_publish(uint32_t peak_abs_pcm24)
{
    uint32_t sequence = g_audio_rec_level_layout.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    uint32_t generation = g_audio_rec_level_layout.generation + 1U;
    if (generation == 0U) generation = 1U;
    g_audio_rec_level_layout.sequence = sequence + 1U;
    __DMB();
    g_audio_rec_level_layout.generation = generation;
    g_audio_rec_level_layout.peak_abs_pcm24 = peak_abs_pcm24;
    __DMB();
    g_audio_rec_level_layout.sequence = sequence + 2U;
    if (g_audio_rec_level_layout.sequence == 0U) g_audio_rec_level_layout.sequence = 2U;
    __DMB();

    uint32_t config_sequence = 0U;
    uint32_t arm_epoch = 0U;
    uint32_t threshold = 0U;
    uint8_t enabled = 0U;
    if (audio_rec_level_producer_read_trigger_config(
            &config_sequence, &arm_epoch, &threshold, &enabled) == 0U)
        return;

    if ((config_sequence != g_trigger_config_sequence)
            || (arm_epoch != g_trigger_arm_epoch)
            || (enabled != g_trigger_enabled))
    {
        g_trigger_config_sequence = config_sequence;
        g_trigger_arm_epoch = arm_epoch;
        g_trigger_enabled = enabled;
        g_trigger_above = 0U;
    }

    if ((enabled == 0U) || (arm_epoch == 0U) || (threshold == 0U))
    {
        g_trigger_above = 0U;
        return;
    }

    const uint8_t above = (peak_abs_pcm24 >= threshold) ? 1U : 0U;
    const uint32_t event_sequence =
        g_audio_rec_level_layout.trigger_event_sequence;
    const uint32_t acknowledged =
        g_audio_rec_level_layout.trigger_event_ack_sequence;
    const uint32_t event_epoch = g_audio_rec_level_layout.trigger_event_arm_epoch;
    if ((above != 0U) && (g_trigger_above == 0U)
            && ((event_sequence == acknowledged) || (event_epoch != arm_epoch)))
    {
        uint32_t next_sequence = event_sequence + 1U;
        if (next_sequence == 0U) next_sequence = 1U;
        g_audio_rec_level_layout.trigger_event_arm_epoch = arm_epoch;
        g_audio_rec_level_layout.trigger_event_generation = generation;
        g_audio_rec_level_layout.trigger_event_peak_abs_pcm24 = peak_abs_pcm24;
        __DMB();
        g_audio_rec_level_layout.trigger_event_sequence = next_sequence;
        __DMB();
        control_rt_wakeup(CONTROL_RT_WAKE_LATEST);
    }
    g_trigger_above = above;
}
