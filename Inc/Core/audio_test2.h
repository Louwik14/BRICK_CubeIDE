#ifndef AUDIO_TEST2_H
#define AUDIO_TEST2_H

#include <stdint.h>

#include "Core/brick_build_config.h"

#if BRICK_TEST_BUILD

#define AUDIO_TEST2_SAMPLE_RATE 48000U
#define AUDIO_TEST2_DURATION_FRAMES 11904000U
#define AUDIO_TEST2_DURATION_SECONDS 248U

typedef enum
{
    AUDIO_TEST2_IDLE = 0,
    AUDIO_TEST2_READY,
    AUDIO_TEST2_REFERENCE,
    AUDIO_TEST2_INTERNAL,
    AUDIO_TEST2_VERIFY,
    AUDIO_TEST2_LINE_READY,
    AUDIO_TEST2_COUNTDOWN_LINE,
    AUDIO_TEST2_LINE,
    AUDIO_TEST2_HEADPHONE_READY,
    AUDIO_TEST2_COUNTDOWN_HEADPHONE,
    AUDIO_TEST2_HEADPHONE,
    AUDIO_TEST2_DONE,
    AUDIO_TEST2_ERROR
} audio_test2_state_t;

typedef struct
{
    audio_test2_state_t state;
    uint32_t frame;
    uint32_t total_frames;
    uint32_t reference_crc;
    uint32_t internal_crc;
    uint32_t sd_errors;
    uint32_t underruns;
    uint32_t overruns;
    uint32_t clips;
    uint32_t nonfinite;
    uint32_t irq_peak_permille;
    uint8_t countdown;
    const char *section;
    const char *status;
} audio_test2_view_t;

void audio_test2_init(void);
void audio_test2_service(void);
uint8_t audio_test2_start_internal(void);
uint8_t audio_test2_start_line(void);
uint8_t audio_test2_start_headphone(void);
void audio_test2_cancel(void);
uint8_t audio_test2_is_active(void);
void audio_test2_get_view(audio_test2_view_t *out_view);
uint8_t audio_test2_process_irq(int32_t *tx, uint32_t frames);

#else

static inline void audio_test2_init(void) {}
static inline void audio_test2_service(void) {}
static inline uint8_t audio_test2_start_internal(void) { return 0U; }
static inline uint8_t audio_test2_start_line(void) { return 0U; }
static inline uint8_t audio_test2_start_headphone(void) { return 0U; }
static inline void audio_test2_cancel(void) {}
static inline uint8_t audio_test2_is_active(void) { return 0U; }
static inline uint8_t audio_test2_process_irq(int32_t *tx, uint32_t frames)
{
    (void)tx;
    (void)frames;
    return 0U;
}

#endif

#endif
