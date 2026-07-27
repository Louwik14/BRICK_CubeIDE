#ifndef MULTI_RECORD_WRITER_H
#define MULTI_RECORD_WRITER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MULTI_RECORD_WRITER_MAX_CLIENTS
#define MULTI_RECORD_WRITER_MAX_CLIENTS 2U
#endif

#define MULTI_RECORD_WRITER_SAMPLE_RATE_HZ 48000U
#define MULTI_RECORD_WRITER_CHANNELS 2U
#define MULTI_RECORD_WRITER_BITS_PER_SAMPLE 24U
#define MULTI_RECORD_WRITER_BYTES_PER_FRAME 6U
#define MULTI_RECORD_WRITER_PATH_MAX 96U
#define MULTI_RECORD_WRITER_RAW_SLOT_NONE 0xFFU
#define MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES 512U

#ifndef MULTI_RECORD_WRITER_RING_MS
#define MULTI_RECORD_WRITER_RING_MS 250U
#endif

#ifndef MULTI_RECORD_WRITER_RING_FRAMES
#define MULTI_RECORD_WRITER_RING_FRAMES (((MULTI_RECORD_WRITER_SAMPLE_RATE_HZ * MULTI_RECORD_WRITER_RING_MS) / 1000U) + 1U)
#endif

typedef enum
{
    MULTI_RECORD_WRITER_STATE_IDLE = 0,
    MULTI_RECORD_WRITER_STATE_PREPARED,
    MULTI_RECORD_WRITER_STATE_RECORDING,
    MULTI_RECORD_WRITER_STATE_STOP_REQUESTED,
    MULTI_RECORD_WRITER_STATE_DRAINING,
    MULTI_RECORD_WRITER_STATE_FINALIZING,
    MULTI_RECORD_WRITER_STATE_TAKE_READY,
    MULTI_RECORD_WRITER_STATE_FAILED
} multi_record_writer_state_t;

typedef enum
{
    MULTI_RECORD_WRITER_BACKEND_NONE = 0,
    MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW,
    MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV
} multi_record_writer_backend_t;

typedef enum
{
    MULTI_RECORD_WRITER_ERROR_NONE = 0,
    MULTI_RECORD_WRITER_ERROR_INVALID_CLIENT,
    MULTI_RECORD_WRITER_ERROR_INVALID_STATE,
    MULTI_RECORD_WRITER_ERROR_INVALID_PATH,
    MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW,
    MULTI_RECORD_WRITER_ERROR_SD_BUSY,
    MULTI_RECORD_WRITER_ERROR_SD_IO
} multi_record_writer_error_t;

typedef enum
{
    MULTI_RECORD_WRITER_OP_NONE = 0,
    MULTI_RECORD_WRITER_OP_PREPARE_RAW,
    MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV,
    MULTI_RECORD_WRITER_OP_WRITE_AUDIO,
    MULTI_RECORD_WRITER_OP_FINALIZE,
    MULTI_RECORD_WRITER_OP_PATCH_WAV_HEADER,
    MULTI_RECORD_WRITER_OP_RENAME_WAV
} multi_record_writer_operation_t;

typedef struct
{
    multi_record_writer_state_t state;
    multi_record_writer_backend_t backend;
    multi_record_writer_error_t error;
    multi_record_writer_operation_t last_operation;
    uint32_t last_sd_error;
    uint8_t degraded;
    uint32_t frames_pending;
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t frames_received;
    uint32_t frames_drained;
    uint32_t frames_written;
    uint32_t bytes_written;
    uint8_t raw_slot;
} multi_record_writer_status_t;

void multi_record_writer_init(void);
uint8_t multi_record_writer_prepare_raw(uint8_t client_id,
                                        uint8_t raw_slot,
                                        const char *raw_path,
                                        uint32_t expected_frames);
uint8_t multi_record_writer_prepare_sample_wav(uint8_t client_id,
                                               const char *temp_path,
                                               const char *final_path,
                                               uint32_t frame_limit);
uint8_t multi_record_writer_start(uint8_t client_id);
uint8_t multi_record_writer_request_stop(uint8_t client_id);
uint8_t multi_record_writer_push_audio_block_from_irq(uint8_t client_id,
                                                      const int32_t *lr_interleaved,
                                                      uint32_t frames);
void multi_record_writer_service(uint32_t byte_budget);
uint8_t multi_record_writer_get_status(uint8_t client_id,
                                       multi_record_writer_status_t *out_status);
uint8_t multi_record_writer_get_last_raw_take(uint8_t client_id,
                                              uint8_t *out_slot,
                                              const char **out_path,
                                              uint32_t *out_recorded_frames);
uint8_t multi_record_writer_get_last_sample_wav_take(uint8_t client_id,
                                                     const char **out_path,
                                                     uint32_t *out_recorded_frames);
uint8_t multi_record_writer_any_active(void);
uint8_t multi_record_writer_any_active_backend(multi_record_writer_backend_t backend);

#ifdef __cplusplus
}
#endif

#endif /* MULTI_RECORD_WRITER_H */
