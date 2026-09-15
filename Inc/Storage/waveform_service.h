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

typedef struct
{
    volatile uint32_t requests, invalid, pending_summary;
    volatile uint32_t ready_pcm, pcm_not_ready;
    volatile uint32_t ready_ideal_local, ready_ideal_sidecar;
    volatile uint32_t ready_fallback_local, ready_fallback_sidecar;
    volatile uint32_t ready_overview;
    volatile uint32_t ideal_local_missing, ideal_sidecar_missing;
    volatile uint32_t build_restarts, tile_evictions;
    volatile uint32_t last_frames_per_pixel, last_start_frame;
    volatile uint32_t last_request_cycle;
    volatile uint32_t pcm_no_page, pcm_reserved, pcm_loading;
    volatile uint32_t pcm_bad_key_epoch, pcm_other;
    volatile uint32_t page_requests_new, page_requests_ready;
    volatile uint32_t page_requests_preexisting;
    volatile uint32_t page_reservations, page_ready;
    volatile uint32_t page_reservation_to_ready_max_cycles;
    volatile uint32_t page_reservation_to_ready_last_cycles;
    volatile uint32_t page_preexisting_to_ready_max_cycles;
    volatile uint32_t page_tracking_overwrites;
    volatile uint32_t ideal_tile_absent, ideal_tile_building;
    volatile uint32_t ideal_sidecar_pending;
    volatile uint32_t minmax_gate_go, minmax_gate_not_now;
    volatile uint32_t minmax_last_sd_owner;
    volatile uint8_t last_ideal_level, last_display_level;
} waveform_latency_diag_t;

#define WAVEFORM_PAGE_DIAG_CAPACITY 32U
typedef struct
{
    sample_audio_key_t key;
    uint32_t registration_epoch;
    uint32_t page_index;
    volatile uint32_t reserved_cycle;
    volatile uint32_t ready_cycle;
    volatile uint32_t elapsed_cycles;
    volatile uint8_t state; /* 0 empty, 1 waiting, 2 READY */
    volatile uint8_t preexisting; /* 1: already RESERVED/LOADING at demand */
} waveform_page_diag_entry_t;

extern volatile waveform_latency_diag_t g_waveform_latency_diag;
extern volatile waveform_page_diag_entry_t
    g_waveform_page_diag_ring[WAVEFORM_PAGE_DIAG_CAPACITY];
extern volatile uint32_t g_waveform_page_diag_head;
void waveform_diag_page_ready(sample_audio_key_t key,
    uint32_t registration_epoch, uint32_t page_index);

/* REC overview remains READY; finer min/max tiles are built cooperatively. */
uint8_t waveform_rec_current_source(waveform_source_t *out_source);
uint16_t waveform_rec_peak(const waveform_source_t *source);
void waveform_service_storage_service(void);
waveform_result_t waveform_request(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns);
