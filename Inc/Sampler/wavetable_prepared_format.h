#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAVETABLE_PREPARED_FILE_MAGIC       (0x54573642UL) /* B6WT */
#define WAVETABLE_PREPARED_FILE_VERSION     (5U)
#define WAVETABLE_PREPARED_HEADER_SIZE      (128U)
#define WAVETABLE_PREPARED_BAND_ENTRY_SIZE  (32U)
#define WAVETABLE_MIPMAP_MAX_BANDS          (8U)
#define WAVETABLE_MIPMAP_PREP_REVISION      (9U)
#define WAVETABLE_MIPMAP_FLAG_MULTIBAND     (1UL << 0)
#define WAVETABLE_PREPARED_SOURCE_CRC_OFFSET (84U)
#define WAVETABLE_PREPARED_BASE_CRC_OFFSET   (88U)
#define WAVETABLE_PREPARED_PAYLOAD_CRC_OFFSET (92U)
#define WAVETABLE_PREPARED_TOTAL_SIZE_OFFSET (96U)

typedef struct
{
    uint32_t max_phase_increment;
    uint32_t from_cycle;
    uint32_t to_cycle;
    uint32_t cycle_sample_count;
    uint16_t cycle_magnitude;
    uint16_t flags;
    float *data;
    uint32_t sample_count;
} wavetable_mipmap_band_t;

typedef struct
{
    uint16_t band_count;
    uint32_t cycle_count;
    uint8_t cycle_transition_magnitude;
    uint8_t reserved[3];
    int32_t wave_index_multiplier;
    wavetable_mipmap_band_t bands[WAVETABLE_MIPMAP_MAX_BANDS];
    float *data;
    uint32_t data_bytes;
    uint16_t first_page_slot;
    uint16_t page_count;
    uint32_t cost_bytes_aligned;
} wavetable_mipmap_view_t;

#ifdef __cplusplus
}
#endif
