static void brick6_sampler_runtime_render_ram_unpitched(brick6_sampler_voice_t *voice,
                                                        const sampler_ram_audio_view_t *ram,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames,
                                                        uint32_t position)
{
    const uint32_t region_begin = voice->region_begin;
    const uint32_t region_end = voice->region_end;
    const uint32_t loop_begin = ((voice->loop_begin >= region_begin)
                                 && (voice->loop_begin < region_end))
                                    ? voice->loop_begin
                                    : region_begin;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float fade_buf[AUDIO_BLOCK_SIZE];
    const uint8_t use_fade = (voice->start_fade_remaining != 0U) ? 1U : 0U;
    if (use_fade != 0U)
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            fade_buf[i] = 1.0f;
        }
        (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, frames);
    }

    float last_l = 0.0f;
    float last_r = 0.0f;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;
    while (produced < frames)
    {
        if ((voice->reverse == 0U) && (position >= region_end))
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position = loop_begin;
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((voice->reverse != 0U) && (position < region_begin))
        {
            terminal = 1U;
            break;
        }

        const uint32_t remaining = (voice->reverse != 0U)
                                       ? (position - region_begin + 1U)
                                       : (region_end - position);
        uint32_t todo = frames - produced;
        if (todo > remaining)
        {
            todo = remaining;
        }
        if (todo == 0U)
        {
            break;
        }

        for (uint32_t i = 0U; i < todo; ++i)
        {
            const uint32_t frame_index = (voice->reverse != 0U)
                                             ? (position - i)
                                             : (position + i);
            float gain = render_gain;
            if (use_fade != 0U)
            {
                gain *= fade_buf[produced + i];
            }
            const uint32_t src = frame_index * frame_stride;
            last_l = data[src] * gain;
            last_r = data[src + right_offset] * gain;
            out_l[produced + i] += last_l;
            out_r[produced + i] += last_r;
        }

        produced += todo;
        if (todo >= remaining)
        {
            if ((voice->reverse == 0U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD))
            {
                position = loop_begin;
            }
            else
            {
                terminal = 1U;
            }
        }
        else if (voice->reverse != 0U)
        {
            position -= todo;
        }
        else
        {
            position += todo;
        }
        if (terminal != 0U)
        {
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    if (terminal != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }
    voice->ram_position_q16 = (uint64_t)position << 16U;
    voice->position = (float)position;
}

static inline float brick6_sampler_runtime_q16_frac_float(uint64_t position_q16)
{
    const uint32_t frac_q16 = (uint32_t)position_q16 & BRICK6_SAMPLER_Q16_FRAC_MASK;
    return (float)frac_q16 * (1.0f / (float)BRICK6_SAMPLER_Q16_ONE);
}

static inline float brick6_sampler_runtime_q16_to_float(uint64_t position_q16)
{
    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
    return (float)frame_index + brick6_sampler_runtime_q16_frac_float(position_q16);
}

/* Exact bounded ceil/floor quotients used only to cap a render segment.  The
 * IRQ never needs the full 64-bit quotient: once it reaches the remaining
 * block length, the exact larger value is irrelevant. */
static uint32_t brick6_sampler_runtime_bounded_steps(uint64_t distance_q16,
                                                     uint32_t step_q16,
                                                     uint32_t limit,
                                                     uint8_t include_endpoint)
{
    uint32_t count = 0U;
    if ((step_q16 == 0U) || (limit == 0U)) return 0U;
    while (count < limit)
    {
        if (include_endpoint != 0U)
        {
            if (distance_q16 < (uint64_t)step_q16) break;
            distance_q16 -= (uint64_t)step_q16;
            count++;
        }
        else
        {
            count++;
            if (distance_q16 <= (uint64_t)step_q16) return count;
            distance_q16 -= (uint64_t)step_q16;
        }
    }
    return count + ((count < limit) ? 1U : 0U);
}

static uint8_t brick6_sampler_runtime_render_ram_forward_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U) || (voice->reverse != 0U)
        || (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint32_t loop_begin = ((voice->loop_begin >= voice->region_begin)
                                 && (voice->loop_begin < voice->region_end))
                                    ? voice->loop_begin
                                    : voice->region_begin;
    const uint64_t begin_q16 = (uint64_t)loop_begin << 16U;
    const uint64_t end_q16 = (uint64_t)voice->region_end << 16U;
    const uint64_t span_q16 = end_q16 - begin_q16;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((span_q16 == 0ULL) || (step_q16 == 0U))
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;

    while (produced < frames)
    {
        if (position_q16 >= end_q16)
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position_q16 = begin_q16
                               + brick6_sampler_runtime_wrap_q16(position_q16 - begin_q16,
                                                                 span_q16);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }

        uint32_t todo = frames - produced;
        uint8_t safe_segment = 0U;
        if (position_q16 < last_q16)
        {
            const uint64_t distance_to_last_q16 = last_q16 - position_q16;
            const uint32_t until_last = brick6_sampler_runtime_bounded_steps(
                distance_to_last_q16, step_q16, todo, 0U);
            if (todo > until_last)
            {
                todo = until_last;
            }
            safe_segment = 1U;
        }
        else
        {
            const uint64_t distance_to_end_q16 = end_q16 - position_q16;
            uint32_t until_end = brick6_sampler_runtime_bounded_steps(
                distance_to_end_q16, step_q16, todo, 0U);
            if (until_end == 0U)
            {
                until_end = 1U;
            }
            if (todo > until_end)
            {
                todo = until_end;
            }
        }

        if (voice->start_fade_remaining == 0U)
        {
            if (safe_segment != 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
        }
        else
        {
            if (safe_segment != 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
        const uint32_t src = frame_index * frame_stride;
        last_l = data[src] * gain;
        last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
        }

        produced += todo;
        if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (position_q16 >= end_q16))
        {
            terminal = 1U;
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    *io_position_q16 = position_q16;
    return (terminal != 0U) ? 2U : 1U;
}

static uint8_t brick6_sampler_runtime_render_ram_reverse_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U) || (voice->reverse == 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE))
    {
        return 0U;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if (step_q16 == 0U)
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;

    while (produced < frames)
    {
        if (position_q16 < begin_q16)
        {
            terminal = 1U;
            break;
        }

        uint32_t todo = frames - produced;
        uint8_t edge_segment = 0U;
        if (position_q16 >= last_q16)
        {
            todo = 1U;
            edge_segment = 1U;
        }
        else
        {
            const uint64_t distance_from_begin = position_q16 - begin_q16;
            const uint32_t until_begin = brick6_sampler_runtime_bounded_steps(
                distance_from_begin, step_q16, todo, 1U);
            if (todo > until_begin)
            {
                todo = until_begin;
            }
        }

        const uint64_t segment_start_q16 = position_q16;
        if (voice->start_fade_remaining == 0U)
        {
            if (edge_segment != 0U)
            {
                const uint32_t frame_index = (uint32_t)(segment_start_q16 >> 16U);
                const uint32_t src0 = frame_index * frame_stride;
                const float l0 = data[src0];
                const float r0 = data[src0 + right_offset];
                last_l = l0 * render_gain;
                last_r = r0 * render_gain;
                out_l[produced] += last_l;
                out_r[produced] += last_r;
            }
            else
            {
                uint64_t render_position_q16 = segment_start_q16;
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
        }
        else
        {
            if (edge_segment != 0U)
            {
                const uint32_t frame_index = (uint32_t)(segment_start_q16 >> 16U);
                const uint32_t src0 = frame_index * frame_stride;
                const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                const float l0 = data[src0];
                const float r0 = data[src0 + right_offset];
                last_l = l0 * gain;
                last_r = r0 * gain;
                out_l[produced] += last_l;
                out_r[produced] += last_r;
            }
            else
            {
                uint64_t render_position_q16 = segment_start_q16;
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
        }

        produced += todo;
        const uint64_t reverse_advance_q16 = (uint64_t)step_q16 * (uint64_t)todo;
        const uint64_t distance_from_begin = segment_start_q16 - begin_q16;
        if (reverse_advance_q16 > distance_from_begin)
        {
            position_q16 = begin_q16;
            terminal = 1U;
            break;
        }
        position_q16 = segment_start_q16 - reverse_advance_q16;
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    *io_position_q16 = position_q16;
    return (terminal != 0U) ? 2U : 1U;
}

static uint64_t brick6_sampler_runtime_wrap_q16(uint64_t offset_q16, uint64_t span_q16)
{
    while (offset_q16 >= span_q16)
    {
        offset_q16 -= span_q16;
    }
    return offset_q16;
}

static void brick6_sampler_runtime_reflect_pingpong_q16(uint64_t begin_q16,
                                                        uint64_t last_offset_q16,
                                                        uint64_t offset_q16,
                                                        uint64_t *io_position_q16,
                                                        uint8_t *io_reverse)
{
    const uint64_t period_q16 = last_offset_q16 * 2ULL;
    const uint64_t phase_q16 = brick6_sampler_runtime_wrap_q16(offset_q16, period_q16);
    if (phase_q16 <= last_offset_q16)
    {
        *io_reverse = 0U;
        *io_position_q16 = begin_q16 + phase_q16;
    }
    else
    {
        *io_reverse = 1U;
        *io_position_q16 = begin_q16 + (period_q16 - phase_q16);
    }
}

static inline float brick6_sampler_runtime_ram_fade_gain(brick6_sampler_voice_t *voice)
{
    if (voice->start_fade_remaining == 0U)
    {
        return 1.0f;
    }

    const float fade_scale = (STEAL_DECLICK_SAMPLES > 1U)
                                 ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                                 : 1.0f;
    const uint8_t fade_pos = (uint8_t)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining);
    voice->start_fade_remaining--;
    return (float)fade_pos * fade_scale;
}

static uint8_t brick6_sampler_runtime_render_ram_pingpong_unpitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint32_t position)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (frames == 0U) || (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint32_t begin = voice->region_begin;
    const uint32_t end = voice->region_end;
    if ((end <= begin) || ((end - begin) <= 1U) || (position < begin) || (position >= end))
    {
        return 0U;
    }

    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t reverse = voice->reverse;

    while (produced < frames)
    {
        uint32_t todo = frames - produced;
        if (reverse == 0U)
        {
            const uint32_t available = end - position;
            if (todo > available)
            {
                todo = available;
            }
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position + i) * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position + i) * frame_stride;
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            produced += todo;
            position += todo;
            if (position >= end)
            {
                position = end - 2U;
                reverse = 1U;
            }
        }
        else
        {
            const uint32_t available = position - begin + 1U;
            if (todo > available)
            {
                todo = available;
            }
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position - i) * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position - i) * frame_stride;
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            produced += todo;
            if (todo > (position - begin))
            {
                position = begin + 1U;
                reverse = 0U;
            }
            else
            {
                position -= todo;
            }
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    voice->reverse = reverse;
    voice->ram_position_q16 = (uint64_t)position << 16U;
    voice->position = (float)position;
    return 1U;
}

static uint8_t brick6_sampler_runtime_render_ram_pingpong_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint32_t region_frames = voice->region_end - voice->region_begin;
    if (region_frames <= 1U)
    {
        return 0U;
    }

    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint64_t last_offset_q16 = last_q16 - begin_q16;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((step_q16 == 0U) || (last_offset_q16 == 0ULL))
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    uint8_t reverse = voice->reverse;
    if ((position_q16 < begin_q16) || (position_q16 > last_q16))
    {
        const uint64_t offset = (position_q16 >= begin_q16)
                                    ? (position_q16 - begin_q16)
                                    : (begin_q16 - position_q16);
        brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                    last_offset_q16,
                                                    offset,
                                                    &position_q16,
                                                    &reverse);
    }

    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;

    while (produced < frames)
    {
        uint32_t todo = frames - produced;
        if (reverse == 0U)
        {
            const uint64_t distance_to_last = last_q16 - position_q16;
            const uint32_t until_last = brick6_sampler_runtime_bounded_steps(
                distance_to_last, step_q16, todo, 1U);
            if (todo > until_last)
            {
                todo = until_last;
            }
            uint64_t render_position_q16 = position_q16;
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 += (uint64_t)step_q16;
                }
            }
            position_q16 += (uint64_t)step_q16 * (uint64_t)todo;
            produced += todo;
            if (position_q16 > last_q16)
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            position_q16 - begin_q16,
                                                            &position_q16,
                                                            &reverse);
            }
        }
        else
        {
            const uint64_t segment_distance_from_begin = position_q16 - begin_q16;
            const uint32_t until_begin = brick6_sampler_runtime_bounded_steps(
                segment_distance_from_begin, step_q16, todo, 1U);
            if (todo > until_begin)
            {
                todo = until_begin;
            }
            const uint64_t segment_start_q16 = position_q16;
            uint64_t render_position_q16 = segment_start_q16;
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
            const uint64_t reverse_advance_q16 = (uint64_t)step_q16 * (uint64_t)todo;
            const uint64_t distance_from_begin = segment_start_q16 - begin_q16;
            uint8_t bounced = 0U;
            if (reverse_advance_q16 > distance_from_begin)
            {
                bounced = 1U;
            }
            else
            {
                position_q16 = segment_start_q16 - reverse_advance_q16;
            }
            produced += todo;
            if (bounced != 0U)
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            reverse_advance_q16 - distance_from_begin,
                                                            &position_q16,
                                                            &reverse);
            }
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    voice->reverse = reverse;
    *io_position_q16 = position_q16;
    voice->ram_position_q16 = position_q16;
    voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
    return 1U;
}

static void brick6_sampler_runtime_render_ram(brick6_sampler_voice_t *voice,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t frames)
{
    sampler_ram_audio_view_t ram_snapshot;
    const sampler_ram_audio_view_t *const ram = &ram_snapshot;
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U))
    {
        return;
    }

    if (brick6_sampler_runtime_ram_voice_snapshot(voice, &ram_snapshot) == 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(voice->owner_track_id, voice);
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }

    uint64_t position_q16 = voice->ram_position_q16;
    if ((position_q16 == 0ULL) && (voice->position > 0.0f))
    {
        position_q16 = (uint64_t)(voice->position * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
    }
    if ((voice->region_end <= voice->region_begin)
        || (voice->region_end > ram->frames)
        || ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE)
            && (((voice->reverse == 0U) && (position_q16 >= ((uint64_t)voice->region_end << 16U)))
                || ((voice->reverse != 0U)
                    && (position_q16 < ((uint64_t)voice->region_begin << 16U))))))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        return;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint64_t end_q16 = (uint64_t)voice->region_end << 16U;
    const uint32_t forward_loop_begin =
        ((voice->loop_begin >= voice->region_begin) && (voice->loop_begin < voice->region_end))
            ? voice->loop_begin
            : voice->region_begin;
    const uint64_t forward_loop_begin_q16 = (uint64_t)forward_loop_begin << 16U;
    const uint32_t region_frames = voice->region_end - voice->region_begin;
    const uint64_t forward_loop_span_q16 = end_q16 - forward_loop_begin_q16;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint64_t last_offset_q16 = last_q16 - begin_q16;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG) && (region_frames > 1U))
    {
        if ((step_q16 == BRICK6_SAMPLER_Q16_ONE)
            && ((position_q16 & (uint64_t)(BRICK6_SAMPLER_Q16_ONE - 1U)) == 0ULL))
        {
            if (brick6_sampler_runtime_render_ram_pingpong_unpitched(
                    voice,
                    ram,
                    out_l,
                    out_r,
                    frames,
                    (uint32_t)(position_q16 >> 16U)) != 0U)
            {
                return;
            }
        }
        else if (brick6_sampler_runtime_render_ram_pingpong_pitched(
                     voice,
                     ram,
                     out_l,
                     out_r,
                     frames,
                     &position_q16) != 0U)
        {
            return;
        }
    }

    if ((step_q16 == BRICK6_SAMPLER_Q16_ONE)
        && ((position_q16 & (uint64_t)(BRICK6_SAMPLER_Q16_ONE - 1U)) == 0ULL)
        && (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        brick6_sampler_runtime_render_ram_unpitched(voice,
                                                    ram,
                                                    out_l,
                                                    out_r,
                                                    frames,
                                                    (uint32_t)(position_q16 >> 16U));
        return;
    }

    if ((voice->reverse == 0U)
        && (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        const uint8_t forward_result =
            brick6_sampler_runtime_render_ram_forward_pitched(voice,
                                                              ram,
                                                              out_l,
                                                              out_r,
                                                              frames,
                                                              &position_q16);
        if (forward_result != 0U)
        {
            if (forward_result == 2U)
            {
                voice->active = 0U;
                voice->position = 0.0f;
                voice->ram_position_q16 = 0ULL;
                voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
                voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
                voice->ram_generation = 0U;
                voice->ram_data = NULL;
                voice->ram_channels = 0U;
                voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
                return;
            }
            voice->ram_position_q16 = position_q16;
            voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
            return;
        }
    }
    else if ((voice->reverse != 0U)
             && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE))
    {
        const uint8_t reverse_result =
            brick6_sampler_runtime_render_ram_reverse_pitched(voice,
                                                              ram,
                                                              out_l,
                                                              out_r,
                                                              frames,
                                                              &position_q16);
        if (reverse_result != 0U)
        {
            if (reverse_result == 2U)
            {
                voice->active = 0U;
                voice->position = 0.0f;
                voice->ram_position_q16 = 0ULL;
                voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
                voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
                voice->ram_generation = 0U;
                voice->ram_data = NULL;
                voice->ram_channels = 0U;
                voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
                return;
            }
            voice->ram_position_q16 = position_q16;
            voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
            return;
        }
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;
    while (produced < frames)
    {
        if ((voice->reverse == 0U) && (position_q16 >= end_q16))
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position_q16 = forward_loop_begin_q16
                               + brick6_sampler_runtime_wrap_q16(
                                   position_q16 - forward_loop_begin_q16,
                                   forward_loop_span_q16);
            }
            else if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                     && (region_frames > 1U)
                     && (last_offset_q16 > 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            position_q16 - begin_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((voice->reverse != 0U) && (position_q16 < begin_q16))
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                && (region_frames > 1U)
                && (last_offset_q16 > 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            begin_q16 - position_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }

        if ((position_q16 < begin_q16) || (position_q16 >= end_q16))
        {
            terminal = 1U;
            break;
        }

        const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
        if ((frame_index < voice->region_begin) || (frame_index >= voice->region_end))
        {
            terminal = 1U;
            break;
        }

        float gain = render_gain;
        if (voice->start_fade_remaining != 0U)
        {
            const float fade_scale = (STEAL_DECLICK_SAMPLES > 1U)
                                         ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                                         : 1.0f;
            const uint8_t fade_pos = (uint8_t)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining);
            gain *= (float)fade_pos * fade_scale;
            voice->start_fade_remaining--;
        }
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;

        out_l[produced] += last_l;
        out_r[produced] += last_r;
        produced++;

        if (voice->reverse != 0U)
        {
            const uint64_t distance_from_begin = position_q16 - begin_q16;
            if (distance_from_begin < (uint64_t)step_q16)
            {
                if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                    && (region_frames > 1U)
                    && (last_offset_q16 > 0ULL))
                {
                    brick6_sampler_runtime_reflect_pingpong_q16(
                        begin_q16,
                        last_offset_q16,
                        (uint64_t)step_q16 - distance_from_begin,
                        &position_q16,
                        &voice->reverse);
                }
                else
                {
                    terminal = 1U;
                    position_q16 = begin_q16;
                }
            }
            else
            {
                position_q16 -= (uint64_t)step_q16;
            }
        }
        else
        {
            position_q16 += (uint64_t)step_q16;
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (position_q16 >= end_q16))
            {
                terminal = 1U;
            }
        }
        if (terminal != 0U)
        {
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    if (terminal != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }
    voice->ram_position_q16 = position_q16;
    voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
}

static void brick6_sampler_runtime_render_ram_mono(brick6_sampler_voice_t *voice,
                                                   float *out_mono,
                                                   uint32_t frames)
{
    sampler_ram_audio_view_t ram_snapshot;
    const sampler_ram_audio_view_t *const ram = &ram_snapshot;
    if ((voice == NULL) || (out_mono == NULL) || (frames == 0U) || (voice->active == 0U))
    {
        return;
    }

    if ((brick6_sampler_runtime_ram_voice_snapshot(voice, &ram_snapshot) == 0U)
        || (ram->format != SAMPLER_RAM_FORMAT_FLOAT32_MONO))
    {
        brick6_sampler_runtime_begin_declick_tail(voice->owner_track_id, voice);
        brick6_sampler_runtime_clear_ram_voice(voice);
        return;
    }

    uint64_t position_q16 = voice->ram_position_q16;
    if ((position_q16 == 0ULL) && (voice->position > 0.0f))
    {
        position_q16 = (uint64_t)(voice->position * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
    }
    if ((voice->region_end <= voice->region_begin) || (voice->region_end > ram->frames))
    {
        brick6_sampler_runtime_clear_ram_voice(voice);
        return;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint64_t end_q16 = (uint64_t)voice->region_end << 16U;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint64_t last_offset_q16 = last_q16 - begin_q16;
    const uint32_t region_frames = voice->region_end - voice->region_begin;
    const uint32_t loop_begin =
        ((voice->loop_begin >= voice->region_begin) && (voice->loop_begin < voice->region_end))
            ? voice->loop_begin
            : voice->region_begin;
    const uint64_t loop_begin_q16 = (uint64_t)loop_begin << 16U;
    const uint64_t loop_span_q16 = end_q16 - loop_begin_q16;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    const float *const data = ram->data;
    float last = 0.0f;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;

    while (produced < frames)
    {
        if ((voice->reverse == 0U) && (position_q16 >= end_q16))
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (loop_span_q16 != 0ULL))
            {
                position_q16 = loop_begin_q16
                               + brick6_sampler_runtime_wrap_q16(position_q16 - loop_begin_q16,
                                                                 loop_span_q16);
            }
            else if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                     && (region_frames > 1U) && (last_offset_q16 != 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            position_q16 - begin_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((voice->reverse != 0U) && (position_q16 < begin_q16))
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                && (region_frames > 1U) && (last_offset_q16 != 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            begin_q16 - position_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((position_q16 < begin_q16) || (position_q16 >= end_q16))
        {
            terminal = 1U;
            break;
        }

        const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
        float gain = render_gain;
        if (voice->start_fade_remaining != 0U)
        {
            gain *= brick6_sampler_runtime_ram_fade_gain(voice);
        }
        last = data[frame_index] * gain;
        out_mono[produced++] += last;

        if (voice->reverse != 0U)
        {
            const uint64_t distance_from_begin = position_q16 - begin_q16;
            if (distance_from_begin < (uint64_t)step_q16)
            {
                if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                    && (region_frames > 1U) && (last_offset_q16 != 0ULL))
                {
                    brick6_sampler_runtime_reflect_pingpong_q16(
                        begin_q16,
                        last_offset_q16,
                        (uint64_t)step_q16 - distance_from_begin,
                        &position_q16,
                        &voice->reverse);
                }
                else
                {
                    position_q16 = begin_q16;
                    terminal = 1U;
                }
            }
            else
            {
                position_q16 -= (uint64_t)step_q16;
            }
        }
        else
        {
            position_q16 += (uint64_t)step_q16;
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (position_q16 >= end_q16))
            {
                terminal = 1U;
            }
        }
        if (terminal != 0U)
        {
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last, last);
    }
    if (terminal != 0U)
    {
        brick6_sampler_runtime_clear_ram_voice(voice);
        return;
    }
    voice->ram_position_q16 = position_q16;
    voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
}

static void brick6_sampler_runtime_clear_ram_voice(brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    voice->ram_frames = 0U;
    voice->ram_sample_rate = 0U;
}

uint8_t brick6_sampler_runtime_track_has_active_ram_voice(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    return ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)) ? 1U : 0U;
}

uint32_t brick6_sampler_runtime_render_track_mask(void)
{
    return g_sampler_render_track_mask;
}

uint8_t brick6_sampler_runtime_track_ram_is_mono(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    return ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
            && (voice->ram_data != NULL)
            && (voice->ram_format == SAMPLER_RAM_FORMAT_FLOAT32_MONO)) ? 1U : 0U;
}

uint8_t brick6_sampler_runtime_track_is_mono_native_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if ((ctx == NULL)
        || (ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
        || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return 0U;
    }

    const uint8_t track_id = ctx->program_route.entity_id;
    if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
        const brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        const uint8_t shifter_slot_ready =
            ((clip->use_shifter_engine == 0U)
             || (brick6_sampler_runtime_clip_get_slot(track_id) != NULL)) ? 1U : 0U;
        return ((clip->state == (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
                && (shifter_slot_ready != 0U)
                && (voice->active != 0U)
                && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
                && (sample_audio_format_or_stereo(voice->format)
                    == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)) ? 1U : 0U;
    }

    if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI)
    {
        const uint8_t voice_limit = brick6_sampler_runtime_multi_spread_voice_count(track_id);
        if ((voice_limit > 1U)
            && (g_sampler_multi_track_state[track_id].spread > 0.0f))
        {
            return 0U;
        }
        uint8_t active_voice = 0U;
        for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
        {
            const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
            if ((brick6_sampler_runtime_multi_voice_occupied(voice) == 0U)
                || (voice->owner_track_id != track_id))
            {
                continue;
            }

            active_voice = 1U;
            if (sample_audio_format_or_stereo(voice->play_plan.format)
                != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
            {
                return 0U;
            }
        }
        return active_voice;
    }

    return 0U;
}

uint8_t brick6_sampler_runtime_track_is_mono_native(uint8_t track_id)
{
    track_audio_runtime_ctx_t ctx;
    if (audio_note_engine_adapter_current_ctx(track_id, &ctx) == 0U)
        return 0U;
    return brick6_sampler_runtime_track_is_mono_native_ctx(&ctx);
}

static uint8_t brick6_sampler_runtime_multi_voice_format_compatible(
    const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return ((sample_audio_format_is_valid(voice->play_plan.format) != 0U)
            && (voice->play_plan.stride_floats
                == sample_audio_format_stride_floats(voice->play_plan.format))
            && (voice->play_plan.frames_per_page
                == sample_audio_format_frames_per_page(voice->play_plan.format))) ? 1U : 0U;
}

ITCM_TEXT void brick6_sampler_runtime_render_ram_track(const track_audio_runtime_ctx_t *ctx,
                                             float *out_l,
                                             float *out_r,
                                             uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
            || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI))
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->program_route.entity_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS)
        && (mixer_track_vca_is_running(ctx->program_route.mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clear_ram_voice(voice);
    }

    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
    {
        brick6_sampler_runtime_render_ram(voice, out_l, out_r, frames);
    }

    brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
}

void brick6_sampler_runtime_render_ram_track_mono(const track_audio_runtime_ctx_t *ctx,
                                                  float *out_mono,
                                                  uint32_t frames)
{
    if ((ctx == NULL) || (out_mono == NULL) || (frames == 0U)
        || (frames > AUDIO_BLOCK_SIZE)
        || (ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
        || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
        || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
        || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI)
        || (brick6_sampler_runtime_track_ram_is_mono(ctx->program_route.entity_id) == 0U))
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->program_route.entity_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS)
        && (mixer_track_vca_is_running(ctx->program_route.mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clear_ram_voice(voice);
    }
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
    {
        brick6_sampler_runtime_render_ram_mono(voice, out_mono, frames);
    }
    brick6_sampler_runtime_mix_declick_tails_mono(ctx->program_route.entity_id, out_mono, frames);
}
