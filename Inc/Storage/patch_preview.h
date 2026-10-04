#ifndef PATCH_PREVIEW_H
#define PATCH_PREVIEW_H

#include <stdint.h>

#include "Storage/persistent_control_model.h"

#define PATCH_PREVIEW_DEFAULT_NOTE 60U
#define PATCH_PREVIEW_DEFAULT_VELOCITY 100U

void patch_preview_init(void);
uint8_t patch_preview_prepare(const persist_control_patch_t *patch);
uint8_t patch_preview_note_on(uint8_t note, uint8_t velocity);
uint8_t patch_preview_note_off(void);
uint8_t patch_preview_stop(void);
uint8_t patch_preview_is_prepared(void);
const persist_control_patch_t *patch_preview_get_staging(void);

#endif /* PATCH_PREVIEW_H */
