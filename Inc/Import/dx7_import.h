#ifndef BRICK6_DX7_IMPORT_H
#define BRICK6_DX7_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#include "Import/dx7_sysex.h"
#include "Storage/persistent_control_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DX7_IMPORT_OK = 0,
    DX7_IMPORT_INVALID_ARGUMENT,
    DX7_IMPORT_INVALID_VOICE,
    DX7_IMPORT_KEY_ERROR
} dx7_import_result_t;

dx7_import_result_t dx7_import_voice(const dx7_voice_t *voice,
                                     uint8_t destination_entity,
                                     persist_control_patch_t *out_patch);
dx7_import_result_t dx7_import_voices(const dx7_voice_t *voices,
                                      size_t voice_count,
                                      uint8_t destination_entity,
                                      persist_control_patch_t *patches,
                                      size_t patch_capacity);

float dx7_import_lfo_frequency_hz(uint8_t speed);
float dx7_import_pitch_depth_semitones(uint8_t depth, uint8_t sensitivity);

#ifdef __cplusplus
}
#endif

#endif
