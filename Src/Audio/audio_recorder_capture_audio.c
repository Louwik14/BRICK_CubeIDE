#include "Audio/audio_recorder_capture_audio.h"

#include <stddef.h>
#include <string.h>

#include "IPC/audio_recorder_capture_contract.h"
#include "Storage/rec_latency_probe.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    uint32_t session_id;
    uint32_t frame_limit;
    uint32_t start_cursor;
    uint8_t client;
    uint8_t active;
} audio_recorder_capture_audio_state_t;

static audio_recorder_capture_audio_state_t g_audio_capture;

typedef struct
{
    audio_recorder_waveform_summary_t levels[2];
    uint32_t compact_cursor;
    uint32_t compact_start_frame;
    uint32_t clear_cursor;
    uint8_t active_level;
    uint8_t compacting;
    uint8_t fixed_length;
} audio_recorder_waveform_capture_t;

STORAGE_STATE_SDRAM static audio_recorder_waveform_capture_t
    g_audio_recorder_waveform_capture;

static void waveform_clear_level(audio_recorder_waveform_summary_t *level)
{
    memset(level, 0, sizeof(*level));
    for (uint32_t i = 0U; i < AUDIO_RECORDER_WAVEFORM_BINS; ++i)
    { level->min[i] = 32767; level->max[i] = -32768; }
}

static void waveform_accumulate(audio_recorder_waveform_summary_t *level,
                                uint32_t bin, int16_t min, int16_t max)
{
    if (bin >= AUDIO_RECORDER_WAVEFORM_BINS) return;
    if (min < level->min[bin]) level->min[bin] = min;
    if (max > level->max[bin]) level->max[bin] = max;
}

static void waveform_compact_one(void)
{
    audio_recorder_waveform_capture_t *const capture =
        &g_audio_recorder_waveform_capture;
    audio_recorder_waveform_summary_t *const src = &capture->levels[capture->active_level];
    audio_recorder_waveform_summary_t *const dst = &capture->levels[capture->active_level ^ 1U];
    const uint32_t i = capture->compact_cursor;
    if (i >= (AUDIO_RECORDER_WAVEFORM_BINS / 2U)) return;
    const int16_t min = (src->min[i * 2U] < src->min[i * 2U + 1U])
        ? src->min[i * 2U] : src->min[i * 2U + 1U];
    const int16_t max = (src->max[i * 2U] > src->max[i * 2U + 1U])
        ? src->max[i * 2U] : src->max[i * 2U + 1U];
    waveform_accumulate(dst, i, min, max);
    capture->compact_cursor++;
}

static void waveform_capture_push(const int32_t *lr, uint32_t frames,
                                  uint32_t first_frame)
{
    audio_recorder_waveform_capture_t *const capture =
        &g_audio_recorder_waveform_capture;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const int32_t l24 = lr[i * 2U];
        const int32_t r24 = lr[i * 2U + 1U];
        const int16_t l = (int16_t)(l24 >> 8U);
        const int16_t r = (int16_t)(r24 >> 8U);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        const uint32_t frame = first_frame + i;
        if (capture->fixed_length != 0U)
        {
            const uint32_t limit = g_audio_capture.frame_limit;
            const uint32_t fixed_bins = (limit < AUDIO_RECORDER_WAVEFORM_BINS)
                ? limit : AUDIO_RECORDER_WAVEFORM_BINS;
            uint32_t bin = (uint32_t)(((uint64_t)frame
                * fixed_bins) / limit);
            if (bin >= fixed_bins) bin = fixed_bins - 1U;
            waveform_accumulate(&capture->levels[0], bin, sample_min, sample_max);
            continue;
        }
        audio_recorder_waveform_summary_t *active = &capture->levels[capture->active_level];
        uint32_t frames_per_bin = active->frames_per_bin;
        if ((capture->compacting == 0U)
                && ((frame / frames_per_bin) >= AUDIO_RECORDER_WAVEFORM_BINS))
        {
            capture->compacting = 1U;
            capture->compact_cursor = 0U;
            capture->compact_start_frame = frame;
        }
        if (capture->compacting != 0U)
        {
            waveform_compact_one();
            audio_recorder_waveform_summary_t *const dst =
                &capture->levels[capture->active_level ^ 1U];
            const uint32_t next_fpb = frames_per_bin * 2U;
            const uint32_t bin = (AUDIO_RECORDER_WAVEFORM_BINS / 2U)
                + ((frame - capture->compact_start_frame) / next_fpb);
            waveform_accumulate(dst, bin, sample_min, sample_max);
            if (capture->compact_cursor >= (AUDIO_RECORDER_WAVEFORM_BINS / 2U))
            {
                capture->active_level ^= 1U;
                capture->levels[capture->active_level].frames_per_bin = next_fpb;
                capture->compacting = 0U;
                capture->clear_cursor = 0U;
            }
        }
        else
        {
            waveform_accumulate(active, frame / frames_per_bin,
                                sample_min, sample_max);
            audio_recorder_waveform_summary_t *const spare =
                &capture->levels[capture->active_level ^ 1U];
            for (uint8_t clear = 0U; clear < 2U
                    && capture->clear_cursor < AUDIO_RECORDER_WAVEFORM_BINS; ++clear)
            {
                spare->min[capture->clear_cursor] = 32767;
                spare->max[capture->clear_cursor] = -32768;
                capture->clear_cursor++;
            }
        }
    }
}

static void audio_recorder_capture_audio_close(audio_recorder_error_t fault)
{
    g_audio_capture.active = 0U;
    g_audio_recorder_capture.capture_fault = (uint32_t)fault;
    __DMB();
    g_audio_recorder_capture.closed_session = g_audio_capture.session_id;
}

void audio_recorder_capture_audio_init(void)
{
    memset(&g_audio_capture, 0, sizeof(g_audio_capture));
    g_audio_recorder_capture.head_cursor = 0U;
    g_audio_recorder_capture.closed_session = 0U;
    g_audio_recorder_capture.capture_fault = AUDIO_RECORDER_ERROR_NONE;
    __DMB();
}

uint8_t audio_recorder_capture_audio_start(uint8_t client,
                                           uint32_t session_id,
                                           uint32_t frame_limit)
{
    if ((client != (uint8_t)AUDIO_RECORDER_CLIENT_AUDIO_REC)
            || (session_id == 0U) || (frame_limit == 0U)
            || (g_audio_capture.active != 0U))
    {
        return 0U;
    }
    g_audio_recorder_capture.head_cursor = 0U;
    g_audio_capture = (audio_recorder_capture_audio_state_t){
        .session_id = session_id,
        .frame_limit = frame_limit,
        .start_cursor = 0U,
        .client = client,
        .active = 1U
    };
    memset(&g_audio_recorder_waveform_capture, 0,
           sizeof(g_audio_recorder_waveform_capture));
    waveform_clear_level(&g_audio_recorder_waveform_capture.levels[0]);
    waveform_clear_level(&g_audio_recorder_waveform_capture.levels[1]);
    g_audio_recorder_waveform_capture.levels[0].frames_per_bin = 1U;
    g_audio_recorder_waveform_capture.levels[1].frames_per_bin = 2U;
    g_audio_recorder_waveform_capture.fixed_length =
        (frame_limit < ((UINT32_MAX - 44U) / 6U)) ? 1U : 0U;
    g_audio_recorder_capture.capture_fault = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_capture.closed_session = 0U;
    __DMB();
    return 1U;
}

uint8_t audio_recorder_capture_audio_stop(uint8_t client,
                                          uint32_t session_id)
{
    if ((g_audio_capture.active == 0U)
            && (g_audio_capture.client == client)
            && (g_audio_capture.session_id == session_id)
            && (g_audio_recorder_capture.closed_session == session_id))
        return 1U;
    if ((g_audio_capture.active == 0U)
            || (g_audio_capture.client != client)
            || (g_audio_capture.session_id != session_id)) return 0U;
    audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
    return 1U;
}

uint8_t audio_recorder_capture_audio_push(audio_recorder_client_t client,
                                          const int32_t *lr_interleaved,
                                          uint32_t frames)
{
    if ((g_audio_capture.active == 0U)
            || (g_audio_capture.client != (uint8_t)client)
            || (lr_interleaved == NULL) || (frames == 0U)) return 0U;
    const uint32_t head = g_audio_recorder_capture.head_cursor;
    const uint32_t captured = head - g_audio_capture.start_cursor;
    if (captured >= g_audio_capture.frame_limit)
    {
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
        return 1U;
    }
    const uint32_t remaining = g_audio_capture.frame_limit - captured;
    if (frames > remaining) frames = remaining;
    const uint32_t tail = g_audio_recorder_capture.tail_cursor;
    __DMB();
    const uint32_t retained = head - tail;
    if (frames > (AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained))
    {
        g_rec_latency_probe.recorder_overflow_count++;
        g_rec_latency_probe.recorder_last_overflow_t = rec_latency_probe_now();
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_RING_OVERFLOW);
        return 0U;
    }
    waveform_capture_push(lr_interleaved, frames, captured);
    const uint32_t used_after_push = retained + frames;
    if(used_after_push > g_rec_latency_probe.recorder_ring_max_used_frames)
        g_rec_latency_probe.recorder_ring_max_used_frames = used_after_push;
    const uint32_t write = head % AUDIO_RECORDER_CAPTURE_RING_FRAMES;
    uint32_t first = AUDIO_RECORDER_CAPTURE_RING_FRAMES - write;
    if (first > frames) first = frames;
    memcpy(&g_audio_recorder_capture_ring[write * AUDIO_RECORDER_CHANNELS],
           lr_interleaved,
           (size_t)first * AUDIO_RECORDER_CHANNELS * sizeof(int32_t));
    if (frames > first)
        memcpy(g_audio_recorder_capture_ring,
               &lr_interleaved[first * AUDIO_RECORDER_CHANNELS],
               (size_t)(frames - first) * AUDIO_RECORDER_CHANNELS * sizeof(int32_t));
    __DMB();
    g_audio_recorder_capture.head_cursor = head + frames;
    if ((captured + frames) >= g_audio_capture.frame_limit)
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
    return 1U;
}

uint8_t audio_recorder_capture_audio_frames(audio_recorder_client_t client,
                                            uint32_t *out_frames)
{
    if ((out_frames == NULL) || (g_audio_capture.client != (uint8_t)client)
            || (g_audio_capture.session_id == 0U)) return 0U;
    *out_frames = g_audio_recorder_capture.head_cursor - g_audio_capture.start_cursor;
    return (g_audio_recorder_capture.capture_fault == AUDIO_RECORDER_ERROR_NONE) ? 1U : 0U;
}

void audio_recorder_capture_audio_fault(audio_recorder_client_t client,
                                        audio_recorder_error_t fault)
{
    if((g_audio_capture.active != 0U)
            && (g_audio_capture.client == (uint8_t)client))
        audio_recorder_capture_audio_close(fault);
}

uint8_t audio_recorder_capture_audio_waveform_export(
    audio_recorder_waveform_summary_t *out_summary)
{
    if ((out_summary == NULL) || (g_audio_capture.active != 0U)
            || (g_audio_recorder_capture.capture_fault != AUDIO_RECORDER_ERROR_NONE)) return 0U;
    while (g_audio_recorder_waveform_capture.compacting != 0U)
    {
        waveform_compact_one();
        if (g_audio_recorder_waveform_capture.compact_cursor
                >= (AUDIO_RECORDER_WAVEFORM_BINS / 2U))
        {
            const uint8_t next = g_audio_recorder_waveform_capture.active_level ^ 1U;
            g_audio_recorder_waveform_capture.levels[next].frames_per_bin =
                g_audio_recorder_waveform_capture.levels[
                    g_audio_recorder_waveform_capture.active_level].frames_per_bin * 2U;
            g_audio_recorder_waveform_capture.active_level = next;
            g_audio_recorder_waveform_capture.compacting = 0U;
        }
    }
    const uint8_t level = (g_audio_recorder_waveform_capture.fixed_length != 0U)
        ? 0U : g_audio_recorder_waveform_capture.active_level;
    *out_summary = g_audio_recorder_waveform_capture.levels[level];
    out_summary->frame_count = g_audio_recorder_capture.head_cursor;
    if (g_audio_recorder_waveform_capture.fixed_length != 0U)
    {
        out_summary->frames_per_bin = 0U;
        out_summary->bin_count = (g_audio_capture.frame_limit < AUDIO_RECORDER_WAVEFORM_BINS)
            ? (uint16_t)g_audio_capture.frame_limit : AUDIO_RECORDER_WAVEFORM_BINS;
    }
    else
    {
        uint32_t bins = (out_summary->frame_count + out_summary->frames_per_bin - 1U)
            / out_summary->frames_per_bin;
        if (bins > AUDIO_RECORDER_WAVEFORM_BINS) bins = AUDIO_RECORDER_WAVEFORM_BINS;
        out_summary->bin_count = (uint16_t)bins;
    }
    for (uint16_t i = 0U; i < out_summary->bin_count; ++i)
        if ((out_summary->min[i] == 32767) && (out_summary->max[i] == -32768))
        { out_summary->min[i] = 0; out_summary->max[i] = 0; }
    out_summary->ready = (out_summary->bin_count != 0U) ? 1U : 0U;
    return out_summary->ready;
}
