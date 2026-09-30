#include "Audio/audio_rec_overdub.h"

#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/rec_source_audio.h"
#include "Sampler/sample_page_lease.h"
#include "Sampler/sample_play_plan.h"
#include "Sampler/sample_voice_reader.h"
#include "Storage/rec_sd_trace.h"

static sample_voice_reader_t g_audio_rec_overdub_reader;
static uint8_t g_audio_rec_overdub_active;
static uint8_t g_audio_rec_overdub_bind_failure;

static void trace_overdub(rec_sd_trace_event_t event, uint8_t before,
                          uint32_t detail, uint32_t source_id,
                          uint32_t source_generation)
{
    rec_sd_trace_log(event,
        REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        REC_SD_TRACE_CONTEXT(before, g_audio_rec_overdub_active, 0xFFU, 0xFFU),
        detail, source_id, source_generation, 0U);
}

void audio_rec_overdub_init(void)
{
    sample_voice_reader_reset(&g_audio_rec_overdub_reader);
    g_audio_rec_overdub_active = 0U;
    g_audio_rec_overdub_bind_failure = 0U;
}

static uint8_t audio_rec_overdub_bind_current(void)
{
    sample_resolved_source_t source;
    sample_play_plan_t plan;
    if(rec_source_audio_resolve(&source) == 0U)
    {
        g_audio_rec_overdub_bind_failure = 1U;
        return 0U;
    }
    const sample_play_plan_build_options_t options = {
        .start_frame = 0U,
        .end_frame = source.total_frames,
        .loop_begin = 0U,
        .loop_end = source.total_frames,
        .rate = 1.0f,
        .loop_mode = SAMPLE_PLAY_LOOP_FORWARD,
        .stop_on_underrun = 1U
    };
    if(sample_play_plan_build_from_source(&source, &options, &plan)
            != SAMPLE_PLAY_PLAN_BUILD_OK)
    {
        g_audio_rec_overdub_bind_failure = 2U;
        return 0U;
    }
    if (sample_voice_reader_bind_play_plan(
        &g_audio_rec_overdub_reader, &plan,
        SAMPLE_PAGE_LEASE_REC_OVERDUB_READER) == 0U)
    {
        g_audio_rec_overdub_bind_failure = 3U;
        return 0U;
    }
    g_audio_rec_overdub_bind_failure = 0U;
    return 1U;
}

uint8_t audio_rec_overdub_mix(uint8_t enabled,
                              float *left,
                              float *right,
                              uint32_t frames)
{
    if(enabled == 0U)
    {
        if(g_audio_rec_overdub_active != 0U)
        {
            sample_voice_reader_stop(&g_audio_rec_overdub_reader);
            g_audio_rec_overdub_active = 0U;
            trace_overdub(REC_SD_TRACE_OVERDUB_STOP, 1U, 0U,
                g_audio_rec_overdub_reader.plan.key.object_id,
                g_audio_rec_overdub_reader.plan.key.generation);
        }
        g_audio_rec_overdub_active = 0U;
        g_audio_rec_overdub_bind_failure = 0U;
        return 1U;
    }
    if(g_audio_rec_overdub_active == 0U)
    {
        const uint8_t previous_failure = g_audio_rec_overdub_bind_failure;
        if(audio_rec_overdub_bind_current() == 0U)
        {
            if (previous_failure != g_audio_rec_overdub_bind_failure)
                trace_overdub(REC_SD_TRACE_OVERDUB_BIND, 0U,
                    g_audio_rec_overdub_bind_failure, 0U, 0U);
            return 0U;
        }
        g_audio_rec_overdub_active = 1U;
        trace_overdub(REC_SD_TRACE_OVERDUB_BIND, 0U, 0U,
            g_audio_rec_overdub_reader.plan.key.object_id,
            g_audio_rec_overdub_reader.plan.key.generation);
    }
    uint8_t underrun = 0U;
    const uint32_t produced = sample_voice_reader_render_pitch_forward(
        &g_audio_rec_overdub_reader,
        g_audio_rec_overdub_reader.plan.loop_begin,
        g_audio_rec_overdub_reader.plan.loop_end,
        SAMPLE_PLAY_LOOP_FORWARD, 1.0f, 0, 0U,
        left, right, frames, &underrun, 0, 0);
    if((underrun != 0U) || (produced != frames))
    {
        sample_voice_reader_stop(&g_audio_rec_overdub_reader);
        g_audio_rec_overdub_active = 0U;
        trace_overdub(REC_SD_TRACE_OVERDUB_FAULT, 1U,
            (uint32_t)AUDIO_RECORDER_ERROR_OVERDUB_UNDERRUN
                | ((uint32_t)underrun << 8U)
                | ((uint32_t)produced << 16U),
            g_audio_rec_overdub_reader.plan.key.object_id,
            g_audio_rec_overdub_reader.plan.key.generation);
        audio_recorder_capture_audio_fault(
            AUDIO_RECORDER_ERROR_OVERDUB_UNDERRUN);
        return 0U;
    }
    return 1U;
}
