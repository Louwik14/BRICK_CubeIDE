static float brick6_sampler_runtime_velocity_gain(uint8_t velocity)
{
    return (float)velocity * (1.0f / 127.0f);
}

static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio)
{
    if (ratio < 0.03125f) ratio = 0.03125f;
    else if (ratio > 32.0f) ratio = 32.0f;
    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

static float brick6_sampler_runtime_pitch_ratio(float semitones)
{
    float octaves = semitones * (1.0f / 12.0f);
    int32_t octave = (int32_t)octaves;
    if ((octaves < 0.0f) && ((float)octave != octaves)) --octave;
    if (octave < -30) octave = -30;
    if (octave > 30) octave = 30;
    const float x = octaves - (float)octave;
    const float fraction = 1.0f + x * (0.6931471806f
        + x * (0.2402265070f + x * (0.0555041087f
        + x * (0.0096181291f + x * 0.0013333558f))));
    union { uint32_t u; float f; } power = {
        .u = (uint32_t)(octave + 127) << 23U
    };
    return power.f * fraction;
}

static uint32_t brick6_sampler_runtime_next_trigger_order(void)
{
    if (++g_sampler_voice_trigger_counter == 0U)
        g_sampler_voice_trigger_counter = 1U;
    return g_sampler_voice_trigger_counter;
}

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id)
{
    return (uint8_t)(BRICK6_SAMPLER_CACHE_VOICE_BASE + track_id);
}

static uint32_t brick6_sampler_runtime_clamp_region_begin(
    uint32_t length_frames, float start)
{
    uint32_t begin = (uint32_t)(start * (float)length_frames);
    if (begin >= length_frames)
        begin = (length_frames > 0U) ? (length_frames - 1U) : 0U;
    return begin;
}

static uint32_t brick6_sampler_runtime_clamp_region_length(
    uint32_t length_frames, uint32_t region_begin, float length)
{
    uint32_t resolved = (uint32_t)(length * (float)length_frames);
    if (resolved == 0U) resolved = 1U;
    const uint32_t available = length_frames - region_begin;
    return (resolved > available) ? available : resolved;
}

static float brick6_sampler_runtime_fade_gain(uint32_t frame_index,
                                               uint32_t loop_frames,
                                               uint32_t fade_in_frames,
                                               uint32_t fade_out_frames)
{
    if (loop_frames == 0U) return 1.0f;
    float gain = 1.0f;
    if ((fade_in_frames > 0U) && (frame_index < fade_in_frames))
        gain *= (float)frame_index / (float)fade_in_frames;
    if ((fade_out_frames > 0U)
        && (frame_index >= (loop_frames - fade_out_frames)))
    {
        const uint32_t pos = frame_index - (loop_frames - fade_out_frames);
        gain *= 1.0f - ((float)pos / (float)fade_out_frames);
    }
    return (gain < 0.0f) ? 0.0f : gain;
}

static uint8_t brick6_sampler_runtime_is_terminal_position(
    const brick6_sampler_voice_t *voice, float position)
{
    if (voice == NULL) return 1U;
    if (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE) return 0U;
    return (position >= (float)voice->region_end) ? 1U : 0U;
}
