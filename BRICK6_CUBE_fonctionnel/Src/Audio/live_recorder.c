#include "Audio/live_recorder.h"

#include <stddef.h>
#include <string.h>

#define LIVE_RECORDER_READ_SAFETY_MARGIN_FRAMES (256U)

void live_recorder_init(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->buffer = NULL;
    rec->max_frames = 0U;
    rec->loop_frames = 0U;
    rec->write_pos = 0U;
    rec->read_pos = 0U;
    rec->recording = 0U;
    rec->playing = 0U;
    rec->latency_offset_frames = 0U;
    rec->tap_mode = (uint8_t)LIVE_RECORDER_TAP_POST_MIX;
}

void live_recorder_set_buffer(live_recorder_t *rec,
                              float *buffer,
                              uint32_t max_frames)
{
    if(rec == NULL)
        return;

    rec->buffer = buffer;
    rec->max_frames = max_frames;

    if(rec->loop_frames > rec->max_frames)
        rec->loop_frames = rec->max_frames;

    if(rec->write_pos >= rec->max_frames)
        rec->write_pos = 0U;

    if(rec->read_pos >= rec->max_frames)
        rec->read_pos = 0U;
}

void live_recorder_set_loop_length(live_recorder_t *rec,
                                   uint32_t loop_frames)
{
    if(rec == NULL)
        return;

    if(loop_frames > rec->max_frames)
        loop_frames = rec->max_frames;

    rec->loop_frames = loop_frames;

    if((rec->loop_frames == 0U) || (rec->write_pos >= rec->loop_frames))
        rec->write_pos = 0U;

    if((rec->loop_frames == 0U) || (rec->read_pos >= rec->loop_frames))
        rec->read_pos = 0U;
}

void live_recorder_start_record(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->recording = 1U;
}

void live_recorder_stop_record(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->recording = 0U;
}

void live_recorder_start_play(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->playing = 1U;
}

void live_recorder_stop_play(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->playing = 0U;
}

void live_recorder_write(live_recorder_t *rec,
                         const float *L,
                         const float *R,
                         uint32_t frames)
{
    if((rec == NULL) || (L == NULL) || (R == NULL))
        return;

    if((rec->recording == 0U) || (rec->buffer == NULL) || (rec->loop_frames == 0U))
        return;

    uint32_t write_pos = rec->write_pos;
    float *buffer = rec->buffer;
    const uint32_t loop_frames = rec->loop_frames;

    for(uint32_t i = 0U; i < frames; i++)
    {
        const uint32_t idx = write_pos * 2U;
        buffer[idx] = L[i];
        buffer[idx + 1U] = R[i];

        write_pos++;
        if(write_pos >= loop_frames)
            write_pos = 0U;
    }

    rec->write_pos = write_pos;
}


void live_recorder_read(live_recorder_t *rec,
                        float *outL,
                        float *outR,
                        uint32_t frames)
{
    if((outL == NULL) || (outR == NULL))
        return;

    if((rec == NULL) || (rec->playing == 0U) || (rec->buffer == NULL) || (rec->loop_frames == 0U))
    {
        memset(outL, 0, sizeof(float) * frames);
        memset(outR, 0, sizeof(float) * frames);
        return;
    }

    const float *buffer = rec->buffer;
    const uint32_t loop_frames = rec->loop_frames;
    const uint32_t write_pos = rec->write_pos;
    uint32_t read_pos = rec->read_pos;

    for(uint32_t i = 0U; i < frames; i++)
    {
        uint32_t distance;
        if(write_pos >= read_pos)
            distance = write_pos - read_pos;
        else
            distance = loop_frames - (read_pos - write_pos);

        if(distance < LIVE_RECORDER_READ_SAFETY_MARGIN_FRAMES)
        {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        else
        {
            const uint32_t idx = read_pos * 2U;
            outL[i] = buffer[idx];
            outR[i] = buffer[idx + 1U];
        }

        read_pos++;
        if(read_pos >= loop_frames)
            read_pos = 0U;
    }

    rec->read_pos = read_pos;
}
