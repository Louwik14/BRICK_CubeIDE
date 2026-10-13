#pragma once

#include <stdint.h>

#include "Storage/wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLER_WAVEFORM_BIN_COUNT 128U
#define SAMPLER_WAVEFORM_RECORD_BYTES 576U
#define SAMPLER_WAVEFORM_RAM_RECORD_COUNT 4U

typedef struct
{
    int16_t min;
    int16_t max;
} sampler_waveform_bin_t;

typedef struct
{
    uint64_t identity_hash;
    uint64_t path_hash;
    uint32_t file_size;
    uint32_t fat_date_time;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint32_t data_offset;
    uint32_t source_signature;
    uint16_t channels;
    uint16_t bits_per_sample;
} sampler_waveform_identity_t;

typedef struct
{
    sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT];
    uint8_t seen[SAMPLER_WAVEFORM_BIN_COUNT / 8U];
    uint32_t frame_count;
    uint32_t next_frame;
} sampler_waveform_accumulator_t;

typedef enum
{
    SAMPLER_WAVEFORM_RESULT_PENDING = 0,
    SAMPLER_WAVEFORM_RESULT_READY_RAM,
    SAMPLER_WAVEFORM_RESULT_READY_PERSISTENT,
    SAMPLER_WAVEFORM_RESULT_INVALID
} sampler_waveform_result_t;

void sampler_waveform_store_init(void);
void sampler_waveform_store_service(void);
void sampler_waveform_store_unregister_classic(uint16_t sample_id);
uint8_t sampler_waveform_store_register_classic(uint16_t sample_id,
                                                 const char *path,
                                                 const wav_info_t *info,
                                                 FIL *open_file);
sampler_waveform_result_t sampler_waveform_store_request_classic(
    uint16_t sample_id,
    sampler_waveform_bin_t out_bins[SAMPLER_WAVEFORM_BIN_COUNT]);
void sampler_waveform_store_release_request(void);

void sampler_waveform_accumulator_init(sampler_waveform_accumulator_t *acc,
                                       uint32_t frame_count);
void sampler_waveform_accumulator_push_float(sampler_waveform_accumulator_t *acc,
                                             const float *stereo,
                                             uint32_t frame_count);
uint8_t sampler_waveform_accumulator_finish(sampler_waveform_accumulator_t *acc,
                                            sampler_waveform_bin_t out_bins[SAMPLER_WAVEFORM_BIN_COUNT]);

/* Conversion already owns the only full audio pass. This appends and commits
 * its finished overview after the canonical WAV replacement. */
uint8_t sampler_waveform_store_commit_converted(
    const char *path,
    const wav_info_t *info,
    const sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT]);

#ifdef __cplusplus
}
#endif
