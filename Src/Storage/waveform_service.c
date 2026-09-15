#include "Storage/waveform_service.h"

#include "Storage/rec_source.h"

static const rec_source_waveform_summary_t *waveform_rec_summary(
    const waveform_source_t *source)
{
    rec_source_snapshot_t current;
    if((source == 0) || (rec_source_current_snapshot(&current) == 0U)
            || (sample_audio_key_equal(&source->key, &current.key) == 0U)
            || (source->registration_epoch != current.registration_epoch)
            || (source->frame_count != current.frame_count))
    {
        return 0;
    }
    const rec_source_waveform_summary_t *const summary =
        rec_source_current_waveform();
    if((summary == 0) || (summary->ready == 0U)
            || (summary->generation != source->key.generation)
            || (summary->frame_count != source->frame_count)
            || (summary->bin_count == 0U))
    {
        return 0;
    }
    return summary;
}

uint8_t waveform_rec_current_source(waveform_source_t *out_source)
{
    rec_source_snapshot_t current;
    if((out_source == 0) || (rec_source_current_snapshot(&current) == 0U)
            || (current.key.domain != SAMPLE_AUDIO_DOMAIN_REC)
            || (current.key.generation == 0U)
            || (current.registration_epoch == 0U)
            || (current.frame_count == 0U))
    {
        return 0U;
    }
    out_source->key = current.key;
    out_source->registration_epoch = current.registration_epoch;
    out_source->frame_count = current.frame_count;
    return (waveform_rec_summary(out_source) != 0) ? 1U : 0U;
}

waveform_result_t waveform_request(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns)
{
    if((source == 0) || (columns == 0) || (pixel_width == 0U)
            || (frame_count == 0U) || (start_frame >= source->frame_count)
            || (frame_count > (source->frame_count - start_frame)))
    {
        return WAVEFORM_RESULT_INVALID;
    }
    const rec_source_waveform_summary_t *const summary =
        waveform_rec_summary(source);
    if(summary == 0)
    {
        return WAVEFORM_RESULT_PENDING;
    }

    const uint32_t frame_step = frame_count / pixel_width;
    const uint32_t frame_remainder = frame_count % pixel_width;
    uint64_t cursor = start_frame;
    uint32_t remainder = 0U;
    for(uint8_t col = 0U; col < pixel_width; ++col)
    {
        const uint32_t frame0 = (uint32_t)cursor;
        cursor += frame_step;
        remainder += frame_remainder;
        if(remainder >= pixel_width)
        {
            remainder -= pixel_width;
            cursor++;
        }
        uint32_t frame1 = (uint32_t)cursor;
        if(frame1 <= frame0) { frame1 = frame0 + 1U; }
        if(frame1 > source->frame_count) { frame1 = source->frame_count; }

        uint32_t bin0;
        uint32_t bin1;
        if(summary->frames_per_bin != 0U)
        {
            bin0 = frame0 / summary->frames_per_bin;
            bin1 = (uint32_t)(((uint64_t)frame1
                + summary->frames_per_bin - 1ULL) / summary->frames_per_bin);
        }
        else
        {
            const uint32_t domain = (summary->bin_domain_frames != 0U)
                ? summary->bin_domain_frames : summary->frame_count;
            bin0 = (uint32_t)(((uint64_t)frame0 * summary->bin_count) / domain);
            bin1 = (uint32_t)((((uint64_t)frame1 * summary->bin_count)
                + domain - 1ULL) / domain);
        }
        if(bin0 >= summary->bin_count) { bin0 = summary->bin_count - 1U; }
        if(bin1 <= bin0) { bin1 = bin0 + 1U; }
        if(bin1 > summary->bin_count) { bin1 = summary->bin_count; }

        int16_t min = summary->min[bin0];
        int16_t max = summary->max[bin0];
        for(uint32_t bin = bin0 + 1U; bin < bin1; ++bin)
        {
            if(summary->min[bin] < min) { min = summary->min[bin]; }
            if(summary->max[bin] > max) { max = summary->max[bin]; }
        }
        columns[col].min = min;
        columns[col].max = max;
    }
    return WAVEFORM_RESULT_READY;
}
