#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_format.h"
#include "Sampler/multi_sample_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MULTI_SAMPLE_INDEX_MAGIC_SIZE       (8U)
#define MULTI_SAMPLE_INDEX_VERSION          (2U)
#define MULTI_SAMPLE_INDEX_HEADER_SIZE      (96U)
#define MULTI_SAMPLE_INDEX_STRING_MAX_BYTES (65536U)

#define MULTI_SAMPLE_INDEX_META_ROOT_SMPL      (0x01U)
#define MULTI_SAMPLE_INDEX_META_ROOT_INST      (0x02U)
#define MULTI_SAMPLE_INDEX_META_ROOT_FILENAME  (0x04U)
#define MULTI_SAMPLE_INDEX_META_ROOT_ALPHA     (0x08U)
#define MULTI_SAMPLE_INDEX_META_VEL_INST       (0x10U)
#define MULTI_SAMPLE_INDEX_META_VEL_FILENAME   (0x20U)
#define MULTI_SAMPLE_INDEX_META_VEL_ALPHA      (0x40U)
#define MULTI_SAMPLE_INDEX_META_LOOP_AUTO      (0x80U)

typedef enum
{
    MULTI_SAMPLE_INDEX_OK = 0,
    MULTI_SAMPLE_INDEX_INVALID_ARG,
    MULTI_SAMPLE_INDEX_SD_BUSY,
    MULTI_SAMPLE_INDEX_SD_MOUNT_FAIL,
    MULTI_SAMPLE_INDEX_OPEN_FAIL,
    MULTI_SAMPLE_INDEX_READ_FAIL,
    MULTI_SAMPLE_INDEX_WRITE_FAIL,
    MULTI_SAMPLE_INDEX_BAD_FORMAT,
    MULTI_SAMPLE_INDEX_LIMIT,
    MULTI_SAMPLE_INDEX_CRC_FAIL,
    MULTI_SAMPLE_INDEX_FORMAT_MISMATCH,
    MULTI_SAMPLE_INDEX_POOL_FAIL
} multi_sample_index_result_t;

typedef struct
{
    const char *relative_path;
    uint32_t total_frames;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint8_t root_note;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t has_loop;
    uint8_t metadata_flags;
    uint32_t wav_size;
    uint32_t wav_mtime;
    uint32_t wav_crc32;
} multi_sample_index_source_sample_t;

typedef struct
{
    uint8_t note_low;
    uint8_t note_high;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t root_note;
    uint16_t multi_sample_id;
} multi_sample_index_zone_t;

typedef struct
{
    const char *instrument_name;
    uint32_t flags;
    uint16_t sample_count;
    uint16_t zone_count;
    const multi_sample_index_source_sample_t *samples;
    const multi_sample_index_zone_t *zones;
} multi_sample_index_source_t;

typedef struct
{
    uint32_t path_offset;
    uint16_t path_len;
    uint16_t reserved;
    uint32_t total_frames;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint8_t root_note;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t has_loop;
    uint8_t metadata_flags;
    uint32_t wav_size;
    uint32_t wav_mtime;
    uint32_t wav_crc32;
    uint8_t wav_identity_valid;
} multi_sample_index_sample_t;

typedef struct
{
    char instrument_name[MULTI_SAMPLE_POOL_NAME_MAX];
    uint32_t flags;
    uint16_t sample_count;
    uint16_t zone_count;
    uint32_t string_bytes;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    const multi_sample_index_sample_t *samples;
    const multi_sample_index_zone_t *zones;
    const char *strings;
    uint32_t file_size;
    uint32_t crc32;
} multi_sample_index_t;

multi_sample_index_result_t multi_sample_index_write(
    const char *path,
    const multi_sample_index_source_t *src);
multi_sample_index_result_t multi_sample_index_load(const char *path,
                                                    multi_sample_index_t *out);
multi_sample_index_result_t multi_sample_index_peek_counts(const char *path,
                                                           uint16_t *out_sample_count,
                                                           uint16_t *out_zone_count);
uint8_t multi_sample_index_validate(const multi_sample_index_t *idx);
multi_sample_index_result_t multi_sample_index_apply_to_pool(
    const multi_sample_index_t *idx,
    uint16_t instrument_id);
void multi_sample_index_reset(multi_sample_index_t *idx);
multi_sample_index_result_t multi_sample_index_debug_make(
    const multi_sample_index_source_t *src,
    multi_sample_index_t *out);

#ifdef __cplusplus
}
#endif
