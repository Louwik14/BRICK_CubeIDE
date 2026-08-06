#include "Sampler/sample_stream_decoder.h"

#include "Storage/wav_audio_codec.h"

sample_page_load_result_t sample_stream_decoder_decode_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    const uint8_t *source,
    uint32_t source_bytes)
{
    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (source == 0) || (info->info.block_align == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    const uint32_t expected_bytes = target->frame_count * info->info.block_align;
    const uint32_t expected_block_align =
        (uint32_t)info->info.channels * ((uint32_t)info->info.bits_per_sample / 8U);
    const sample_audio_format_t expected_format =
        sample_audio_format_from_channels(info->info.channels);
    const wav_audio_codec_decode_block_fn decode_block =
        (info->info.channels == 1U)
            ? wav_audio_codec_select_pcm_decode_mono_block(info->info.bits_per_sample)
            : wav_audio_codec_select_pcm_decode_block(info->info.channels,
                                                      info->info.bits_per_sample);
    if ((expected_bytes == 0U) || (source_bytes != expected_bytes)
        || (decode_block == 0) || (info->info.block_align != expected_block_align)
        || (target->format != expected_format)
        || (target->stride_floats != sample_audio_format_stride_floats(expected_format)))
    {
        return SAMPLE_PAGE_LOAD_DECODE_FAILED;
    }

    decode_block(source, target->frames_interleaved, target->frame_count);
    return SAMPLE_PAGE_LOAD_OK;
}
