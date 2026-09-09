#pragma once

#include <stdint.h>
#include "IPC/control_audio_command.h"
#include "IPC/control_music_capacity.h"
#include "IPC/audio_state_snapshot.h"

#define CONTROL_AUDIO_FIFO_MAX_PARAM_BURST   1024U
#define CONTROL_AUDIO_FIFO_MAX_NOTE_BURST \
    (2U * (CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST \
        + CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY))
#define CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST   35U
#define CONTROL_AUDIO_FIFO_CONTRACT_BURST \
    (CONTROL_AUDIO_FIFO_MAX_PARAM_BURST + CONTROL_AUDIO_FIFO_MAX_NOTE_BURST \
     + CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST)

/* Large Pattern/Project replacements publish one generated snapshot COMMIT.
 * FM remains the largest ordinary single batch. */
#define CONTROL_AUDIO_FIFO_MAX_SINGLE_NON_HORIZON_BURST 160U
/* Patch can rebuild every renderer/topology projection, then restore one FM
 * entity and its common owners/modulation. */
#define CONTROL_AUDIO_FIFO_MAX_PATCH_TRANSACTION          348U
#define CONTROL_AUDIO_FIFO_MAX_SNAPSHOT_COMMITS_IN_FLIGHT   1U

/* One dispatcher can first empty the 32-entry detent queue.  Before the next
 * 64-frame AUDIO IRQ, TIM7 can run ceil(5208.34 Hz * 64 / 48000) = 7 times
 * and each poll can enqueue the four encoders.  A group MUTE is the largest
 * per-detent delta (nine commands). */
#define CONTROL_AUDIO_FIFO_ENCODER_PENDING                 32U
#define CONTROL_AUDIO_FIFO_ENCODER_REFILL_POLLS             7U
#define CONTROL_AUDIO_FIFO_ENCODER_COUNT                    4U
#define CONTROL_AUDIO_FIFO_ENCODER_MAX_DELTA                 9U
#define CONTROL_AUDIO_FIFO_MAX_ENCODER_ACCUMULATION \
    ((CONTROL_AUDIO_FIFO_ENCODER_PENDING \
        + CONTROL_AUDIO_FIFO_ENCODER_REFILL_POLLS \
            * CONTROL_AUDIO_FIFO_ENCODER_COUNT) \
      * CONTROL_AUDIO_FIFO_ENCODER_MAX_DELTA)

/* Storage/resource projections preceding Pattern service and the remaining
 * CONTROL services are each bounded by the common 64-item publication bulk. */
#define CONTROL_AUDIO_FIFO_MAX_INCIDENTAL_BURST             64U
#define CONTROL_AUDIO_FIFO_MAX_NON_HORIZON_IN_FLIGHT \
    (CONTROL_AUDIO_FIFO_MAX_SNAPSHOT_COMMITS_IN_FLIGHT \
     + CONTROL_AUDIO_FIFO_MAX_PATCH_TRANSACTION \
     + CONTROL_AUDIO_FIFO_MAX_ENCODER_ACCUMULATION \
     + CONTROL_AUDIO_FIFO_MAX_INCIDENTAL_BURST)

#define CONTROL_AUDIO_FIFO_SAFETY_MARGIN                   512U
#define CONTROL_AUDIO_FIFO_REQUIRED \
    (CONTROL_AUDIO_FIFO_CONTRACT_BURST \
     + CONTROL_AUDIO_FIFO_MAX_NON_HORIZON_IN_FLIGHT \
     + CONTROL_AUDIO_FIFO_SAFETY_MARGIN)
#define CONTROL_AUDIO_FIFO_CAPACITY                       4096U

_Static_assert((CONTROL_AUDIO_FIFO_CAPACITY
                & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)) == 0U,
               "functional FIFO capacity must be a power of two");
_Static_assert(CONTROL_AUDIO_FIFO_CAPACITY >= CONTROL_AUDIO_FIFO_CONTRACT_BURST,
               "functional FIFO cannot contain the contractual worst burst");
_Static_assert(CONTROL_AUDIO_FIFO_MAX_NOTE_BURST == 768U,
               "music action conversion proof changed");
_Static_assert(CONTROL_AUDIO_FIFO_CONTRACT_BURST == 1827U,
               "functional FIFO aggregate proof changed");
_Static_assert(AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY == 4618U,
               "complete AUDIO projection bound changed");
_Static_assert(CONTROL_AUDIO_FIFO_MAX_SNAPSHOT_COMMITS_IN_FLIGHT == 1U,
               "single snapshot lifecycle proof changed");
_Static_assert(CONTROL_AUDIO_FIFO_MAX_ENCODER_ACCUMULATION == 540U,
               "encoder publication proof changed");
_Static_assert(CONTROL_AUDIO_FIFO_MAX_NON_HORIZON_IN_FLIGHT == 953U,
               "non-horizon accumulation proof changed");
_Static_assert(CONTROL_AUDIO_FIFO_REQUIRED == 3292U,
               "global in-flight publication proof changed");
_Static_assert(CONTROL_AUDIO_FIFO_CAPACITY >= CONTROL_AUDIO_FIFO_REQUIRED,
               "functional FIFO is below the global in-flight proof");
_Static_assert(CONTROL_AUDIO_FIFO_CAPACITY <= UINT16_MAX,
               "functional FIFO free-count ABI is uint16_t");

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t invariant_failure_count;
} control_audio_fifo_layout_t;

extern control_audio_fifo_layout_t g_control_audio_fifo_layout;
extern control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
