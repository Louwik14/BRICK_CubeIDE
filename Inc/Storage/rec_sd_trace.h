#pragma once

#include <stdint.h>

/* Thirteen words per entry. The sequence is written last; zero means unused. */
#define REC_SD_TRACE_CAPACITY 64U
#define REC_SD_TRACE_STATES(rec_before, rec_after, storage_before, storage_after) \
    ((uint32_t)(uint8_t)(rec_before) | ((uint32_t)(uint8_t)(rec_after) << 8U) \
     | ((uint32_t)(uint8_t)(storage_before) << 16U) \
     | ((uint32_t)(uint8_t)(storage_after) << 24U))
#define REC_SD_TRACE_CONTEXT(overdub_before, overdub_after, arm, trigger) \
    ((uint32_t)(uint8_t)(overdub_before) | ((uint32_t)(uint8_t)(overdub_after) << 8U) \
     | ((uint32_t)(uint8_t)(arm) << 16U) | ((uint32_t)(uint8_t)(trigger) << 24U))

typedef enum {
    REC_SD_TRACE_ARM = 1,
    REC_SD_TRACE_TRIGGER,
    REC_SD_TRACE_PREPARE,
    REC_SD_TRACE_START_REQUEST,
    REC_SD_TRACE_STOP_REQUEST,
    REC_SD_TRACE_CANCEL,
    REC_SD_TRACE_REC_STATE,
    REC_SD_TRACE_STORAGE,
    REC_SD_TRACE_AUDIO_START,
    REC_SD_TRACE_AUDIO_STOP,
    REC_SD_TRACE_AUDIO_CLOSE,
    REC_SD_TRACE_OVERDUB_BIND,
    REC_SD_TRACE_OVERDUB_STOP,
    REC_SD_TRACE_OVERDUB_FAULT,
    REC_SD_TRACE_TAKE,
    REC_SD_TRACE_SD_GATE,
    REC_SD_TRACE_SD_SCHED,
    REC_SD_TRACE_SD_IO,
    REC_SD_TRACE_SD_FS
} rec_sd_trace_event_t;

typedef struct {
    uint8_t requester;
    uint8_t operation;
    uint8_t admission;
    uint8_t block_result;
    uint8_t fatfs_result;
} rec_sd_trace_sd_meta_t;

typedef enum {
    REC_SD_OP_NONE = 0,
    REC_SD_OP_PATH_MOUNT,
    REC_SD_OP_PATH_MKDIR,
    REC_SD_OP_PATH_SLOT,
    REC_SD_OP_PREPARE,
    REC_SD_OP_WRITE,
    REC_SD_OP_FINALIZE,
    REC_SD_OP_STOP,
    REC_SD_OP_CANCEL,
    REC_SD_OP_DMA,
    REC_SD_OP_DISK_READ,
    REC_SD_OP_DISK_WRITE
} rec_sd_trace_operation_t;

typedef enum {
    REC_SD_ADMISSION_NONE = 0,
    REC_SD_ADMISSION_ACCEPTED,
    REC_SD_ADMISSION_DEFERRED,
    REC_SD_ADMISSION_ERROR,
    REC_SD_ADMISSION_COMPLETED
} rec_sd_trace_admission_t;

typedef struct {
    uint32_t sequence;
    uint32_t event;
    uint32_t states;
    uint32_t context;
    uint32_t detail;
    uint32_t frames;
    uint32_t session;
    uint32_t sample_lo;
    uint32_t sd_owner;
    uint32_t sd_flags;
    uint32_t sd_io;
    uint32_t sd_result;
    uint32_t sd_hal_error;
} rec_sd_trace_entry_t;

extern volatile rec_sd_trace_entry_t g_rec_sd_trace[REC_SD_TRACE_CAPACITY];
extern volatile uint32_t g_rec_sd_trace_next;

void rec_sd_trace_log(rec_sd_trace_event_t event, uint32_t states,
                      uint32_t context, uint32_t detail, uint32_t frames,
                      uint32_t session, uint64_t sample);
void rec_sd_trace_log_sd(rec_sd_trace_event_t event, uint32_t states,
                         uint32_t context, uint32_t detail, uint32_t frames,
                         uint32_t session, uint64_t sample,
                         rec_sd_trace_sd_meta_t meta);
void rec_sd_trace_note_sd(rec_sd_trace_event_t event, uint32_t detail,
                          rec_sd_trace_sd_meta_t meta);
