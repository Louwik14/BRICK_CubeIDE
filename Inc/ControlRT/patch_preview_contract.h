#ifndef PATCH_PREVIEW_CONTRACT_H
#define PATCH_PREVIEW_CONTRACT_H

#include <stdint.h>

#include "ControlRT/fm_dsp_projection.h"

#define PATCH_PREVIEW_PUBLICATION_SLOT_COUNT 2U

typedef enum
{
    PATCH_PREVIEW_COMMAND_PREPARE = 0U,
    PATCH_PREVIEW_COMMAND_NOTE_ON,
    PATCH_PREVIEW_COMMAND_NOTE_OFF,
    PATCH_PREVIEW_COMMAND_STOP
} patch_preview_command_t;

typedef struct
{
    track_tone_fm_base_voice_t base;
    track_tone_fm_macros_t macros;
    uint32_t generation;
} patch_preview_fm_publication_t;

extern patch_preview_fm_publication_t
    g_patch_preview_fm_publication[PATCH_PREVIEW_PUBLICATION_SLOT_COUNT];
extern volatile uint32_t g_patch_preview_audio_consumed_generation;

#endif /* PATCH_PREVIEW_CONTRACT_H */
