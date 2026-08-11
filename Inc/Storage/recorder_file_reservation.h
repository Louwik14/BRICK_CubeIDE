#ifndef RECORDER_FILE_RESERVATION_H
#define RECORDER_FILE_RESERVATION_H

#include <stdint.h>

#include "Sampler/sample_stream_fatfs_map.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_FILE_RESERVATION_MAX_EXTENTS SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS
#define RECORDER_FILE_RESERVATION_PATH_MAX 96U

typedef enum
{
    RECORDER_FILE_RESERVATION_OK = 0,
    RECORDER_FILE_RESERVATION_PARTIAL,
    RECORDER_FILE_RESERVATION_INVALID_ARG,
    RECORDER_FILE_RESERVATION_INVALID_STATE,
    RECORDER_FILE_RESERVATION_SD_BUSY,
    RECORDER_FILE_RESERVATION_NO_SPACE,
    RECORDER_FILE_RESERVATION_MAP_FULL,
    RECORDER_FILE_RESERVATION_FS_ERROR
} recorder_file_reservation_result_t;

typedef struct
{
    const sample_stream_physical_extent_t *extents;
    uint64_t reserved_file_bytes;
    uint64_t valid_file_bytes;
    uint32_t generation;
    uint32_t media_epoch;
    uint16_t extent_count;
    uint16_t sector_size;
} recorder_file_reservation_map_snapshot_t;

typedef struct
{
    uint32_t last_operation_ms;
    uint32_t max_create_ms;
    uint32_t max_extend_ms;
    uint32_t max_commit_ms;
    uint32_t max_release_ms;
    uint32_t operation_count;
    uint32_t metadata_sectors_read;
    uint32_t metadata_sectors_written;
    uint32_t sync_count;
    uint32_t clusters_allocated;
    uint32_t clusters_released;
    uint32_t extents_added;
    uint32_t last_fresult;
} recorder_file_reservation_metrics_t;

typedef struct
{
    FIL file;
    FF_BRICK_REC_STATE fs_state;
    FF_BRICK_REC_EXTENT fs_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    sample_stream_physical_extent_t physical_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    recorder_file_reservation_metrics_t metrics;
    char path[RECORDER_FILE_RESERVATION_PATH_MAX];
    volatile uint32_t publish_sequence;
    volatile uint32_t map_generation;
    volatile uint64_t published_reserved_file_bytes;
    volatile uint64_t published_valid_file_bytes;
    volatile uint32_t published_media_epoch;
    volatile uint16_t published_extent_count;
    uint16_t extent_count;
    uint32_t header_bytes;
    uint64_t reserved_bytes;
    uint64_t valid_bytes;
    uint8_t open;
    uint8_t finalizing;
    uint8_t failed;
    uint8_t reserved;
} recorder_file_reservation_t;

void recorder_file_reservation_init(recorder_file_reservation_t *session);
recorder_file_reservation_result_t recorder_file_reservation_create(
    recorder_file_reservation_t *session,
    const char *temporary_path,
    uint32_t header_bytes,
    uint64_t initial_reserve_bytes);
recorder_file_reservation_result_t recorder_file_reservation_recover(
    recorder_file_reservation_t *session,
    const char *temporary_path,
    uint32_t header_bytes);
recorder_file_reservation_result_t recorder_file_reservation_extend(
    recorder_file_reservation_t *session,
    uint64_t additional_bytes);
recorder_file_reservation_result_t recorder_file_reservation_commit_valid(
    recorder_file_reservation_t *session,
    uint64_t valid_bytes);
recorder_file_reservation_result_t recorder_file_reservation_release_unused(
    recorder_file_reservation_t *session);
recorder_file_reservation_result_t recorder_file_reservation_close(
    recorder_file_reservation_t *session);
recorder_file_reservation_result_t recorder_file_reservation_rename_closed(
    recorder_file_reservation_t *session,
    const char *final_path);
uint8_t recorder_file_reservation_map_snapshot(
    const recorder_file_reservation_t *session,
    recorder_file_reservation_map_snapshot_t *out_snapshot);
uint8_t recorder_file_reservation_map_resolve(
    const recorder_file_reservation_map_snapshot_t *snapshot,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_span_t *out_span);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_FILE_RESERVATION_H */
