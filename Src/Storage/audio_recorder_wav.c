#include "Storage/audio_recorder_wav.h"

#include <string.h>

static void audio_recorder_wav_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void audio_recorder_wav_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

uint8_t audio_recorder_wav_build_header(
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES],
    uint32_t data_bytes,
    uint32_t sample_rate_hz,
    uint16_t channels)
{
    if ((header == 0) || (sample_rate_hz == 0U) || (channels == 0U)
            || (channels > (UINT16_MAX / 3U)))
    {
        return 0U;
    }
    const uint16_t block_align = (uint16_t)(channels * 3U);
    if ((data_bytes > (UINT32_MAX - (AUDIO_RECORDER_WAV_HEADER_BYTES - 8U)))
            || ((data_bytes % block_align) != 0U))
    {
        return 0U;
    }
    const uint64_t byte_rate_64 = (uint64_t)sample_rate_hz * block_align;
    if (byte_rate_64 > UINT32_MAX)
    {
        return 0U;
    }
    memset(header, 0, AUDIO_RECORDER_WAV_HEADER_BYTES);
    memcpy(&header[0], "RIFF", 4U);
    audio_recorder_wav_le32(&header[4],
        (AUDIO_RECORDER_WAV_HEADER_BYTES - 8U) + data_bytes);
    memcpy(&header[8], "WAVE", 4U);
    memcpy(&header[12], "fmt ", 4U);
    audio_recorder_wav_le32(&header[16], 16U);
    audio_recorder_wav_le16(&header[20], 1U);
    audio_recorder_wav_le16(&header[22], channels);
    audio_recorder_wav_le32(&header[24], sample_rate_hz);
    audio_recorder_wav_le32(&header[28], (uint32_t)byte_rate_64);
    audio_recorder_wav_le16(&header[32], block_align);
    audio_recorder_wav_le16(&header[34], 24U);
    memcpy(&header[36], "JUNK", 4U);
    audio_recorder_wav_le32(&header[40],
        AUDIO_RECORDER_WAV_HEADER_BYTES - 52U);
    memcpy(&header[AUDIO_RECORDER_WAV_HEADER_BYTES - 8U], "data", 4U);
    audio_recorder_wav_le32(
        &header[AUDIO_RECORDER_WAV_HEADER_BYTES - 4U], data_bytes);
    return 1U;
}
