#ifndef LOOPER_STORAGE_H
#define LOOPER_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOOPER_STORAGE_SAVE_PATH_TRIES 10000U
#define LOOPER_STORAGE_RAW_SLOT_COUNT 4U
#define LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ 48000U
#define LOOPER_STORAGE_RAW_CHANNELS 2U
#define LOOPER_STORAGE_RAW_BITS_PER_SAMPLE 24U
#define LOOPER_STORAGE_RAW_BYTES_PER_FRAME 6U
#define LOOPER_STORAGE_RAW_RESERVOIR_BYTES 999999996UL
#define LOOPER_STORAGE_RAW_CAPACITY_FRAMES \
    (LOOPER_STORAGE_RAW_RESERVOIR_BYTES / LOOPER_STORAGE_RAW_BYTES_PER_FRAME)

typedef enum
{
    LOOPER_STORAGE_PATH_OK = 0,
    LOOPER_STORAGE_PATH_BUSY,
    LOOPER_STORAGE_PATH_FAIL
} looper_storage_path_result_t;

typedef enum
{
    LOOPER_STORAGE_RAW_ERROR_NONE = 0,
    LOOPER_STORAGE_RAW_ERROR_NOT_VALIDATED,
    LOOPER_STORAGE_RAW_ERROR_SD_BUSY,
    LOOPER_STORAGE_RAW_ERROR_MOUNT_FAIL,
    LOOPER_STORAGE_RAW_ERROR_MISSING,
    LOOPER_STORAGE_RAW_ERROR_STAT_FAIL,
    LOOPER_STORAGE_RAW_ERROR_SIZE_MISMATCH
} looper_storage_raw_error_t;

typedef enum
{
    LOOPER_STORAGE_RAW_EXPORT_IDLE = 0,
    LOOPER_STORAGE_RAW_EXPORT_ACTIVE,
    LOOPER_STORAGE_RAW_EXPORT_DONE,
    LOOPER_STORAGE_RAW_EXPORT_FAILED
} looper_storage_raw_export_state_t;

typedef enum
{
    LOOPER_STORAGE_RAW_EXPORT_ERROR_NONE = 0,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_INVALID_ARG,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_BUSY,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_SD_BUSY,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_MOUNT_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_OPEN_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_SEEK_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_READ_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_WRITE_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_SYNC_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_CLOSE_FAIL,
    LOOPER_STORAGE_RAW_EXPORT_ERROR_VERIFY_FAIL
} looper_storage_raw_export_error_t;

typedef struct
{
    uint32_t recorded_frames;
    uint32_t raw_bytes_expected;
    uint32_t wav_data_bytes_written;
    uint32_t wav_data_offset;
    uint32_t first_compare_frames;
    uint32_t last_compare_frames;
    uint32_t first_compare_start_frame;
    uint32_t last_compare_start_frame;
    uint32_t first_mismatch_data_offset;
    uint8_t first_raw_bytes[16U * LOOPER_STORAGE_RAW_BYTES_PER_FRAME];
    uint8_t first_wav_bytes[16U * LOOPER_STORAGE_RAW_BYTES_PER_FRAME];
    uint8_t last_raw_bytes[16U * LOOPER_STORAGE_RAW_BYTES_PER_FRAME];
    uint8_t last_wav_bytes[16U * LOOPER_STORAGE_RAW_BYTES_PER_FRAME];
    uint8_t verified;
} looper_storage_raw_export_diag_t;

void looper_storage_raw_init(void);
uint8_t looper_storage_raw_validate(void);
uint8_t looper_storage_raw_is_available(void);
const char *looper_storage_raw_get_path(uint8_t slot);
uint32_t looper_storage_raw_get_capacity_frames(void);
looper_storage_raw_error_t looper_storage_raw_get_last_error(void);
uint8_t looper_storage_raw_get_last_failed_slot(void);
uint32_t looper_storage_raw_get_last_fresult(void);
uint64_t looper_storage_raw_get_last_observed_size(void);
uint8_t looper_storage_raw_get_slot_for_track(uint8_t track_id, uint8_t *out_slot);
uint8_t looper_storage_raw_track_is_available(uint8_t track_id);
uint8_t looper_storage_raw_export_start(uint8_t track_id,
                                        uint8_t raw_slot,
                                        const char *raw_path,
                                        uint32_t recorded_frames,
                                        const char *final_path);
void looper_storage_raw_export_service(uint32_t byte_budget);
uint8_t looper_storage_raw_export_is_active(void);
looper_storage_raw_export_state_t looper_storage_raw_export_get_state(void);
looper_storage_raw_export_error_t looper_storage_raw_export_get_last_error(void);
void looper_storage_raw_export_get_diag(looper_storage_raw_export_diag_t *out_diag);
uint8_t looper_storage_raw_export_get_progress_percent(void);
const char *looper_storage_raw_export_get_final_path(void);
void looper_storage_raw_export_clear_finished(void);

looper_storage_path_result_t looper_storage_make_next_path(uint8_t track_id,
                                                           char *out_path,
                                                           uint32_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* LOOPER_STORAGE_H */
