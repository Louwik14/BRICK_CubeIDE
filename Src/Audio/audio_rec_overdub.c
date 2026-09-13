#include "Audio/audio_rec_overdub.h"

#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/rec_source_audio.h"
#include "Sampler/sample_page_lease.h"
#include "Sampler/sample_play_plan.h"
#include "Sampler/sample_voice_reader.h"

static sample_voice_reader_t g_audio_rec_overdub_reader;
static uint8_t g_audio_rec_overdub_active;

void audio_rec_overdub_init(void)
{
    sample_voice_reader_reset(&g_audio_rec_overdub_reader);
    g_audio_rec_overdub_active = 0U;
}

static uint8_t audio_rec_overdub_bind_current(void)
{
    sample_resolved_source_t source;
    sample_play_plan_t plan;
    if(rec_source_audio_resolve(&source) == 0U) return 0U;
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
            != SAMPLE_PLAY_PLAN_BUILD_OK) return 0U;
    return sample_voice_reader_bind_play_plan(
        &g_audio_rec_overdub_reader, &plan,
        SAMPLE_PAGE_LEASE_REC_OVERDUB_READER);
}

uint8_t audio_rec_overdub_mix(uint8_t enabled,
                              float *left,
                              float *right,
                              uint32_t frames)
{
    if(enabled == 0U)
    {
        if(g_audio_rec_overdub_active != 0U)
            sample_voice_reader_stop(&g_audio_rec_overdub_reader);
        g_audio_rec_overdub_active = 0U;
        return 1U;
    }
    if(g_audio_rec_overdub_active == 0U)
    {
        if(audio_rec_overdub_bind_current() == 0U) return 0U;
        g_audio_rec_overdub_active = 1U;
    }
    uint8_t reverse = 0U;
    uint8_t underrun = 0U;
    const uint32_t produced = sample_voice_reader_render_pitch_forward(
        &g_audio_rec_overdub_reader,
        g_audio_rec_overdub_reader.plan.loop_begin,
        g_audio_rec_overdub_reader.plan.loop_end,
        &reverse, SAMPLE_PLAY_LOOP_FORWARD, 1.0f, 0, 0U,
        left, right, frames, &underrun, 0, 0);
    if((underrun != 0U) || (produced != frames))
    {
        sample_voice_reader_stop(&g_audio_rec_overdub_reader);
        g_audio_rec_overdub_active = 0U;
        audio_recorder_capture_audio_fault(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                           AUDIO_RECORDER_ERROR_SD_IO);
        return 0U;
    }
    return 1U;
}
