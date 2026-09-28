#pragma once

#include <stdint.h>

/* Temporary 1 kHz / 48 kHz stereo diagnostic. All sample values are signed Q24. */
#define AUDIO_CHAIN_DIAG_PERIOD 48U
#define AUDIO_CHAIN_DIAG_SNAPSHOT 9U

enum {
    AUDIO_CHAIN_PCM32 = 0,
    AUDIO_CHAIN_USB_FLOAT,
    AUDIO_CHAIN_RING_READ,
    AUDIO_CHAIN_ENGINE_INPUT,
    AUDIO_CHAIN_MAIN_FLOAT,
    AUDIO_CHAIN_INT24,
    AUDIO_CHAIN_TX_DMA,
    AUDIO_CHAIN_STAGE_COUNT
};

typedef struct {
    uint32_t samples_checked;          /* stereo frames compared with n-48 */
    uint32_t glitch_count;             /* frames exceeding threshold, either channel */
    uint32_t max_error;                /* largest absolute Q24 period error */
    uint32_t first_glitch_tick;        /* HAL milliseconds */
    uint32_t first_glitch_block;       /* stage-local observe call, starting at 1 */
    uint32_t first_glitch_sample_index;/* frame offset in that observe call */
    uint32_t first_glitch_stream_index;/* stage-local cumulative frame offset */
    uint32_t blocks;
    uint32_t frames_seen;
    uint32_t armed;                    /* amplitude observed above 0.04 FS */
    uint32_t armed_frame;              /* first loud frame; checks begin 100 ms later */
    uint32_t snapshot_count;           /* up to 9 frames, 4 before and 4 after */
    int32_t snapshot[AUDIO_CHAIN_DIAG_SNAPSHOT][2];
    int32_t history[AUDIO_CHAIN_DIAG_PERIOD][2];
} audio_chain_diag_stage_t;

typedef struct {
    uint32_t magic;                    /* 0x41434431 after reset */
    uint32_t version;                  /* 1 */
    uint32_t tx_deadline_miss_count;
    uint32_t tx_wrong_half_count;
    uint32_t tx_ndtr_begin;            /* last TX DMA NDTR, in 32-bit words */
    uint32_t tx_ndtr_end;
    uint32_t tx_min_margin;            /* minimum safe words left at render end */
    uint32_t tx_last_half;             /* half currently rendered: 0/1 */
    uint32_t tx_last_margin;
    uint32_t tx_dma_callback_count;
    uint32_t tx_dma_last_half;          /* half whose consumption just began */
    uint32_t tx_dma_half_sequence_error_count;
    uint32_t tx_near_deadline_count;    /* <= 8 DMA words at render end */
    uint32_t rx_callback_skipped_count;
    uint32_t rx_callback_recovered_count;
    uint32_t ring_underflow_count;
    uint32_t ring_drop_frames;
    uint32_t usb_fifo_error_count;
    uint32_t pcm_float_mismatch_count; /* Q24 conversion difference > 2 LSB */
    uint32_t float_int24_mismatch_count;/* Q24 conversion difference > 3 LSB */
    audio_chain_diag_stage_t stage[AUDIO_CHAIN_STAGE_COUNT];
} audio_chain_diag_t;

extern volatile audio_chain_diag_t g_audio_chain_diag;

void audio_chain_diag_reset(void);
void audio_chain_diag_i32(uint32_t stage, const int32_t *interleaved,
                          uint32_t frames, uint32_t stride, uint32_t shift);
void audio_chain_diag_float(uint32_t stage, const float *left,
                            const float *right, uint32_t frames);
void audio_chain_diag_float_interleaved(uint32_t stage, const float *samples,
                                        uint32_t frames);
void audio_chain_diag_compare_pcm_float(const int32_t *pcm,
                                        const float *interleaved, uint32_t frames);
void audio_chain_diag_compare_float_int24(const float *left, const float *right,
                                           const int32_t *tx, uint32_t frames,
                                           uint32_t stride);
