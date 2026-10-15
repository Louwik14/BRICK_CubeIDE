typedef struct
{
    brick6_sampler_voice_t voice;
    brick6_sampler_preview_kind_t kind;
    uint16_t runtime_id;
    uint8_t prepared;
} brick6_sampler_preview_runtime_t;

AUDIO_WARM static brick6_sampler_preview_runtime_t g_sampler_preview;

void brick6_sampler_preview_reset(void)
{
    sample_voice_reader_reset(&g_sampler_preview.voice.reader);
    memset(&g_sampler_preview, 0, sizeof(g_sampler_preview));
    g_sampler_preview.voice.note = STREAM_SAMPLER_ROOT_NOTE;
    g_sampler_preview.voice.owner_track_id = UINT8_MAX;
    g_sampler_preview.voice.velocity = 127U;
    g_sampler_preview.voice.gain = 1.0f;
    g_sampler_preview.voice.trigger_velocity_gain = 1.0f;
    g_sampler_preview.voice.length = 1.0f;
    g_sampler_preview.voice.multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_preview.voice.multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
}

uint8_t brick6_sampler_preview_prepare(brick6_sampler_preview_kind_t kind,
                                       uint16_t runtime_id)
{
    brick6_sampler_preview_reset();
    g_sampler_preview.kind = kind;
    g_sampler_preview.runtime_id = runtime_id;
    if (kind == BRICK6_SAMPLER_PREVIEW_STREAM)
    {
        sample_resolved_source_t source;
        if (!sample_classic_audio_projection_resolve(runtime_id, &source))
            return 0U;
    }
    else if (kind == BRICK6_SAMPLER_PREVIEW_MULTI)
    {
        multi_sample_audio_source_t source;
        if (!multi_sample_audio_projection_resolve(runtime_id, 60U, 100U,
                                                    &source)) return 0U;
    }
    else return 0U;
    g_sampler_preview.prepared = 1U;
    return 1U;
}

uint8_t brick6_sampler_preview_apply_param(param_id_t id, float value)
{
    (void)value;
    switch (id)
    {
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_START:
        case PARAM_SAMPLER_LENGTH:
        case PARAM_SAMPLER_MODE:
        case PARAM_SAMPLER_TUNE:
        case PARAM_SAMPLER_LOOP_START:
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
        case PARAM_SAMPLER_CLIP_GRAIN:
        case PARAM_SAMPLER_CLIP_SOURCE:
        case PARAM_SHIFTER_HEADS:
        case PARAM_SHIFTER_WINDOW:
        case PARAM_SHIFTER_DISP:
        case PARAM_SAMPLER_MULTI_LOOP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t brick6_sampler_preview_bind_source(
    sample_resolved_source_t *source, uint8_t note, uint8_t velocity,
    uint8_t source_kind, float velocity_gain)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    source->rate = 1.0f;
    source->region_begin = 0U;
    source->region_end = source->total_frames;
    source->loop_begin = 0U;
    source->loop_end = source->total_frames;
    source->loop_mode = SAMPLE_PLAY_LOOP_NONE;

    sample_play_plan_build_options_t options = {
        .flags = SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION
            | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP
            | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_RATE,
        .stop_on_underrun = 1U
    };
    sample_play_plan_t plan;
    if (sample_play_plan_build_from_source(source, &options, &plan)
            != SAMPLE_PLAY_PLAN_BUILD_OK) return 0U;
    if (!sample_voice_reader_bind_preview_play_plan(&voice->reader, &plan))
        return 0U;

    voice->note = note;
    voice->velocity = velocity;
    voice->source_kind = source_kind;
    voice->sample_id = plan.sample_id;
    voice->gain = 1.0f;
    voice->trigger_velocity_gain = velocity_gain;
    voice->region_begin = plan.region_begin;
    voice->region_end = plan.region_end;
    voice->loop_begin = plan.loop_begin;
    voice->loop_frames = plan.region_end - plan.region_begin;
    voice->loop_mode = plan.loop_mode;
    voice->use_segment_cursor = 1U;
    voice->play_plan = plan;
    voice->format = plan.format;
    voice->stride_floats = plan.stride_floats;
    voice->frames_per_page = plan.frames_per_page;
    voice->registration_epoch = plan.registration_epoch;
    voice->position = (float)plan.start_frame;
    voice->active = 1U;
    brick6_sampler_runtime_start_declick_fade_in(voice);
    return 1U;
}

uint8_t brick6_sampler_preview_note_on(uint8_t note, uint8_t velocity)
{
    if ((g_sampler_preview.prepared == 0U) || (note > 127U)
            || (velocity == 0U) || (velocity > 127U)) return 0U;
    sample_voice_reader_reset(&g_sampler_preview.voice.reader);
    if (g_sampler_preview.kind == BRICK6_SAMPLER_PREVIEW_STREAM)
    {
        sample_resolved_source_t source;
        if (!sample_classic_audio_projection_resolve(
                g_sampler_preview.runtime_id, &source)) return 0U;
        return brick6_sampler_preview_bind_source(&source, note, velocity,
            BRICK6_SAMPLER_VOICE_CLIP, 1.0f);
    }

    multi_sample_audio_source_t projected;
    if (!multi_sample_audio_projection_resolve(g_sampler_preview.runtime_id,
            note, velocity, &projected)) return 0U;
    sample_resolved_source_t source;
    sample_resolved_source_init(&source);
    source.key = sample_audio_key_multi(projected.multi_sample_id);
    source.total_frames = projected.total_frames;
    source.format = projected.format;
    source.stride_floats = projected.stride_floats;
    source.frames_per_page = projected.frames_per_page;
    source.registration_epoch = projected.registration_epoch;
    source.root_note = projected.root_note;
    const float velocity_gain = projected.zone_is_single_velocity_layer
        ? brick6_sampler_runtime_velocity_gain(velocity) : 1.0f;
    return brick6_sampler_preview_bind_source(&source, note, velocity,
        BRICK6_SAMPLER_VOICE_MULTI, velocity_gain);
}

void brick6_sampler_preview_note_off(uint8_t note)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    if ((voice->active == 0U) || (voice->note != note)) return;
    sample_voice_reader_stop(&voice->reader);
    voice->active = 0U;
}

static void brick6_sampler_preview_stop_classic_sample(uint16_t sample_id)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (voice->sample_id != sample_id)) return;
    sample_voice_reader_stop(&voice->reader);
    voice->active = 0U;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
}

uint8_t brick6_sampler_preview_render(float *left, float *right,
                                      uint32_t frames)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    if ((left == NULL) || (right == NULL) || (frames == 0U)
            || (voice->active == 0U)) return 0U;
    brick6_sampler_render_sample_segment_cursor(voice, left, right, frames);
    return voice->active;
}
