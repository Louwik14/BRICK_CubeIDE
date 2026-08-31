#include "Audio/audio_waveform_capture_audio.h"

#include "Board/board_audio_format.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"
#include <string.h>

#define AUDIO_WAVEFORM_TRIGGER_ARM_Q15 256
#define AUDIO_WAVEFORM_TRIGGER_MIN_SLOPE_Q15 32
#define AUDIO_WAVEFORM_NO_TRIGGER UINT16_MAX

typedef struct
{
    brick_entity_id_t entity_id;
    uint16_t write_sample;
    uint16_t final_block_sample;
    uint16_t pending_trigger_sample;
    uint16_t capture_audio_samples;
    uint32_t samples_without_trigger;
    uint32_t cooldown_samples;
    int16_t reference_previous;
    uint8_t reference_previous_valid;
    uint8_t trigger_armed;
    uint8_t acquiring;
    uint8_t write_buffer;
    uint8_t pending_trigger_fraction_q8;
    uint8_t frame_triggered;
} audio_waveform_capture_state_t;

static AUDIO_HOT audio_waveform_capture_state_t g_capture;
static AUDIO_HOT brick_entity_id_t g_requested_entity = BRICK_ENTITY_INVALID_ID;
static AUDIO_HOT uint8_t g_fast_refresh;

static int16_t to_q15(float sample)
{
    if (!(sample == sample)) return 0;
    if (sample >= 1.0f) return INT16_MAX;
    if (sample <= -1.0f) return INT16_MIN;
    return (int16_t)(sample * 32767.0f);
}

static int8_t to_q7(float sample)
{
    if (!(sample == sample)) return 0;
    if (sample >= 1.0f) return INT8_MAX;
    if (sample <= -1.0f) return INT8_MIN;
    return (int8_t)(sample * 127.0f);
}

static uint8_t crossing_fraction_q8(int16_t before, int16_t after)
{
    const int32_t span = (int32_t)after - (int32_t)before;
    if (span <= 0) return 0U;
    int32_t fraction = ((-(int32_t)before) << 8) / span;
    if (fraction < 0) fraction = 0;
    if (fraction > 255) fraction = 255;
    return (uint8_t)fraction;
}

static void reset_capture(brick_entity_id_t entity_id)
{
    memset(&g_capture, 0, sizeof(g_capture));
    g_capture.entity_id = entity_id;
    g_capture.pending_trigger_sample = AUDIO_WAVEFORM_NO_TRIGGER;
    g_capture.write_buffer = (uint8_t)(g_audio_waveform_layout.buffer ^ 1U);
}

static void publish_invalid(brick_entity_id_t entity_id)
{
    uint32_t sequence = g_audio_waveform_layout.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_audio_waveform_layout.sequence = sequence + 1U;
    __DMB();
    g_audio_waveform_layout.entity_id = entity_id;
    g_audio_waveform_layout.valid = 0U;
    ++g_audio_waveform_layout.generation;
    __DMB();
    g_audio_waveform_layout.sequence = sequence + 2U;
}

static void publish_capture(void)
{
    uint32_t sequence = g_audio_waveform_layout.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_audio_waveform_layout.sequence = sequence + 1U;
    __DMB();
    g_audio_waveform_layout.buffer = g_capture.write_buffer;
    g_audio_waveform_layout.entity_id = g_capture.entity_id;
    g_audio_waveform_layout.triggered = g_capture.frame_triggered;
    g_audio_waveform_layout.trigger_fraction_q8 = g_capture.pending_trigger_fraction_q8;
    g_audio_waveform_layout.valid = 1U;
    ++g_audio_waveform_layout.generation;
    if (g_audio_waveform_layout.generation == 0U) g_audio_waveform_layout.generation = 1U;
    __DMB();
    g_audio_waveform_layout.sequence = sequence + 2U;

    g_capture.write_buffer ^= 1U;
    g_capture.write_sample = 0U;
    g_capture.capture_audio_samples = 0U;
    g_capture.acquiring = 0U;
    g_capture.frame_triggered = 0U;
    g_capture.pending_trigger_sample = AUDIO_WAVEFORM_NO_TRIGGER;
    g_capture.samples_without_trigger = 0U;
    g_capture.trigger_armed = 0U;
    g_capture.reference_previous_valid = 0U;
    const uint32_t period_samples = BOARD_AUDIO_SAMPLE_RATE_HZ / ((g_fast_refresh != 0U) ? 20U : 10U);
    g_capture.cooldown_samples = (period_samples > AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES)
        ? (period_samples - AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES) : 0U;
}

void audio_waveform_capture_init(void)
{
    memset(g_audio_waveform_buffers, 0, sizeof(g_audio_waveform_buffers));
    memset(&g_audio_waveform_layout, 0, sizeof(g_audio_waveform_layout));
    memset(&g_capture, 0, sizeof(g_capture));
    g_capture.entity_id = BRICK_ENTITY_INVALID_ID;
    g_capture.pending_trigger_sample = AUDIO_WAVEFORM_NO_TRIGGER;
    g_requested_entity = BRICK_ENTITY_INVALID_ID;
    g_audio_waveform_layout.entity_id = BRICK_ENTITY_INVALID_ID;
    g_fast_refresh = 0U;
    __DMB();
}

void audio_waveform_capture_audio_apply_control(brick_entity_id_t entity_id,
                                                uint8_t enabled,
                                                uint8_t fast_refresh)
{
    if ((enabled == 0U) || (entity_id >= BRICK_ENTITY_CAPACITY))
        entity_id = BRICK_ENTITY_INVALID_ID;
    g_requested_entity = entity_id;
    g_fast_refresh = (fast_refresh != 0U) ? 1U : 0U;
}

brick_entity_id_t audio_waveform_capture_get_entity(void) { return g_requested_entity; }

void audio_waveform_capture_begin_block(brick_entity_id_t observed_entity)
{
    if (observed_entity == g_capture.entity_id) return;
    reset_capture(observed_entity);
    publish_invalid(observed_entity);
}

uint8_t audio_waveform_capture_needs_final_samples(void)
{
    return (uint8_t)((g_capture.acquiring != 0U)
        || (g_capture.pending_trigger_sample != AUDIO_WAVEFORM_NO_TRIGGER)
        || ((g_capture.cooldown_samples == 0U)
            && (g_capture.samples_without_trigger >= AUDIO_WAVEFORM_CAPTURE_FREE_RUN_SAMPLES)));
}

static void reference_sample(float sample, uint16_t frame)
{
    const int16_t current = to_q15(sample);
    if (g_capture.acquiring == 0U)
    {
        if (current <= -AUDIO_WAVEFORM_TRIGGER_ARM_Q15) g_capture.trigger_armed = 1U;
        if ((g_capture.trigger_armed != 0U) && (g_capture.reference_previous_valid != 0U)
                && (g_capture.reference_previous < 0) && (current >= 0)
                && (((int32_t)current - g_capture.reference_previous) >= AUDIO_WAVEFORM_TRIGGER_MIN_SLOPE_Q15))
        {
            g_capture.pending_trigger_sample = frame;
            g_capture.pending_trigger_fraction_q8 = crossing_fraction_q8(g_capture.reference_previous, current);
            g_capture.trigger_armed = 0U;
            g_capture.samples_without_trigger = 0U;
        }
        else if (g_capture.samples_without_trigger < UINT32_MAX) ++g_capture.samples_without_trigger;
    }
    g_capture.reference_previous = current;
    g_capture.reference_previous_valid = 1U;
}

static uint8_t reference_block_ready(uint32_t frames)
{
    g_capture.final_block_sample = 0U;
    g_capture.pending_trigger_sample = AUDIO_WAVEFORM_NO_TRIGGER;
    if (g_capture.acquiring != 0U) return 0U;
    if (g_fast_refresh != 0U)
    {
        const uint32_t fast_cooldown = (BOARD_AUDIO_SAMPLE_RATE_HZ / 20U)
            - AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES;
        if (g_capture.cooldown_samples > fast_cooldown) g_capture.cooldown_samples = fast_cooldown;
    }
    if (g_capture.cooldown_samples == 0U) return 1U;
    if (g_capture.cooldown_samples > frames)
    {
        g_capture.cooldown_samples -= frames;
        return 0U;
    }
    g_capture.cooldown_samples = 0U;
    g_capture.reference_previous_valid = 0U;
    return 1U;
}

void audio_waveform_capture_tap_reference_mono_block(const float *mono, uint32_t frames)
{
    if ((mono == NULL) || (g_capture.entity_id == BRICK_ENTITY_INVALID_ID)) return;
    if (reference_block_ready(frames) == 0U) return;
    for (uint32_t frame = 0U; frame < frames; ++frame) reference_sample(mono[frame], (uint16_t)frame);
}

void audio_waveform_capture_tap_reference_stereo_block(const float *left, const float *right, uint32_t frames)
{
    if ((left == NULL) || (right == NULL) || (g_capture.entity_id == BRICK_ENTITY_INVALID_ID)) return;
    if (reference_block_ready(frames) == 0U) return;
    for (uint32_t frame = 0U; frame < frames; ++frame)
        reference_sample((left[frame] + right[frame]) * 0.5f, (uint16_t)frame);
}

void audio_waveform_capture_tap_stereo_sample(float left, float right)
{
    if (g_capture.entity_id == BRICK_ENTITY_INVALID_ID) return;
    if (g_capture.acquiring == 0U)
    {
        if (g_capture.final_block_sample == g_capture.pending_trigger_sample)
        {
            g_capture.acquiring = 1U;
            g_capture.frame_triggered = 1U;
            g_capture.write_sample = 0U;
            g_capture.capture_audio_samples = 0U;
        }
        else if ((g_capture.samples_without_trigger >= AUDIO_WAVEFORM_CAPTURE_FREE_RUN_SAMPLES)
                && (g_capture.final_block_sample == 0U))
        {
            g_capture.acquiring = 1U;
            g_capture.frame_triggered = 0U;
            g_capture.pending_trigger_fraction_q8 = 0U;
            g_capture.write_sample = 0U;
            g_capture.capture_audio_samples = 0U;
        }
    }
    if (g_capture.acquiring != 0U)
    {
        g_audio_waveform_buffers[g_capture.write_buffer][g_capture.write_sample++] = to_q7((left + right) * 0.5f);
        ++g_capture.capture_audio_samples;
        if (g_capture.capture_audio_samples >= AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES) publish_capture();
    }
    ++g_capture.final_block_sample;
}

void audio_waveform_capture_tap_stereo_block(const float *left, const float *right, uint32_t frames)
{
    if ((left == NULL) || (right == NULL)) return;
    for (uint32_t frame = 0U; frame < frames; ++frame)
        audio_waveform_capture_tap_stereo_sample(left[frame], right[frame]);
}
