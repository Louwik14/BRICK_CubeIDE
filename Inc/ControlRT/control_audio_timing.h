#pragma once

#include "Board/board_audio_format.h"

/* CONTROL never commits functional commands farther than one audio block
 * ahead of its TIM5-derived present.  Resource retirement additionally keeps
 * two complete DMA halves for command application and the following render. */
/* Keep the product timing contract unchanged during the temporary 32-frame
 * AUDIO benchmark: 64 frames of publication horizon and 192 frames of
 * retirement grace still represent the same wall-clock durations. */
#define CONTROL_AUDIO_MAX_PUBLICATION_HORIZON_FRAMES 64U
#define CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES 192U

_Static_assert(CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES == 192U,
               "resource retirement bound changed; review the hard-RT proof");
