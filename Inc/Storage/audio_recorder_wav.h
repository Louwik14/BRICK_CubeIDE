#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_WAV_HEADER_BYTES (512U)

uint8_t audio_recorder_wav_build_header(
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES],
    uint32_t data_bytes,
    uint32_t sample_rate_hz,
    uint16_t channels);

#ifdef __cplusplus
}
#endif
