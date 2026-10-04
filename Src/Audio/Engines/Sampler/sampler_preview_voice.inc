typedef struct
{
    brick6_sampler_voice_t voice;
    brick6_sampler_clip_runtime_t clip;
    brick6_sampler_clip_slot_t clip_slot;
    brick6_sampler_preview_kind_t kind;
    uint16_t runtime_id;
    float multi_gain;
    uint8_t multi_loop;
    uint8_t prepared;
} brick6_sampler_preview_runtime_t;

AUDIO_WARM static brick6_sampler_preview_runtime_t g_sampler_preview;
AUDIO_HISTORY_SDRAM static float
    g_sampler_preview_shifter_delay[2U][BRICK6_CLIP_SHIFTER_DELAY_FRAMES];

void brick6_sampler_preview_reset(void)
{
    sample_voice_reader_reset(&g_sampler_preview.voice.reader);
    memset(&g_sampler_preview, 0, sizeof(g_sampler_preview));
    brick6_clip_shifter_init(&g_sampler_preview.clip_slot.shifter,
                             g_sampler_preview_shifter_delay[0],
                             g_sampler_preview_shifter_delay[1]);
    g_sampler_preview.voice.note = STREAM_SAMPLER_ROOT_NOTE;
    g_sampler_preview.voice.owner_track_id = UINT8_MAX;
    g_sampler_preview.voice.velocity = 127U;
    g_sampler_preview.voice.gain = 1.0f;
    g_sampler_preview.voice.trigger_velocity_gain = 1.0f;
    g_sampler_preview.voice.length = 1.0f;
    g_sampler_preview.voice.ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_preview.voice.ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    g_sampler_preview.voice.multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_preview.voice.multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_preview.clip.gain = 1.0f;
    g_sampler_preview.clip.source_bpm = 120.0f;
    g_sampler_preview.clip.loop_enabled = 1U;
    g_sampler_preview.clip.grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_preview.clip.law_grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_preview.clip.shifter_heads = 2U;
    g_sampler_preview.clip.shifter_window = (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI;
    g_sampler_preview.clip.shifter_correction = 1.0f;
    g_sampler_preview.multi_gain = 1.0f;
}

uint8_t brick6_sampler_preview_prepare(brick6_sampler_preview_kind_t kind,
                                       uint16_t runtime_id)
{
    brick6_sampler_preview_reset();
    g_sampler_preview.kind = kind;
    g_sampler_preview.runtime_id = runtime_id;
    if (kind == BRICK6_SAMPLER_PREVIEW_RAM)
    {
        sampler_ram_audio_descriptor_t descriptor;
        if (!sampler_ram_audio_projection_resolve(runtime_id, &descriptor))
            return 0U;
    }
    else if (kind == BRICK6_SAMPLER_PREVIEW_STREAM)
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
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_preview.clip;
    switch (id)
    {
        case PARAM_SAMPLER_GAIN:
            voice->gain = value;
            clip->gain = value;
            g_sampler_preview.multi_gain = value;
            return 1U;
        case PARAM_SAMPLER_START: voice->start = value; return 1U;
        case PARAM_SAMPLER_LENGTH: voice->length = value; return 1U;
        case PARAM_SAMPLER_MODE: voice->mode = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_TUNE: voice->tune = value; return 1U;
        case PARAM_SAMPLER_LOOP_START: voice->loop_start = value; return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            uint8_t index = (uint8_t)(value + 0.5f);
            if (index >= (uint8_t)(sizeof(counts) / sizeof(counts[0])))
                index = (uint8_t)(sizeof(counts) / sizeof(counts[0]) - 1U);
            voice->slice_count = counts[index];
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SOURCE_BPM: clip->source_bpm = value; return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE: clip->play_mode = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_CLIP_LOOP: clip->loop_enabled = value >= 0.5f; return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE: clip->stretch_mode = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_CLIP_PITCH: clip->pitch_semitones = value; return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH: clip->sync_length = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_CLIP_GRAIN: clip->grain_size = (uint16_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_CLIP_SOURCE: clip->source = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SHIFTER_HEADS: clip->shifter_heads = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SHIFTER_WINDOW: clip->shifter_window = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SHIFTER_DISP: clip->shifter_dispersion = (uint8_t)(value + 0.5f); return 1U;
        case PARAM_SAMPLER_MULTI_LOOP: g_sampler_preview.multi_loop = value >= 0.5f; return 1U;
        default: return 0U;
    }
}

static uint8_t brick6_sampler_preview_trigger_ram(uint8_t note,
                                                   uint8_t velocity)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    uint16_t ram_slot;
    sampler_ram_audio_view_t ram;
    if (!brick6_sampler_runtime_resolve_ram_source(g_sampler_preview.runtime_id,
                                                   &ram_slot, &ram)) return 0U;
    uint32_t begin, end, loop_begin;
    brick6_sampler_runtime_build_effective_ram_bounds(ram.frames, voice->start,
        voice->length, voice->loop_start, voice->mode, 1U,
        &begin, &end, &loop_begin);
    if (voice->slice_count != 0U)
    {
        uint32_t slice_count = voice->slice_count;
        if (slice_count > 64U) slice_count = 64U;
        const uint32_t slice_index =
            brick6_sampler_runtime_note_to_slice_index(note, slice_count);
        brick6_sampler_runtime_build_effective_ram_slice_region(begin, end,
            slice_count, slice_index, &begin, &end);
        loop_begin = begin;
        voice->use_slice = 1U;
        voice->slice_index = (uint8_t)slice_index;
    }
    const uint8_t playback = brick6_sampler_runtime_ram_mode_playback(voice->mode);
    const uint8_t reverse = playback == BRICK6_SAMPLER_RAM_MODE_REVERSE_ONE;
    voice->note = note;
    voice->velocity = velocity;
    voice->sample_id = g_sampler_preview.runtime_id;
    voice->ram_slot = ram_slot;
    voice->ram_generation = ram.generation;
    voice->ram_data = ram.data;
    voice->ram_channels = ram.channels;
    voice->ram_format = ram.format;
    voice->ram_frames = ram.frames;
    voice->ram_sample_rate = ram.sample_rate;
    voice->region_begin = begin;
    voice->region_end = end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = end - loop_begin;
    voice->reverse = reverse;
    voice->position = (float)(reverse ? end - 1U : begin);
    voice->ram_position_q16 = (uint64_t)(reverse ? end - 1U : begin) << 16U;
    voice->ram_step_q16 = brick6_sampler_runtime_ram_step_q16(voice, &ram);
    voice->step_signed = (float)voice->ram_step_q16
        / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->loop_mode = brick6_sampler_runtime_ram_mode_to_loop_mode(
        voice->mode, begin, end, 1U);
    voice->trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(velocity);
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_RAM;
    voice->active = 1U;
    brick6_sampler_runtime_start_declick_fade_in(voice);
    return 1U;
}

static uint32_t brick6_sampler_preview_stream_timing(
    const sample_resolved_source_t *source)
{
    const brick6_sampler_clip_runtime_t *const clip = &g_sampler_preview.clip;
    if ((clip->stretch_mode == 0U) || (clip->sync_length == 0U))
        return BRICK6_SAMPLER_Q16_ONE;
    if (clip->sync_length == 4U)
        return brick6_sampler_runtime_clip_ratio_q16(clip->source_bpm);
    const uint8_t bars = (clip->sync_length == 1U) ? 1U
        : (clip->sync_length == 2U) ? 2U
        : (clip->sync_length == 3U) ? 4U : 0U;
    const float bpm = (float)audio_transport_runtime_get()
        ->tempo_effective_bpm_milli * 0.001f;
    if ((bars == 0U) || (bpm <= 0.0f)) return BRICK6_SAMPLER_Q16_ONE;
    const float source_seconds = (float)(source->region_end
        - source->region_begin)
        / (float)BOARD_AUDIO_SAMPLE_RATE_HZ;
    const float target_seconds = (float)bars * 240.0f / bpm;
    float ratio = source_seconds / target_seconds;
    if (ratio < 0.5f) ratio = 0.5f;
    if (ratio > 2.0f) ratio = 2.0f;
    return brick6_sampler_runtime_ratio_to_q16(ratio);
}

static uint8_t brick6_sampler_preview_bind_source(
    sample_resolved_source_t *source, uint8_t note, uint8_t velocity,
    uint8_t source_kind, float gain, uint8_t loop)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    const float pitch = brick6_sampler_runtime_pitch_ratio(
        (float)((int16_t)note - (int16_t)source->root_note)
        + ((source_kind == BRICK6_SAMPLER_VOICE_CLIP)
            ? g_sampler_preview.clip.pitch_semitones : 0.0f));
    float timing = 1.0f;
    if (source_kind == BRICK6_SAMPLER_VOICE_CLIP)
    {
        const uint32_t begin = brick6_sampler_runtime_clamp_region_begin(
            source->total_frames, voice->start);
        source->region_begin = begin;
        source->region_end = begin + brick6_sampler_runtime_clamp_region_length(
            source->total_frames, begin, voice->length);
        if (source->region_end <= source->region_begin)
        {
            source->region_begin = 0U;
            source->region_end = source->total_frames;
        }
        timing = (float)brick6_sampler_preview_stream_timing(source)
            / (float)BRICK6_SAMPLER_Q16_ONE;
    }
    const uint8_t use_shifter = (uint8_t)((source_kind
        == BRICK6_SAMPLER_VOICE_CLIP)
        && (g_sampler_preview.clip.stretch_mode == 2U));
    source->rate = use_shifter ? timing : pitch * timing;
    source->loop_mode = loop ? SAMPLE_PLAY_LOOP_FORWARD : SAMPLE_PLAY_LOOP_NONE;
    if ((source->loop_end <= source->loop_begin)
            || (source->loop_end > source->region_end))
    {
        source->loop_begin = source->region_begin;
        source->loop_end = source->region_end;
    }
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
    voice->gain = gain;
    voice->trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(velocity);
    voice->region_begin = plan.region_begin;
    voice->region_end = plan.region_end;
    voice->loop_begin = plan.loop_begin;
    voice->loop_frames = plan.region_end - plan.region_begin;
    voice->loop_mode = plan.loop_mode;
    voice->play_plan = plan;
    voice->format = plan.format;
    voice->stride_floats = plan.stride_floats;
    voice->frames_per_page = plan.frames_per_page;
    voice->registration_epoch = plan.registration_epoch;
    voice->position = (float)plan.start_frame;
    voice->active = 1U;
    if (source_kind == BRICK6_SAMPLER_VOICE_CLIP)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_preview.clip;
        clip->timing_ratio_q16 = brick6_sampler_runtime_ratio_to_q16(timing);
        clip->pitch_ratio_q16 = brick6_sampler_runtime_ratio_to_q16(pitch);
        clip->shifter_correction = use_shifter ? (pitch / timing) : 1.0f;
        clip->law_grain_size = brick6_sampler_runtime_clip_law_grain_size(
            clip->timing_ratio_q16);
        clip->use_shifter_engine = use_shifter;
        if (use_shifter != 0U)
            brick6_sampler_runtime_clip_reset_shifter(
                clip, &g_sampler_preview.clip_slot);
    }
    brick6_sampler_runtime_start_declick_fade_in(voice);
    return 1U;
}

uint8_t brick6_sampler_preview_note_on(uint8_t note, uint8_t velocity)
{
    if ((g_sampler_preview.prepared == 0U) || (note > 127U)
            || (velocity == 0U) || (velocity > 127U)) return 0U;
    sample_voice_reader_reset(&g_sampler_preview.voice.reader);
    if (g_sampler_preview.kind == BRICK6_SAMPLER_PREVIEW_RAM)
        return brick6_sampler_preview_trigger_ram(note, velocity);
    if (g_sampler_preview.kind == BRICK6_SAMPLER_PREVIEW_STREAM)
    {
        sample_resolved_source_t source;
        if (!sample_classic_audio_projection_resolve(
                g_sampler_preview.runtime_id, &source)) return 0U;
        return brick6_sampler_preview_bind_source(&source, note, velocity,
            BRICK6_SAMPLER_VOICE_CLIP, g_sampler_preview.clip.gain,
            g_sampler_preview.clip.loop_enabled);
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
    source.region_end = projected.total_frames;
    source.loop_begin = projected.loop_begin;
    source.loop_end = projected.loop_end;
    const float velocity_gain = projected.zone_is_single_velocity_layer
        ? brick6_sampler_runtime_velocity_gain(velocity) : 1.0f;
    const uint8_t result = brick6_sampler_preview_bind_source(&source,
        note, velocity, BRICK6_SAMPLER_VOICE_MULTI,
        g_sampler_preview.multi_gain, g_sampler_preview.multi_loop);
    if (result != 0U)
        g_sampler_preview.voice.trigger_velocity_gain = velocity_gain;
    return result;
}

void brick6_sampler_preview_note_off(uint8_t note)
{
    (void)note;
}

uint8_t brick6_sampler_preview_render(float *left, float *right,
                                      uint32_t frames)
{
    brick6_sampler_voice_t *const voice = &g_sampler_preview.voice;
    if ((left == NULL) || (right == NULL) || (frames == 0U)
            || (voice->active == 0U)) return 0U;
    if (voice->source_kind == BRICK6_SAMPLER_VOICE_RAM)
        brick6_sampler_runtime_render_ram(voice, left, right, frames);
    else if ((voice->source_kind == BRICK6_SAMPLER_VOICE_CLIP)
            && (g_sampler_preview.clip.use_shifter_engine != 0U))
        brick6_sampler_runtime_clip_render_shifter(voice,
            &g_sampler_preview.clip, &g_sampler_preview.clip_slot,
            left, right, frames);
    else brick6_sampler_render_sample_segment_cursor(voice, left, right, frames);
    return voice->active;
}
