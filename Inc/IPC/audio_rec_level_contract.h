#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t generation;
    uint32_t peak_abs_pcm24;
} audio_rec_level_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t generation;
    volatile uint32_t peak_abs_pcm24;
    /* CONTROL-owned trigger watch configuration. */
    volatile uint32_t trigger_config_sequence;
    volatile uint32_t trigger_arm_epoch;
    volatile uint32_t trigger_threshold_peak_abs_pcm24;
    volatile uint32_t trigger_enabled;
    /* AUDIO-owned one-slot trigger event, CONTROL-owned acknowledgement. */
    volatile uint32_t trigger_event_sequence;
    volatile uint32_t trigger_event_arm_epoch;
    volatile uint32_t trigger_event_generation;
    volatile uint32_t trigger_event_peak_abs_pcm24;
    volatile uint32_t trigger_event_ack_sequence;
} audio_rec_level_layout_t;

_Static_assert(sizeof(audio_rec_level_snapshot_t) == 8U,
               "AUDIO REC level snapshot ABI changed");
_Static_assert(sizeof(audio_rec_level_layout_t) == 48U,
               "AUDIO REC level layout ABI changed");

extern audio_rec_level_layout_t g_audio_rec_level_layout;
