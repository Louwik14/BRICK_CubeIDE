void sample_voice_reader_mix_fwd_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset,
                                    float *out_last_l,
                                    float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = (segment->is_mono != 0U) ? src_l : segment->r;
    float last_l = 0.0f;
    float last_r = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        last_l = src_l[i * segment->frame_stride] * sample_gain;
        last_r = src_r[i * segment->frame_stride] * sample_gain;
        out_l[out_offset + i] += last_l;
        out_r[out_offset + i] += last_r;
    }
    if ((segment->frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_rev_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset,
                                    float *out_last_l,
                                    float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = (segment->is_mono != 0U) ? src_l : segment->r;
    float last_l = 0.0f;
    float last_r = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        last_l = src_l[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
        last_r = src_r[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
        out_l[out_offset + i] += last_l;
        out_r[out_offset + i] += last_r;
    }
    if ((segment->frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_fwd_1x_mono(const sample_audio_segment_t *segment,
                                         float gain,
                                         const float *fade_gain,
                                         uint32_t fade_count,
                                         float *out_mono,
                                         uint32_t out_offset,
                                         float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = segment->l[i * segment->frame_stride] * gain * fade;
        out_mono[out_offset + i] += last;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_rev_1x_mono(const sample_audio_segment_t *segment,
                                         float gain,
                                         const float *fade_gain,
                                         uint32_t fade_count,
                                         float *out_mono,
                                         uint32_t out_offset,
                                         float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = segment->l[-((int32_t)i * (int32_t)segment->frame_stride)] * gain * fade;
        out_mono[out_offset + i] += last;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_pitch_fwd_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset,
                                              float *out_last_l,
                                              float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frames = segment->frames;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_limit_frame = segment->source_limit_frame;
    const uint32_t source_end_frame = source_start_frame + segment->source_frame_count;
    const uint32_t frame_stride = segment->frame_stride;
    const uint32_t right_offset = (segment->is_mono != 0U) ? 0U : 1U;
    const float gain_base = gain;
    uint32_t pos_q16 =
        (uint32_t)(((segment->source_position - (float)source_start_frame)
                    * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }
    float *dst_l = &out_l[out_offset];
    float *dst_r = &out_r[out_offset];
    const uint8_t has_fade = ((fade_gain != 0) && (fade_count != 0U)) ? 1U : 0U;
    float last_l = 0.0f;
    float last_r = 0.0f;

    if (frames == 0U)
    {
        return;
    }

    uint32_t same_page_frames = frames;
    const uint32_t same_page_limit_frame = (source_end_frame < source_limit_frame)
                                               ? source_end_frame
                                               : source_limit_frame;
    if (same_page_limit_frame <= (source_start_frame + 1U))
    {
        same_page_frames = 0U;
    }
    else
    {
        const uint32_t first_boundary_frame = same_page_limit_frame - 1U;
        const uint64_t first_boundary_q16 =
            ((uint64_t)(first_boundary_frame - source_start_frame)) << 16U;
        if ((uint64_t)pos_q16 >= first_boundary_q16)
        {
            same_page_frames = 0U;
        }
        else
        {
            const uint64_t distance_q16 = first_boundary_q16 - (uint64_t)pos_q16;
            const uint64_t frames_before_boundary = ((distance_q16 - 1ULL) / step_q16) + 1ULL;
            if (frames_before_boundary < (uint64_t)same_page_frames)
            {
                same_page_frames = (uint32_t)frames_before_boundary;
            }
        }
    }

    for (uint32_t i = 0U; i < same_page_frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t src0 = base_offset * frame_stride;
        const float curr_l = src[src0];
        const float curr_r = src[src0 + right_offset];
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        float sample_l = curr_l;
        float sample_r = curr_r;
        if (frac_q16 != 0U)
        {
            const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
            const float next_l = src[src0 + frame_stride];
            sample_l = curr_l + ((next_l - curr_l) * frac);
            if (right_offset != 0U)
            {
                const float next_r = src[src0 + frame_stride + right_offset];
                sample_r = curr_r + ((next_r - curr_r) * frac);
            }
            else
            {
                sample_r = sample_l;
            }
        }

        const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
        last_l = sample_l * sample_gain;
        last_r = sample_r * sample_gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
        pos_q16 += step_q16;
    }

    for (uint32_t i = same_page_frames; i < frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t base_frame = source_start_frame + base_offset;
        const uint32_t src0 = base_offset * frame_stride;
        const float curr_l = src[src0];
        const float curr_r = src[src0 + right_offset];
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        float sample_l = curr_l;
        float sample_r = curr_r;

        if (frac_q16 != 0U)
        {
            uint32_t next_frame = base_frame + 1U;
            if (next_frame >= source_limit_frame)
            {
                next_frame = (segment->neighbor_l != 0) ? segment->source_region_begin : UINT32_MAX;
            }

            const float *next_l_ptr = 0;
            const float *next_r_ptr = 0;
            if ((next_frame >= source_start_frame) && (next_frame < source_end_frame))
            {
                next_l_ptr = &src[(next_frame - source_start_frame) * frame_stride];
                next_r_ptr = &next_l_ptr[right_offset];
            }
            else if ((segment->neighbor_l != 0) && (next_frame >= segment->neighbor_start_frame)
                     && (next_frame < (segment->neighbor_start_frame + segment->neighbor_frame_count)))
            {
                next_l_ptr = &segment->neighbor_l[(next_frame - segment->neighbor_start_frame) * frame_stride];
                next_r_ptr = &segment->neighbor_r[(next_frame - segment->neighbor_start_frame) * frame_stride];
            }

            if ((next_l_ptr != 0) && (next_r_ptr != 0))
            {
                const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
                sample_l = curr_l + ((*next_l_ptr - curr_l) * frac);
                sample_r = curr_r + ((*next_r_ptr - curr_r) * frac);
            }
        }

        const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
        last_l = sample_l * sample_gain;
        last_r = sample_r * sample_gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
        pos_q16 += step_q16;
    }
    if ((frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_pitch_rev_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset,
                                              float *out_last_l,
                                              float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frames = segment->frames;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_region_begin = segment->source_region_begin;
    const uint32_t frame_stride = segment->frame_stride;
    const uint32_t right_offset = (segment->is_mono != 0U) ? 0U : 1U;
    const float gain_base = gain;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)source_start_frame) * 65536.0f) + 0.5f);
    const uint32_t step_q16 = (uint32_t)(segment->source_step * 65536.0f + 0.5f);
    float *dst_l = &out_l[out_offset];
    float *dst_r = &out_r[out_offset];
    const uint8_t has_fade = ((fade_gain != 0) && (fade_count != 0U)) ? 1U : 0U;
    float last_l = 0.0f;
    float last_r = 0.0f;

    if (frames == 0U)
    {
        return;
    }

    const uint64_t reverse_span_q16 = (uint64_t)step_q16 * (uint64_t)(frames - 1U);
    const uint32_t end_pos_q16 = (reverse_span_q16 < (uint64_t)pos_q16)
                                     ? (pos_q16 - (uint32_t)reverse_span_q16)
                                     : 0U;
    const uint32_t min_base_offset = end_pos_q16 >> 16U;
    const uint8_t same_span =
        ((min_base_offset > 0U) && ((source_start_frame + min_base_offset) > source_region_begin)) ? 1U : 0U;

    if (same_span != 0U)
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t base_offset = pos_q16 >> 16U;
            const uint32_t src0 = base_offset * frame_stride;
            const float sample_l = src[src0];
            const float sample_r = src[src0 + right_offset];
            const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
            last_l = sample_l * sample_gain;
            last_r = sample_r * sample_gain;
            dst_l[i] += last_l;
            dst_r[i] += last_r;
            pos_q16 -= step_q16;
        }
    }
    else
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t base_offset = pos_q16 >> 16U;
            const float *read_ptr = &src[base_offset * frame_stride];

            const float sample_l = read_ptr[0];
            const float sample_r = read_ptr[right_offset];
            const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
            last_l = sample_l * sample_gain;
            last_r = sample_r * sample_gain;
            dst_l[i] += last_l;
            dst_r[i] += last_r;
            pos_q16 -= step_q16;
        }
    }
    if ((frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_pitch_fwd_linear_mono(const sample_audio_segment_t *segment,
                                                   float gain,
                                                   const float *fade_gain,
                                                   uint32_t fade_count,
                                                   float *out_mono,
                                                   uint32_t out_offset,
                                                   float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_end_frame = source_start_frame + segment->source_frame_count;
    const uint32_t frame_stride = segment->frame_stride;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)source_start_frame)
                                  * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t base_frame = source_start_frame + base_offset;
        const uint32_t src0 = base_offset * frame_stride;
        const float current = src[src0];
        float sample = current;
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        if (frac_q16 != 0U)
        {
            uint32_t next_frame = base_frame + 1U;
            if (next_frame >= segment->source_limit_frame)
            {
                next_frame = (segment->neighbor_l != 0) ? segment->source_region_begin : UINT32_MAX;
            }

            const float *next = 0;
            if ((next_frame >= source_start_frame) && (next_frame < source_end_frame))
            {
                next = &src[(next_frame - source_start_frame) * frame_stride];
            }
            else if ((segment->neighbor_l != 0) && (next_frame >= segment->neighbor_start_frame)
                     && (next_frame < (segment->neighbor_start_frame + segment->neighbor_frame_count)))
            {
                next = &segment->neighbor_l[(next_frame - segment->neighbor_start_frame) * frame_stride];
            }
            if (next != 0)
            {
                const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
                sample = current + ((*next - current) * frac);
            }
        }

        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = sample * gain * fade;
        out_mono[out_offset + i] += last;
        pos_q16 += step_q16;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_pitch_rev_linear_mono(const sample_audio_segment_t *segment,
                                                   float gain,
                                                   const float *fade_gain,
                                                   uint32_t fade_count,
                                                   float *out_mono,
                                                   uint32_t out_offset,
                                                   float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frame_stride = segment->frame_stride;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)segment->source_start_frame)
                                  * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = src[base_offset * frame_stride] * gain * fade;
        out_mono[out_offset + i] += last;
        pos_q16 -= step_q16;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}
