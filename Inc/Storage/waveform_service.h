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

/* GDB-visible counters for the current sampler Waveview source. Times are
 * wall-clock milliseconds; cycle totals use DWT->CYCCNT deltas. */
typedef struct
{
    uint32_t source_frames;
    uint32_t pixel_width;
    uint32_t requests;
    uint32_t ready_returns;
    uint32_t pending_returns;
    uint32_t invalid_returns;
    uint32_t ram_cache_hits;
    uint32_t ram_cache_misses;
    uint32_t sidecar_hits;
    uint32_t sidecar_misses;
    uint32_t persistent_hits;
    uint32_t persistent_misses;
    uint32_t persistent_read_calls;
    uint32_t persistent_read_bytes;
    uint32_t build_starts;
    uint32_t build_aborts;
    uint32_t build_completions;
    uint32_t file_opens;
    uint32_t format_parses;
    uint32_t read_calls;
    uint32_t read_bytes;
    uint32_t read_max_bytes;
    uint32_t request_to_build_ms;
    uint32_t request_to_first_pixel_ms;
    uint32_t request_to_complete_ms;
    uint64_t io_cycles;
    uint64_t reduction_cycles;
} waveform_diag_t;

extern volatile waveform_diag_t g_waveform_diag;
void waveform_diag_reset(void);

/* REC overview remains READY; finer min/max tiles are built cooperatively. */
void waveform_service_init(void);
uint8_t waveform_rec_current_source(waveform_source_t *out_source);
uint8_t waveform_sampler_source(uint16_t sample_id,
                                waveform_source_t *out_source);
void waveform_sampler_request_release(void);
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
