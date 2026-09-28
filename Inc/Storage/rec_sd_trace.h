#pragma once

#include <stdint.h>

/* Eight words per entry. The sequence is written last; zero means unused. */
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
    REC_SD_TRACE_TAKE
} rec_sd_trace_event_t;

typedef struct {
    uint32_t sequence;
    uint32_t event;
    uint32_t states;
    uint32_t context;
    uint32_t detail;
    uint32_t frames;
    uint32_t session;
    uint32_t sample_lo;
} rec_sd_trace_entry_t;

extern volatile rec_sd_trace_entry_t g_rec_sd_trace[REC_SD_TRACE_CAPACITY];
extern volatile uint32_t g_rec_sd_trace_next;

void rec_sd_trace_log(rec_sd_trace_event_t event, uint32_t states,
                      uint32_t context, uint32_t detail, uint32_t frames,
                      uint32_t session, uint64_t sample);
