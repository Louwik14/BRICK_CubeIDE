#ifndef PATCH_PREVIEW_AUDIO_H
#define PATCH_PREVIEW_AUDIO_H

#include <stdint.h>

#include "ControlRT/patch_preview_contract.h"

void patch_preview_audio_init(void);
uint8_t patch_preview_audio_prepare(
    const patch_preview_publication_t *publication);
uint8_t patch_preview_audio_note_on(uint8_t note, uint8_t velocity);
uint8_t patch_preview_audio_note_off(uint8_t note);
uint8_t patch_preview_audio_stop(void);
uint8_t patch_preview_audio_render_main(float *out_main_l, float *out_main_r,
                                        uint32_t frames);

#endif /* PATCH_PREVIEW_AUDIO_H */
