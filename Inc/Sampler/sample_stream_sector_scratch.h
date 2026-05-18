#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_SECTOR_SCRATCH_BYTES (16384U)

uint8_t *sample_stream_sector_scratch_buffer(void);
uint32_t sample_stream_sector_scratch_size_bytes(void);

#ifdef __cplusplus
}
#endif
