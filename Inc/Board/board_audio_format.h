#pragma once

#include <stdint.h>

#define BOARD_AUDIO_SAMPLE_RATE_HZ 48000U
#define BOARD_AUDIO_FRAMES_PER_HALF 64U
#define BOARD_AUDIO_FRAMES_TOTAL (BOARD_AUDIO_FRAMES_PER_HALF * 2U)

/*
 * Common audio contract used by both product variants.
 *
 * The active board transport is still selected below until the Premium board
 * adapter is migrated.  These constants are intentionally variant-neutral so
 * the common audio layer has one authoritative target contract during that
 * migration.
 */
#define BOARD_AUDIO_CONTRACT_SAMPLE_RATE_HZ 48000U
#define BOARD_AUDIO_CONTRACT_SAMPLE_BITS 24U
#define BOARD_AUDIO_CONTRACT_SLOT_BITS 32U
#define BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF 64U
#define BOARD_AUDIO_CONTRACT_FRAMES_TOTAL \
    (BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF * 2U)
#define BOARD_AUDIO_CONTRACT_TDM_SLOTS 2U
#define BOARD_AUDIO_CONTRACT_LOGICAL_CHANNELS 2U
#define BOARD_AUDIO_CONTRACT_WORDS_PER_FRAME BOARD_AUDIO_CONTRACT_TDM_SLOTS
#define BOARD_AUDIO_CONTRACT_BUFFER_WORDS \
    (BOARD_AUDIO_CONTRACT_FRAMES_TOTAL * BOARD_AUDIO_CONTRACT_WORDS_PER_FRAME)

#if (BOARD_AUDIO_SAMPLE_RATE_HZ != BOARD_AUDIO_CONTRACT_SAMPLE_RATE_HZ) \
    || (BOARD_AUDIO_FRAMES_PER_HALF != BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF)
#error "Board audio timing must match the common audio contract"
#endif

#if defined(BRICK6_VARIANT_LOWCOST)
#define BOARD_AUDIO_TDM_SLOTS 2U
#else
#define BOARD_AUDIO_TDM_SLOTS 8U
#endif
#define BOARD_AUDIO_WORDS_PER_FRAME BOARD_AUDIO_TDM_SLOTS
#define BOARD_AUDIO_BUFFER_WORDS (BOARD_AUDIO_FRAMES_TOTAL * BOARD_AUDIO_WORDS_PER_FRAME)
