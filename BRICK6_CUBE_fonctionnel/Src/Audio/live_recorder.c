#include "Audio/live_recorder.h"

#include <stddef.h>

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
