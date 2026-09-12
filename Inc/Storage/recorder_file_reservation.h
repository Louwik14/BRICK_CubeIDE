#ifndef RECORDER_FILE_RESERVATION_H
#define RECORDER_FILE_RESERVATION_H

#include <stdint.h>

#include "Platform/memory_layout.h"
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
    RECORDER_FILE_RESERVATION_PROGRESS,
    RECORDER_FILE_RESERVATION_IO_STARTED,
    RECORDER_FILE_RESERVATION_RECOVERY_ABORT,
    RECORDER_FILE_RESERVATION_INVALID_ARG,
    RECORDER_FILE_RESERVATION_INVALID_STATE,
    RECORDER_FILE_RESERVATION_SD_BUSY,
    RECORDER_FILE_RESERVATION_NO_SPACE,
    RECORDER_FILE_RESERVATION_MAP_FULL,
    RECORDER_FILE_RESERVATION_FS_ERROR
} recorder_file_reservation_result_t;

typedef enum
{
    RECORDER_FILE_JOB_NONE = 0,
    RECORDER_FILE_JOB_EXTEND,
    RECORDER_FILE_JOB_COMMIT,
    RECORDER_FILE_JOB_SYNC,
    RECORDER_FILE_JOB_TERMINAL
} recorder_file_job_phase_t;

typedef struct
{
    const sample_stream_physical_extent_t *extents;
    uint64_t reserved_file_bytes;
    uint64_t valid_file_bytes;
    uint32_t media_epoch;
    uint16_t extent_count;
    uint16_t sector_size;
} recorder_file_reservation_map_snapshot_t;

typedef struct
{
    FIL file;
    FF_BRICK_REC_STATE fs_state;
    FF_BRICK_REC_EXTENT fs_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    sample_stream_physical_extent_t physical_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    char path[RECORDER_FILE_RESERVATION_PATH_MAX];
    volatile uint32_t publish_sequence;
    volatile uint64_t published_reserved_file_bytes;
    volatile uint64_t published_valid_file_bytes;
    volatile uint32_t published_media_epoch;
    volatile uint16_t published_extent_count;
    uint16_t extent_count;
    uint32_t header_bytes;
    uint64_t reserved_bytes;
    uint64_t valid_bytes;
    uint64_t job_target_file_bytes;
    FF_BRICK_REC_RESERVE_CONT reserve_cont;
    FF_META_OBJECT_SYNC_CONT sync_cont;
    recorder_file_reservation_result_t job_result;
    recorder_file_job_phase_t job_phase;
    uint32_t job_media_epoch;
    uint32_t job_io_lba;
    uint32_t job_io_sequence;
    uint32_t job_io_identity;
    const void *job_io_buffer;
    uint16_t job_old_extent_count;
    UINT job_added_extent_count;
    ALIGN32 uint8_t metadata_staging[512U];
    uint8_t job_io_operation;
    uint8_t job_io_active;
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
recorder_file_reservation_result_t recorder_file_reservation_extend_begin(
    recorder_file_reservation_t *session,
    uint64_t additional_bytes);
recorder_file_reservation_result_t recorder_file_reservation_commit_begin(
    recorder_file_reservation_t *session,
    uint64_t valid_bytes);
recorder_file_reservation_result_t recorder_file_reservation_sync_begin(
    recorder_file_reservation_t *session);
recorder_file_reservation_result_t recorder_file_reservation_job_step(
    recorder_file_reservation_t *session);
recorder_file_reservation_result_t recorder_file_reservation_job_poll(
    recorder_file_reservation_t *session);
uint8_t recorder_file_reservation_job_active(
    const recorder_file_reservation_t *session);
void recorder_file_reservation_job_finish(recorder_file_reservation_t *session);
void recorder_file_reservation_job_cancel(recorder_file_reservation_t *session);
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
