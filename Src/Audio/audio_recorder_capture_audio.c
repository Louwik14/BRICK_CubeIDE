#include "Audio/audio_recorder_capture_audio.h"

#include <stddef.h>
#include <string.h>

#include "IPC/audio_recorder_capture_contract.h"
#include "IPC/storage_io_wakeup.h"
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

static void audio_recorder_capture_audio_close(audio_recorder_error_t fault)
{
    g_audio_capture.active = 0U;
    g_audio_recorder_capture.capture_fault = (uint32_t)fault;
    __DMB();
    g_audio_recorder_capture.closed_session = g_audio_capture.session_id;
    storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
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
    if ((client == (uint8_t)AUDIO_RECORDER_CLIENT_NONE)
            || (session_id == 0U) || (frame_limit == 0U)
            || (g_audio_capture.active != 0U)) return 0U;
    g_audio_recorder_capture.head_cursor = 0U;
    g_audio_capture = (audio_recorder_capture_audio_state_t){
        .session_id = session_id,
        .frame_limit = frame_limit,
        .start_cursor = 0U,
        .client = client,
        .active = 1U
    };
    g_audio_recorder_capture.capture_fault = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_capture.closed_session = 0U;
    __DMB();
    return 1U;
}

uint8_t audio_recorder_capture_audio_stop(uint8_t client,
                                          uint32_t session_id)
{
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
    const uint8_t was_empty = (head == tail) ? 1U : 0U;
    __DMB();
    const uint32_t retained = head - tail;
    if (frames > (AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained))
    {
        audio_recorder_capture_audio_close(AUDIO_RECORDER_ERROR_RING_OVERFLOW);
        return 0U;
    }
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
    if (was_empty != 0U)
    {
        storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
    }
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

uint8_t audio_recorder_capture_audio_pending(void)
{
    return (g_audio_recorder_capture.head_cursor
            != g_audio_recorder_capture.tail_cursor) ? 1U : 0U;
}
