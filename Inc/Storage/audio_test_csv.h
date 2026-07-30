#pragma once

#include <stdint.h>

#include "Audio/audio_track_diag.h"
#include "Core/cpu_load.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_TEST_CSV_RESULT_NONE = 0,
    AUDIO_TEST_CSV_RESULT_SESSION_OK,
    AUDIO_TEST_CSV_RESULT_ROW_OK,
    AUDIO_TEST_CSV_RESULT_ERROR
} audio_test_csv_result_t;

typedef struct
{
    uint32_t run_id;
    uint16_t test_index;
    uint16_t test_total;
    const char *test_phase;
    const char *test_name;
    const char *test_status;
    uint32_t warmup_ms;
    uint32_t measure_ms;
    uint8_t track_count;
    uint8_t voice_count;
    const char *notes;
    const char *source_config;
    const char *filter_config;
    const char *fx_config;
    const char *master_config;
    cpu_load_metrics_t cpu_metrics;
    float delay_send;
    float reverb_send;
    float delay_mix;
    float delay_feedback;
    float delay_time;
    float reverb_mix;
    float reverb_size;
    float reverb_decay;
    float reverb_damping;
    float tail_early_wet_peak;
    float tail_late_wet_peak;
    uint32_t return_over_full_scale_count;
    uint32_t nonfinite_count;
    uint8_t tail_cut_detected;
    uint8_t tail_rising_detected;
    uint8_t final_saturation_detected;
    uint8_t irq_overload_detected;
    uint8_t headroom_exceeded;
    float sum_expected_ratio;
    float sum_peak_ratio;
    float sum_rms_ratio;
    uint8_t sum_progression_fail;
    const char *row_type;
    const char *sound_type;
    const char *measurement_phase;
    uint8_t note;
    uint8_t velocity;
    uint8_t model_id;
    float timbre;
    float color;
    uint8_t oscillator_count;
    const char *oscillator_mode;
    uint8_t repetition;
} audio_test_csv_case_t;

typedef struct
{
    uint32_t run_id;
    uint16_t test_total;
    const char *engine;
    const char *model_name;
    const char *sound_type;
    uint8_t model_id;
    uint16_t observation_count;
    float weighted_median;
    float rms_median;
    float peak_high;
    float crest_representative;
    float worst_dc;
    uint32_t total_clips;
    const char *weakest_scenario;
    const char *strongest_scenario;
    float recommended_gain_db;
    float remaining_headroom_db;
    const char *status;
} audio_test_csv_summary_t;

void audio_test_csv_init(void);
void audio_test_csv_begin_session(void);
uint8_t audio_test_csv_enqueue_auto(uint8_t track,
                                    const audio_test_csv_case_t *test_case,
                                    const audio_track_diag_snapshot_t *track_snapshot,
                                    const audio_global_diag_snapshot_t *global_snapshot);
uint8_t audio_test_csv_enqueue_summary(
    const audio_test_csv_summary_t *summary);
void audio_test_csv_service(void);
audio_test_csv_result_t audio_test_csv_take_result(void);
uint8_t audio_test_csv_is_busy(void);

#ifdef __cplusplus
}
#endif
