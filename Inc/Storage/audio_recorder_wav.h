#pragma once

#include <stdint.h>

#include "Storage/audio_recorder_format.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t audio_recorder_wav_build_header(
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES],
    uint32_t data_bytes,
    uint32_t sample_rate_hz,
    uint16_t channels);

#ifdef __cplusplus
}
#endif
