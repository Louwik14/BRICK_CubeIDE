static uint8_t brick6_sampler_runtime_resolve_grid_count(uint8_t raw_grid_count)
{
    switch (raw_grid_count)
    {
        case 0U:
            return 0U;
        case 1U:
        case 2U:
            return 2U;
        case 3U:
        case 4U:
            return 4U;
        case 5U:
        case 8U:
            return 8U;
        case 6U:
        case 16U:
            return 16U;
        case 32U:
            return 32U;
        case 64U:
            return 64U;
        default:
            break;
    }

    if (raw_grid_count <= 2U)
    {
        return 2U;
    }
    if (raw_grid_count <= 4U)
    {
        return 4U;
    }
    if (raw_grid_count <= 8U)
    {
        return 8U;
    }
    if (raw_grid_count <= 16U)
    {
        return 16U;
    }
    if (raw_grid_count <= 32U)
    {
        return 32U;
    }

    return 64U;
}

static float brick6_sampler_runtime_velocity_gain(uint8_t velocity)
{
    return (float)velocity * (1.0f / 127.0f);
}

static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio)
{
    if (ratio < 0.03125f)
    {
        ratio = 0.03125f;
    }
    else if (ratio > 32.0f)
    {
        ratio = 32.0f;
    }

    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

/* Bounded 2^x approximation for the dated tune path.  Range reduction keeps
 * the fifth-order polynomial on [0, 1); no libm call is made by the setter. */
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

static uint32_t brick6_sampler_runtime_ram_step_q16(const brick6_sampler_voice_t *voice,
                                                    const sampler_ram_audio_view_t *ram)
{
    const float tune = (voice != NULL) ? voice->tune : 0.0f;
    const int32_t note = (voice != NULL) ? (int32_t)voice->note : 60;
    const uint32_t source_rate = ((ram != NULL) && (ram->sample_rate != 0U))
                                     ? ram->sample_rate
                                     : BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float rate_ratio = (float)source_rate
                             / (float)BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float pitch_ratio = brick6_sampler_runtime_pitch_ratio(
        (float)(note - 60) + tune);

    return brick6_sampler_runtime_ratio_to_q16(rate_ratio * pitch_ratio);
}

static uint32_t brick6_sampler_runtime_ram_slice_step_q16(const brick6_sampler_voice_t *voice,
                                                          const sampler_ram_audio_view_t *ram)
{
    const float tune = (voice != NULL) ? voice->tune : 0.0f;
    const uint32_t source_rate = ((ram != NULL) && (ram->sample_rate != 0U))
                                     ? ram->sample_rate
                                     : BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float rate_ratio = (float)source_rate
                             / (float)BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float pitch_ratio = brick6_sampler_runtime_pitch_ratio(tune);

    return brick6_sampler_runtime_ratio_to_q16(rate_ratio * pitch_ratio);
}

static uint8_t brick6_sampler_runtime_note_to_slice_index(uint8_t note, uint32_t slice_count)
{
    const uint8_t reference_note = 60U;
    uint32_t index = 0U;
    if (note > reference_note)
    {
        index = (uint32_t)(note - reference_note);
    }
    if ((slice_count != 0U) && (index >= slice_count))
    {
        index = slice_count - 1U;
    }
    return (uint8_t)index;
}

static uint32_t brick6_sampler_runtime_next_trigger_order(void)
{
    g_sampler_voice_trigger_counter++;
    if (g_sampler_voice_trigger_counter == 0U)
    {
        g_sampler_voice_trigger_counter = 1U;
    }
    return g_sampler_voice_trigger_counter;
}

static void brick6_sampler_runtime_trigger_ram_slice(uint8_t track_id)
{
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    uint16_t ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    sampler_ram_audio_view_t ram_value;
    const sampler_ram_audio_view_t *const ram = &ram_value;

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_reset(&voice->reader);

    if (brick6_sampler_runtime_resolve_ram_source(voice->sample_id,
                                                  &ram_slot,
                                                  &ram_value) != 0U)
    {
        uint32_t slice_count = (uint32_t)brick6_sampler_runtime_resolve_grid_count(
            voice->slice_count);
        if (slice_count == 0U)
        {
            slice_count = 1U;
        }
        if (slice_count > 64U)
        {
            slice_count = 64U;
        }
        uint32_t base_begin = 0U;
        uint32_t base_end = ram->frames;
        brick6_sampler_runtime_build_effective_ram_region(ram->frames,
                                                          voice->start,
                                                          voice->length,
                                                          &base_begin,
                                                          &base_end);
        const uint8_t slice_index =
            brick6_sampler_runtime_note_to_slice_index(voice->note, slice_count);
        uint32_t region_begin = base_begin;
        uint32_t region_end = base_end;
        brick6_sampler_runtime_build_effective_ram_slice_region(base_begin,
                                                                base_end,
                                                                slice_count,
                                                                (uint32_t)slice_index,
                                                                &region_begin,
                                                                &region_end);
        if ((region_end > region_begin) && (region_end <= ram->frames))
        {
            const uint8_t mode = voice->mode;
            const uint8_t reverse = (mode == 1U) ? 1U : 0U;
            uint32_t loop_begin = region_begin;
            const uint8_t loop_mode =
                brick6_sampler_runtime_ram_mode_to_loop_mode(mode,
                                                             region_begin,
                                                             region_end,
                                                             1U);
            const uint32_t step_q16 = brick6_sampler_runtime_ram_slice_step_q16(voice, ram);
            voice->active = 1U;
            brick6_sampler_runtime_mark_track_activity(track_id);
            voice->owner_track_id = track_id;
            voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->ram_slot = ram_slot;
            voice->ram_generation = ram->generation;
            voice->ram_data = ram->data;
            voice->ram_channels = ram->channels;
            voice->ram_format = ram->format;
            voice->ram_frames = ram->frames;
            voice->ram_sample_rate = ram->sample_rate;
            voice->position = (float)((reverse != 0U) ? (region_end - 1U) : region_begin);
            voice->ram_position_q16 =
                (uint64_t)((reverse != 0U) ? (region_end - 1U) : region_begin) << 16U;
            voice->region_begin = region_begin;
            voice->region_end = region_end;
            voice->loop_begin = loop_begin;
            voice->loop_frames = region_end - loop_begin;
            voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
            voice->ram_step_q16 = step_q16;
            voice->loop_mode = loop_mode;
            voice->reverse = reverse;
            voice->use_slice = 1U;
            voice->slice_index = slice_index;
            voice->use_segment_cursor = 0U;
            voice->release_pending = 0U;
            voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_RAM;
            voice->trigger_velocity_gain =
                brick6_sampler_runtime_velocity_gain(voice->velocity);
            voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
            memset(&voice->play_plan, 0, sizeof(voice->play_plan));
            brick6_sampler_runtime_start_declick_fade_in(voice);
            return;
        }
    }

    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    memset(&voice->play_plan, 0, sizeof(voice->play_plan));
}

static uint8_t brick6_sampler_runtime_resolve_ram_source(uint16_t global_slot,
                                                         uint16_t *out_ram_slot,
                                                         sampler_ram_audio_view_t *out_ram)
{
    if (out_ram_slot != NULL)
    {
        *out_ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    }
    if (out_ram != NULL)
    {
        memset(out_ram, 0, sizeof(*out_ram));
    }

    sampler_ram_audio_descriptor_t projected;
    if (sampler_ram_audio_projection_resolve(global_slot, &projected) == 0U)
    {
        return 0U;
    }
    if ((projected.ram_slot >= SAMPLER_RAM_AUDIO_MAX_SLOTS)
        || ((projected.format != SAMPLER_RAM_FORMAT_FLOAT32_MONO)
            && (projected.format != SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED))
        || (projected.data.region == AUDIO_SHARED_REGION_NONE) || (projected.frames == 0U)
        || (projected.channels != sampler_ram_audio_format_channels(projected.format)))
    {
        return 0U;
    }
    const uint16_t ram_slot = projected.ram_slot;

    if (out_ram_slot != NULL)
    {
        *out_ram_slot = ram_slot;
    }
    if (out_ram != NULL)
    {
        out_ram->global_slot = projected.global_slot;
        out_ram->format = projected.format;
        out_ram->channels = projected.channels;
        out_ram->sample_rate = projected.sample_rate;
        out_ram->frames = projected.frames;
        out_ram->bytes_per_frame = projected.bytes_per_frame;
        out_ram->data = (float *)audio_shared_memory_consume(&projected.data);
        if (out_ram->data == NULL) return 0U;
        out_ram->data_offset = projected.data_offset;
        out_ram->generation = projected.generation;
    }
    return 1U;
}

static uint8_t brick6_sampler_runtime_ram_voice_snapshot(
    const brick6_sampler_voice_t *voice,
    sampler_ram_audio_view_t *out_slot)
{
    if (out_slot != NULL) memset(out_slot, 0, sizeof(*out_slot));
    if ((voice == NULL) || (out_slot == NULL)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (voice->ram_data == NULL) || (voice->ram_frames == 0U)
        || ((voice->ram_format != SAMPLER_RAM_FORMAT_FLOAT32_MONO)
            && (voice->ram_format != SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED))
        || (voice->ram_channels != sampler_ram_audio_format_channels(voice->ram_format)))
    {
        return 0U;
    }
    out_slot->global_slot = voice->sample_id;
    out_slot->format = voice->ram_format;
    out_slot->channels = voice->ram_channels;
    out_slot->sample_rate = voice->ram_sample_rate;
    out_slot->frames = voice->ram_frames;
    out_slot->bytes_per_frame = sampler_ram_audio_format_bytes_per_frame(voice->ram_format);
    out_slot->data = (float *)voice->ram_data;
    out_slot->generation = voice->ram_generation;
    return 1U;
}

static uint8_t brick6_sampler_runtime_trigger_ram(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const uint16_t global_slot = voice->sample_id;
    uint16_t ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    sampler_ram_audio_view_t ram_value;
    const sampler_ram_audio_view_t *const ram = &ram_value;
    if (brick6_sampler_runtime_resolve_ram_source(global_slot, &ram_slot, &ram_value) == 0U)
    {
        return 0U;
    }

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_reset(&voice->reader);

    uint32_t region_begin = 0U;
    uint32_t region_end = ram->frames;
    uint32_t loop_begin = 0U;
    const uint8_t mode = voice->mode;
    brick6_sampler_runtime_build_effective_ram_bounds(ram->frames,
                                                      voice->start,
                                                      voice->length,
                                                      voice->loop_start,
                                                      mode,
                                                      1U,
                                                      &region_begin,
                                                      &region_end,
                                                      &loop_begin);
    const uint8_t reverse = (mode == 1U) ? 1U : 0U;
    const uint32_t step_q16 = brick6_sampler_runtime_ram_step_q16(voice, ram);
    const uint8_t loop_mode =
        brick6_sampler_runtime_ram_mode_to_loop_mode(mode,
                                                     region_begin,
                                                     region_end,
                                                     1U);

    voice->active = 1U;
    brick6_sampler_runtime_mark_track_activity(track_id);
    voice->owner_track_id = track_id;
    voice->sample_id = global_slot;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = ram_slot;
    voice->ram_generation = ram->generation;
    voice->ram_data = ram->data;
    voice->ram_channels = ram->channels;
    voice->ram_format = ram->format;
    voice->ram_frames = ram->frames;
    voice->ram_sample_rate = ram->sample_rate;
    voice->position = (float)((reverse != 0U) ? (region_end - 1U) : region_begin);
    voice->ram_position_q16 =
        (uint64_t)((reverse != 0U) ? (region_end - 1U) : region_begin) << 16U;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = region_end - loop_begin;
    voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->ram_step_q16 = step_q16;
    voice->loop_mode = loop_mode;
    voice->reverse = reverse;
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_RAM;
    voice->trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(voice->velocity);
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    brick6_sampler_runtime_start_declick_fade_in(voice);
    return 1U;
}

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id)
{
    return (uint8_t)(BRICK6_SAMPLER_CACHE_VOICE_BASE + track_id);
}

static uint32_t brick6_sampler_runtime_clamp_region_begin(uint32_t length_frames, float start)
{
    uint32_t begin = (uint32_t)(start * (float)length_frames);
    if (begin >= length_frames)
    {
        begin = (length_frames > 0U) ? (length_frames - 1U) : 0U;
    }
    return begin;
}

static uint32_t brick6_sampler_runtime_clamp_region_length(uint32_t length_frames,
                                                           uint32_t region_begin,
                                                           float length)
{
    uint32_t resolved_length = (uint32_t)(length * (float)length_frames);
    if (resolved_length == 0U)
    {
        resolved_length = 1U;
    }
    const uint32_t available = length_frames - region_begin;
    return (resolved_length > available) ? available : resolved_length;
}

static void brick6_sampler_runtime_build_effective_ram_region(uint32_t length_frames,
                                                              float start,
                                                              float length,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = length_frames;
    if (length_frames != 0U)
    {
        region_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, start);
        region_end = region_begin
                     + brick6_sampler_runtime_clamp_region_length(length_frames,
                                                                  region_begin,
                                                                  length);
    }
    if (out_begin != NULL)
    {
        *out_begin = region_begin;
    }
    if (out_end != NULL)
    {
        *out_end = region_end;
    }
}

static void brick6_sampler_runtime_build_effective_ram_bounds(uint32_t length_frames,
                                                              float start,
                                                              float length,
                                                              float loop_start,
                                                              uint8_t mode,
                                                              uint8_t use_loop_marker,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end,
                                                              uint32_t *out_loop_begin)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = length_frames;
    uint32_t loop_begin = 0U;
    if (length_frames != 0U)
    {
        region_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, start);
        region_end = region_begin
                     + brick6_sampler_runtime_clamp_region_length(length_frames,
                                                                  region_begin,
                                                                  length);
        loop_begin = region_begin;

        if ((mode == 2U) && (use_loop_marker != 0U))
        {
            loop_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, loop_start);
            if (loop_begin < region_begin)
            {
                loop_begin = region_begin;
            }
            if (loop_begin >= length_frames)
            {
                loop_begin = length_frames - 1U;
            }
            if (region_end <= loop_begin)
            {
                region_end = loop_begin + 1U;
                if (region_end > length_frames)
                {
                    region_end = length_frames;
                    loop_begin = (length_frames > 1U) ? (length_frames - 1U) : 0U;
                    if (region_begin > loop_begin)
                    {
                        region_begin = loop_begin;
                    }
                }
            }
        }
        else if (region_end <= region_begin)
        {
            region_end = region_begin + 1U;
            if (region_end > length_frames)
            {
                region_end = length_frames;
                region_begin = (length_frames > 1U) ? (length_frames - 1U) : 0U;
            }
            loop_begin = region_begin;
        }

        if (region_end <= region_begin)
        {
            region_begin = 0U;
            region_end = (length_frames != 0U) ? 1U : 0U;
            loop_begin = region_begin;
        }
        if (loop_begin < region_begin)
        {
            loop_begin = region_begin;
        }
        if (loop_begin >= region_end)
        {
            loop_begin = region_begin;
        }
    }
    if (out_begin != NULL)
    {
        *out_begin = region_begin;
    }
    if (out_end != NULL)
    {
        *out_end = region_end;
    }
    if (out_loop_begin != NULL)
    {
        *out_loop_begin = loop_begin;
    }
}

static void brick6_sampler_runtime_build_effective_ram_slice_region(uint32_t region_begin,
                                                                    uint32_t region_end,
                                                                    uint32_t slice_count,
                                                                    uint32_t slice_index,
                                                                    uint32_t *out_begin,
                                                                    uint32_t *out_end)
{
    uint32_t slice_begin = region_begin;
    uint32_t slice_end = region_end;
    const uint32_t region_frames = (region_end > region_begin) ? (region_end - region_begin) : 0U;
    if ((region_frames != 0U) && (slice_count != 0U))
    {
        if (slice_index >= slice_count)
        {
            slice_index = slice_count - 1U;
        }
        slice_begin = region_begin
                      + (uint32_t)(((uint64_t)region_frames * (uint64_t)slice_index)
                                   / (uint64_t)slice_count);
        slice_end = ((slice_index + 1U) >= slice_count)
                        ? region_end
                        : (region_begin
                           + (uint32_t)(((uint64_t)region_frames
                                        * (uint64_t)(slice_index + 1U))
                                       / (uint64_t)slice_count));
        if (slice_end <= slice_begin)
        {
            if (slice_begin >= region_end)
            {
                slice_begin = region_end - 1U;
            }
            slice_end = slice_begin + 1U;
            if (slice_end > region_end)
            {
                slice_end = region_end;
            }
        }
    }
    if (out_begin != NULL)
    {
        *out_begin = slice_begin;
    }
    if (out_end != NULL)
    {
        *out_end = slice_end;
    }
}

static uint8_t brick6_sampler_runtime_ram_mode_to_loop_mode(uint8_t mode,
                                                            uint32_t region_begin,
                                                            uint32_t region_end,
                                                            uint8_t loop_valid)
{
    (void)loop_valid;
    if ((mode == 2U) && (region_end > region_begin))
    {
        return (uint8_t)BRICK6_SAMPLER_LOOP_FORWARD;
    }
    if (mode == 3U)
    {
        return ((region_end - region_begin) > 1U)
                   ? (uint8_t)BRICK6_SAMPLER_LOOP_PINGPONG
                   : (uint8_t)BRICK6_SAMPLER_LOOP_NONE;
    }
    return (uint8_t)BRICK6_SAMPLER_LOOP_NONE;
}

static void brick6_sampler_runtime_reconcile_ram_voice_bounds_live(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    sampler_ram_audio_view_t ram_snapshot;
    const sampler_ram_audio_view_t *const ram = &ram_snapshot;
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (brick6_sampler_runtime_ram_voice_snapshot(voice, &ram_snapshot) == 0U))
    {
        return;
    }

    uint32_t region_begin = 0U;
    uint32_t region_end = ram->frames;
    uint32_t loop_begin = 0U;
    const uint8_t slice_voice_active = ((voice->use_slice != 0U) && (voice->slice_count != 0U))
                                           ? 1U
                                           : 0U;
    brick6_sampler_runtime_build_effective_ram_bounds(ram->frames,
                                                      voice->start,
                                                      voice->length,
                                                      voice->loop_start,
                                                      voice->mode,
                                                      (slice_voice_active == 0U) ? 1U : 0U,
                                                      &region_begin,
                                                      &region_end,
                                                      &loop_begin);
    if (slice_voice_active != 0U)
    {
        const uint32_t slice_count =
            (uint32_t)brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
        const uint32_t clamped_slice_count = (slice_count == 0U) ? 1U : slice_count;
        const uint32_t base_begin = region_begin;
        const uint32_t base_end = region_end;
        const uint32_t slice_index =
            ((uint32_t)voice->slice_index < clamped_slice_count)
                ? (uint32_t)voice->slice_index
                : 0U;
        brick6_sampler_runtime_build_effective_ram_slice_region(base_begin,
                                                                base_end,
                                                                clamped_slice_count,
                                                                slice_index,
                                                                &region_begin,
                                                                &region_end);
        loop_begin = region_begin;
    }

    const uint8_t loop_mode =
        brick6_sampler_runtime_ram_mode_to_loop_mode(voice->mode,
                                                     region_begin,
                                                     region_end,
                                                     1U);
    const uint64_t begin_q16 = (uint64_t)region_begin << 16U;
    const uint64_t end_q16 = (uint64_t)region_end << 16U;
    uint64_t position_q16 = voice->ram_position_q16;
    if ((position_q16 == 0ULL) && (voice->position > 0.0f))
    {
        position_q16 = (uint64_t)(voice->position * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
    }

    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = region_end - loop_begin;
    voice->loop_mode = loop_mode;
    if (voice->mode == 1U)
    {
        voice->reverse = 1U;
    }
    else if (loop_mode != (uint8_t)BRICK6_SAMPLER_LOOP_PINGPONG)
    {
        voice->reverse = 0U;
    }

    uint64_t reconciled_q16 = position_q16;
    uint8_t recaled = 0U;
    if (voice->mode == 2U)
    {
        voice->reverse = 0U;
        if (position_q16 < begin_q16)
        {
            reconciled_q16 = begin_q16;
            recaled = 1U;
        }
        else if (position_q16 >= end_q16)
        {
            reconciled_q16 = (uint64_t)loop_begin << 16U;
            recaled = 1U;
        }
    }
    else if (voice->mode == 3U)
    {
        if (position_q16 < begin_q16)
        {
            reconciled_q16 = begin_q16;
            voice->reverse = 0U;
            recaled = 1U;
        }
        else if (position_q16 >= end_q16)
        {
            reconciled_q16 = (uint64_t)(region_end - 1U) << 16U;
            voice->reverse = 1U;
            recaled = 1U;
        }
    }
    else if (position_q16 < begin_q16)
    {
        reconciled_q16 = begin_q16;
        recaled = 1U;
    }

    if ((voice->mode <= 1U) && (position_q16 >= end_q16))
    {
        reconciled_q16 = (uint64_t)(region_end - 1U) << 16U;
        recaled = 1U;
    }

    if (recaled != 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(track_id, voice);
        voice->ram_position_q16 = reconciled_q16;
        voice->position = (float)reconciled_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
        brick6_sampler_runtime_start_declick_fade_in(voice);
    }
}

static void brick6_sampler_runtime_reproject_ram_voice_tune_live(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    sampler_ram_audio_view_t ram_snapshot;
    const sampler_ram_audio_view_t *const ram = &ram_snapshot;
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (brick6_sampler_runtime_ram_voice_snapshot(voice, &ram_snapshot) == 0U))
    {
        return;
    }

    const uint8_t slice_voice_active = ((voice->use_slice != 0U) && (voice->slice_count != 0U))
                                           ? 1U
                                           : 0U;
    const uint32_t step_q16 = (slice_voice_active != 0U)
                                  ? brick6_sampler_runtime_ram_slice_step_q16(voice, ram)
                                  : brick6_sampler_runtime_ram_step_q16(voice, ram);
    voice->ram_step_q16 = step_q16;
    voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
}

static float brick6_sampler_runtime_fade_gain(uint32_t frame_index,
                                              uint32_t loop_frames,
                                              uint32_t fade_in_frames,
                                              uint32_t fade_out_frames)
{
    if (loop_frames == 0U)
    {
        return 1.0f;
    }

    float gain = 1.0f;
    if ((fade_in_frames > 0U) && (frame_index < fade_in_frames))
    {
        gain *= (float)frame_index / (float)fade_in_frames;
    }

    if ((fade_out_frames > 0U) && (frame_index >= (loop_frames - fade_out_frames)))
    {
        const uint32_t fade_pos = frame_index - (loop_frames - fade_out_frames);
        gain *= 1.0f - ((float)fade_pos / (float)fade_out_frames);
    }

    return (gain < 0.0f) ? 0.0f : gain;
}

static uint8_t brick6_sampler_runtime_is_terminal_position(const brick6_sampler_voice_t *voice, float position)
{
    if (voice == NULL)
    {
        return 1U;
    }

    if (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE)
    {
        return 0U;
    }

    if (voice->reverse != 0U)
    {
        return (position < (float)voice->region_begin) ? 1U : 0U;
    }

    return (position >= (float)voice->region_end) ? 1U : 0U;
}
