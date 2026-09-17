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

typedef struct
{
    int16_t point;
    uint8_t ready;
} waveform_line_column_t;

typedef enum
{
    WAVEFORM_RESULT_PENDING = 0,
    WAVEFORM_RESULT_READY,
    WAVEFORM_RESULT_INVALID
} waveform_result_t;

/* REC overview remains READY; finer min/max tiles are built cooperatively. */
uint8_t waveform_rec_current_source(waveform_source_t *out_source);
uint16_t waveform_rec_peak(const waveform_source_t *source);
void waveform_service_storage_service(void);
uint8_t waveform_service_local_pending(void);
void waveform_service_local_suspend(void);
waveform_result_t waveform_request(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns);
/* REC overview only: read-only, with no local focus, paging or BG requests. */
waveform_result_t waveform_request_overview(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns,
                                   waveform_line_column_t *line);
waveform_result_t waveform_request_detailed(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns,
                                   waveform_line_column_t *line);
