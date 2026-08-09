#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Storage/wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_STREAM_BACKEND_FATFS = 0,
    SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS = 1,
    SAMPLE_STREAM_BACKEND_RUNS_RESERVED = 2
} sample_stream_backend_kind_t;

typedef enum
{
    SAMPLE_STREAM_SAFE_INVALID = 0,
    SAMPLE_STREAM_SAFE_CONTIGUOUS = 1,
    SAMPLE_STREAM_SAFE_REJECTED = 2
} sample_stream_safe_state_t;

#define SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS      (128U)
#define SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS      (512U)
#define SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK (8U)
#define SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK    (UINT16_MAX)

typedef struct
{
    uint32_t file_sector_start;
    uint32_t lba_start;
    uint32_t sector_count;
} sample_stream_physical_extent_t;

typedef struct
{
    sample_stream_physical_extent_t inline_extent;
    uint32_t generation;
    uint32_t media_epoch;
    uint16_t extent_count;
    uint16_t first_pool_block;
    uint8_t valid;
    uint8_t reserved[3];
} sample_stream_physical_map_t;

typedef struct
{
    sample_audio_key_t key;
    uint8_t valid;
    uint8_t backend_kind;
    uint8_t safe_state;
    uint8_t contiguous;
    uint32_t first_file_lba;
    uint32_t data_offset_bytes;
    uint32_t total_frames;
    uint32_t block_align;
    uint32_t bytes_per_frame;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    uint32_t file_size;
    uint32_t data_size;
    uint16_t sector_size;
    uint16_t data_sector_offset;
    sample_stream_physical_map_t physical_map;
} sample_stream_safe_metadata_t;

void sample_stream_physical_map_pool_reset(void);
void sample_stream_physical_map_release(sample_stream_physical_map_t *map);
uint8_t sample_stream_physical_map_is_current(const sample_stream_physical_map_t *map);
uint8_t sample_stream_physical_map_get_extent(const sample_stream_physical_map_t *map,
                                              uint16_t extent_index,
                                              sample_stream_physical_extent_t *out_extent);

uint8_t sample_stream_fatfs_map_build_from_file(FIL *fp,
                                                sample_stream_safe_metadata_t *out_meta);

uint8_t sample_stream_fatfs_map_build_from_path(const char *path,
                                                sample_stream_safe_metadata_t *out_meta);

void sample_stream_safe_metadata_init_fatfs(sample_audio_key_t key,
                                            const wav_info_t *info,
                                            uint32_t total_frames,
                                            uint32_t data_offset,
                                            sample_stream_safe_metadata_t *out_meta);

uint8_t sample_stream_fatfs_map_certify_contiguous(sample_audio_key_t key,
                                                   const char *path,
                                                   const wav_info_t *info,
                                                   uint32_t total_frames,
                                                   uint32_t data_offset,
                                                   sample_stream_safe_metadata_t *out_meta);

#ifdef __cplusplus
}
#endif
