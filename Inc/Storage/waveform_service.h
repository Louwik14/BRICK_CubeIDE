#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

typedef struct
{
    sample_audio_key_t key;
    uint32_t registration_epoch;
    uint32_t frame_count;
} waveform_source_t;

typedef struct
{
    int16_t min;
    int16_t max;
} waveform_column_t;

typedef enum
{
    WAVEFORM_RESULT_PENDING = 0,
    WAVEFORM_RESULT_READY,
    WAVEFORM_RESULT_INVALID
} waveform_result_t;

/* REC overview remains READY; finer min/max tiles are built cooperatively. */
uint8_t waveform_rec_current_source(waveform_source_t *out_source);
void waveform_service_storage_service(void);
waveform_result_t waveform_request(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns);
