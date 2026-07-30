#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_TEST_RUNNER_TEST_TOTAL 3602U
#define AUDIO_TEST_RUNNER_ESTIMATED_DURATION_MS 6200000U

typedef enum
{
    AUDIO_TEST_RUNNER_IDLE = 0,
    AUDIO_TEST_RUNNER_PREPARE,
    AUDIO_TEST_RUNNER_VOLUME_WARNING,
    AUDIO_TEST_RUNNER_CONFIGURE,
    AUDIO_TEST_RUNNER_NOTE_ON,
    AUDIO_TEST_RUNNER_ATTACK_MEASURE,
    AUDIO_TEST_RUNNER_ATTACK_CAPTURE,
    AUDIO_TEST_RUNNER_PERCUSSIVE_DECAY,
    AUDIO_TEST_RUNNER_WARMUP,
    AUDIO_TEST_RUNNER_MEASURE,
    AUDIO_TEST_RUNNER_CAPTURE,
    AUDIO_TEST_RUNNER_NOTE_OFF,
    AUDIO_TEST_RUNNER_FX_TAIL_EARLY,
    AUDIO_TEST_RUNNER_FX_TAIL_LATE,
    AUDIO_TEST_RUNNER_WRITE,
    AUDIO_TEST_RUNNER_NEXT,
    AUDIO_TEST_RUNNER_SUMMARY_WRITE,
    AUDIO_TEST_RUNNER_SUMMARY_NEXT,
    AUDIO_TEST_RUNNER_RESTORE,
    AUDIO_TEST_RUNNER_DONE,
    AUDIO_TEST_RUNNER_STOPPED,
    AUDIO_TEST_RUNNER_ERROR
} audio_test_runner_state_t;

typedef struct
{
    audio_test_runner_state_t state;
    uint16_t test_index;
    uint16_t test_total;
    uint8_t progress_12;
    char test_name[32];
    char phase[12];
    char status[16];
} audio_test_runner_view_t;

void audio_test_runner_init(void);
uint8_t audio_test_runner_start(void);
void audio_test_runner_tick(void);
void audio_test_runner_cancel(void);
uint8_t audio_test_runner_is_active(void);
void audio_test_runner_get_view(audio_test_runner_view_t *out_view);

#ifdef __cplusplus
}
#endif
