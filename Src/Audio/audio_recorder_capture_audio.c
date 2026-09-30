#include "Audio/audio_recorder_capture_audio.h"

#include <stddef.h>
#include <string.h>

#include "Recorder/audio_recorder_ring.h"
#include "Platform/stream_rec_perf.h"
#include "Storage/rec_sd_trace.h"
#include "stm32h7xx.h"

typedef struct
{
    uint32_t session_id;
    uint32_t frame_limit;
    uint8_t active;
} audio_recorder_capture_audio_state_t;

static audio_recorder_capture_audio_state_t g_audio_capture;

static void audio_recorder_capture_audio_close(audio_recorder_error_t fault)
{
    rec_sd_trace_log(REC_SD_TRACE_AUDIO_CLOSE,
        REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        (uint32_t)fault, g_audio_recorder_ring_state.produced_frames,
        g_audio_capture.session_id, 0U);
    g_audio_capture.active = 0U;
    g_audio_recorder_ring_state.capture_fault = (uint32_t)fault;
    __DMB();
    g_audio_recorder_ring_state.closed_session = g_audio_capture.session_id;
}

void audio_recorder_capture_audio_init(void)
{
    memset(&g_audio_capture, 0, sizeof(g_audio_capture));
    g_audio_recorder_ring_state.produced_frames = 0U;
    g_audio_recorder_ring_state.released_frames = 0U;
    g_audio_recorder_ring_state.started_session = 0U;
    g_audio_recorder_ring_state.closed_session = 0U;
    g_audio_recorder_ring_state.capture_fault = AUDIO_RECORDER_ERROR_NONE;
    __DMB();
}

uint8_t audio_recorder_capture_audio_start(uint32_t session_id,
                                           uint32_t frame_limit)
{
    if ((session_id == 0U) || (frame_limit == 0U)
            || (g_audio_capture.active != 0U))
    {
        rec_sd_trace_log(REC_SD_TRACE_AUDIO_START,
            REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
            REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
            0U, g_audio_recorder_ring_state.produced_frames, session_id, 0U);
        return 0U;
    }
    g_audio_recorder_ring_state.produced_frames = 0U;
    g_audio_capture = (audio_recorder_capture_audio_state_t){
        .session_id = session_id,
        .frame_limit = frame_limit,
        .active = 1U
    };
    g_audio_recorder_ring_state.capture_fault = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_ring_state.closed_session = 0U;
    __DMB();
    g_audio_recorder_ring_state.started_session = session_id;
    rec_sd_trace_log(REC_SD_TRACE_AUDIO_START,
        REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        1U, 0U, session_id, 0U);
    return 1U;
}

uint8_t audio_recorder_capture_audio_stop(uint32_t session_id)
{
    if ((g_audio_capture.active == 0U)
            && (g_audio_capture.session_id == session_id)
            && (g_audio_recorder_ring_state.closed_session == session_id))
        return 1U;
    if ((g_audio_capture.active == 0U)
            || (g_audio_capture.session_id != session_id))
    {
        rec_sd_trace_log(REC_SD_TRACE_AUDIO_STOP,
            REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
            REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
            0U, g_audio_recorder_ring_state.produced_frames, session_id, 0U);
        return 0U;
    }
    rec_sd_trace_log(REC_SD_TRACE_AUDIO_STOP,
        REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        1U, g_audio_recorder_ring_state.produced_frames, session_id, 0U);
    audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
    return 1U;
}

uint8_t audio_recorder_capture_audio_push(const float *lr_interleaved,
                                          uint32_t frames)
{
    if ((g_audio_capture.active == 0U)
            || (lr_interleaved == NULL) || (frames == 0U)) return 0U;
    const uint32_t head = g_audio_recorder_ring_state.produced_frames;
    const uint32_t captured = head;
    if (captured >= g_audio_capture.frame_limit)
    {
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
        return 1U;
    }
    const uint32_t remaining = g_audio_capture.frame_limit - captured;
    if (frames > remaining) frames = remaining;
    const uint32_t tail = g_audio_recorder_ring_state.released_frames;
    __DMB();
    const uint32_t retained = head - tail;
    if (frames > (AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained))
    {
        PERF_COUNT(PERF_N_REC_OVERFLOW);
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_RING_OVERFLOW);
        return 0U;
    }
    PERF_START(ring_start);
    const uint32_t write = head % AUDIO_RECORDER_CAPTURE_RING_FRAMES;
    uint32_t first = AUDIO_RECORDER_CAPTURE_RING_FRAMES - write;
    if (first > frames) first = frames;
    memcpy(&g_audio_recorder_ring[write * AUDIO_RECORDER_CHANNELS],
           lr_interleaved,
           (size_t)first * AUDIO_RECORDER_CHANNELS * sizeof(float));
    if (frames > first)
        memcpy(g_audio_recorder_ring,
               &lr_interleaved[first * AUDIO_RECORDER_CHANNELS],
               (size_t)(frames - first) * AUDIO_RECORDER_CHANNELS * sizeof(float));
    __DMB();
    g_audio_recorder_ring_state.produced_frames = head + frames;
    PERF_END(PERF_CPU_AUDIO_RING, ring_start);
    PERF_ACCUM(PERF_N_REC_FRAMES, frames);
    PERF_ACCUM(PERF_N_REC_PCM_BYTES, frames * AUDIO_RECORDER_BYTES_PER_FRAME);
    const uint32_t fill = retained + frames;
    PERF_SET(PERF_N_REC_RING_FILL, fill);
    PERF_MAX(PERF_N_REC_RING_MAX, fill);
    const uint32_t free_frames = AUDIO_RECORDER_CAPTURE_RING_FRAMES - fill;
    PERF_MIN_NONZERO(PERF_N_REC_RING_MIN_FREE, free_frames);
    if (free_frames < AUDIO_RECORDER_CAPTURE_RING_FRAMES / 8U)
        PERF_COUNT(PERF_N_REC_RING_NEAR_FULL);
    if ((captured + frames) >= g_audio_capture.frame_limit)
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_NONE);
    return 1U;
}

void audio_recorder_capture_audio_fault(audio_recorder_error_t fault)
{
    if(g_audio_capture.active != 0U)
        audio_recorder_capture_audio_close(fault);
}
