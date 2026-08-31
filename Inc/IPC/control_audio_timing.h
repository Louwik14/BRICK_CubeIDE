#pragma once

#include "Board/board_audio_format.h"

/* CONTROL never commits functional commands farther than one audio block
 * ahead of its TIM5-derived present.  Resource retirement additionally keeps
 * two complete DMA halves for command application and the following render. */
#define CONTROL_AUDIO_MAX_PUBLICATION_HORIZON_FRAMES \
    BOARD_AUDIO_FRAMES_PER_HALF
#define CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES \
    (CONTROL_AUDIO_MAX_PUBLICATION_HORIZON_FRAMES \
     + (2U * BOARD_AUDIO_FRAMES_PER_HALF))

_Static_assert(CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES == 192U,
               "resource retirement bound changed; review the hard-RT proof");
