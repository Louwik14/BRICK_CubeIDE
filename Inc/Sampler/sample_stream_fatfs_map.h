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
    SAMPLE_STREAM_BACKEND_PHYSICAL = 1
} sample_stream_backend_kind_t;

#define SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS      (128U)
#define SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS      (512U)
#define SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK (8U)
#define SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS       \
    ((SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS - 1U   \
      + SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK - 1U) \
     / SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK)
#define SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK    (UINT16_MAX)
#define SAMPLE_STREAM_COOP_CLMT_ITEMS                (2U + (2U * SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS))

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
    uint16_t pool_block[SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS];
    uint8_t valid;
    uint8_t reserved[3];
} sample_stream_physical_map_t;

typedef struct
{
    uint32_t map_generation;
    uint16_t extent_index;
    uint16_t reserved;
} sample_stream_physical_cursor_t;

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    uint32_t logical_bytes;
    uint16_t first_sector_skip;
    uint16_t extent_index;
} sample_stream_physical_span_t;

typedef struct
{
    sample_audio_key_t key;
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

typedef enum
{
    SAMPLE_STREAM_COOP_IDLE = 0,
    SAMPLE_STREAM_COOP_OPENING,
    SAMPLE_STREAM_COOP_MAPPING,
    SAMPLE_STREAM_COOP_READY,
    SAMPLE_STREAM_COOP_READING,
    SAMPLE_STREAM_COOP_ERROR
} sample_stream_coop_state_t;

typedef struct
{
    uint8_t sector[512U];
    DWORD clmt[SAMPLE_STREAM_COOP_CLMT_ITEMS];
    FIL file;
    FF_BRICK_REC_PREPARE_CONT open;
    FF_BRICK_READ_MAP_CONT map;
    sample_stream_physical_map_t physical_map;
    char path[160U];
    void *read_dst;
    uint32_t file_size;
    uint32_t media_epoch;
    uint32_t io_identity;
    uint32_t io_lba;
    uint32_t io_sequence;
    uint32_t cached_sector;
    uint32_t read_offset;
    uint32_t read_remaining;
    uint16_t read_sector_skip;
    uint16_t read_copy_bytes;
    uint8_t *read_cursor;
    uint8_t io_active;
    uint8_t io_kind;
    uint8_t file_open;
    uint8_t cached_valid;
    sample_stream_coop_state_t state;
    FRESULT result;
} sample_stream_coop_file_t;

void sample_stream_physical_map_pool_reset(void);
void sample_stream_physical_map_release(sample_stream_physical_map_t *map);
uint8_t sample_stream_physical_map_import(
    sample_stream_physical_map_t *map,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch);
uint8_t sample_stream_physical_map_is_current(const sample_stream_physical_map_t *map);
sample_stream_backend_kind_t sample_stream_safe_metadata_backend(
    const sample_stream_safe_metadata_t *metadata);
uint8_t sample_stream_physical_map_get_extent(const sample_stream_physical_map_t *map,
                                              uint16_t extent_index,
                                              sample_stream_physical_extent_t *out_extent);
uint8_t sample_stream_physical_map_resolve(const sample_stream_physical_map_t *map,
                                           uint64_t file_byte_offset,
                                           uint32_t requested_bytes,
                                           sample_stream_physical_cursor_t *cursor,
                                           sample_stream_physical_span_t *out_span);

uint8_t sample_stream_fatfs_map_build_from_file(FIL *fp,
                                                sample_stream_safe_metadata_t *out_meta);

uint8_t sample_stream_fatfs_map_build_from_path(const char *path,
                                                sample_stream_safe_metadata_t *out_meta);

void sample_stream_coop_file_init(sample_stream_coop_file_t *file);
uint8_t sample_stream_coop_file_begin(sample_stream_coop_file_t *file,
                                      const char *path);
void sample_stream_coop_file_service(sample_stream_coop_file_t *file,
                                     uint32_t cpu_step_budget);
uint8_t sample_stream_coop_file_read_begin(sample_stream_coop_file_t *file,
                                           uint32_t offset,
                                           void *dst,
                                           uint32_t bytes);
uint8_t sample_stream_coop_file_read_done(const sample_stream_coop_file_t *file);
uint8_t sample_stream_coop_file_take_map(sample_stream_coop_file_t *file,
                                         sample_stream_physical_map_t *out_map,
                                         uint32_t *out_file_size);
void sample_stream_coop_file_close(sample_stream_coop_file_t *file);

void sample_stream_safe_metadata_init_fatfs(sample_audio_key_t key,
                                            const wav_info_t *info,
                                            uint32_t total_frames,
                                            uint32_t data_offset,
                                            sample_stream_safe_metadata_t *out_meta);

#ifdef __cplusplus
}
#endif
