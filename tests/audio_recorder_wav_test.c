#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Storage/audio_recorder_wav.h"

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void check_header(uint32_t frames)
{
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES];
    const uint32_t data_bytes = frames * 6U;
    assert(audio_recorder_wav_build_header(
        header, data_bytes, 48000U, 2U) != 0U);
    assert(memcmp(&header[0], "RIFF", 4U) == 0);
    assert(read_le32(&header[4]) == 504U + data_bytes);
    assert(memcmp(&header[8], "WAVEfmt ", 8U) == 0);
    assert(read_le32(&header[16]) == 16U);
    assert(read_le16(&header[20]) == 1U);
    assert(read_le16(&header[22]) == 2U);
    assert(read_le32(&header[24]) == 48000U);
    assert(read_le32(&header[28]) == 288000U);
    assert(read_le16(&header[32]) == 6U);
    assert(read_le16(&header[34]) == 24U);
    assert(memcmp(&header[36], "JUNK", 4U) == 0);
    assert(read_le32(&header[40]) == 460U);
    assert(memcmp(&header[504], "data", 4U) == 0);
    assert(read_le32(&header[508]) == data_bytes);
}

int main(void)
{
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES];
    assert(audio_recorder_wav_build_header(header, 1U, 48000U, 2U) == 0U);
    assert(audio_recorder_wav_build_header(
        header, UINT32_MAX, 48000U, 2U) == 0U);
    check_header(0U);
    check_header(1U);
    check_header(85U);
    check_header(86U);
    check_header(48000U * 60U);
    puts("audio_recorder_wav_test: PASS (PCM24 stereo 48 kHz, exact sizes)");
    return 0;
}
