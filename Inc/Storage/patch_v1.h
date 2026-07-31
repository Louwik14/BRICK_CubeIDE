#ifndef PATCH_V1_H
#define PATCH_V1_H

#include <stdint.h>

#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "Sampler/sample_global_pool.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PATCH_V1_SLOT_COUNT 192U
#define PATCH_V1_NAME_MAX   32U
#define PATCH_V1_ASSET_PATH_MAX SAMPLE_GLOBAL_POOL_PATH_MAX
#define PATCH_V1_INVALID_SLOT 0xFFFFU

typedef enum
{
    PATCH_V1_RESULT_OK = 0,
    PATCH_V1_RESULT_INVALID_ARG,
    PATCH_V1_RESULT_CAPTURE_FAIL,
    PATCH_V1_RESULT_SLOT_FULL,
    PATCH_V1_RESULT_SD_BUSY,
    PATCH_V1_RESULT_SD_FAIL,
    PATCH_V1_RESULT_EMPTY,
    PATCH_V1_RESULT_BAD_PATCH,
    PATCH_V1_RESULT_ASSET_MISS,
    PATCH_V1_RESULT_APPLY_FAIL,
    PATCH_V1_RESULT_RENAME_FAIL,
    PATCH_V1_RESULT_DELETE_FAIL,
    PATCH_V1_RESULT_VOICE_LIMITED,
    PATCH_V1_RESULT_VOICE_MAX
} patch_v1_result_t;

typedef struct
{
    uint8_t has_asset;
    uint8_t kind;
    uint16_t global_slot;
    uint16_t backend_index;
    char path[PATCH_V1_ASSET_PATH_MAX];
} patch_v1_asset_ref_t;

typedef struct
{
    char name[PATCH_V1_NAME_MAX];
    uint8_t family;
    uint8_t type;
    uint8_t source_track;
    uint8_t summary_family;
    uint8_t summary_type;
    uint8_t topology_role;
} patch_v1_metadata_t;

typedef struct
{
    uint8_t family;
    uint8_t type;
    uint8_t synth_voice_count;
    uint8_t reserved;
    track_sound_state_t sound;
    track_tone_sound_state_t tone;
    patch_v1_asset_ref_t asset;
} patch_v1_track_t;

typedef struct
{
    patch_v1_metadata_t meta;
    patch_v1_track_t track;
} PatchSaveV1;

void patch_v1_init(void);
patch_v1_result_t patch_v1_capture_track(uint8_t track, PatchSaveV1 *out_patch);
patch_v1_result_t patch_v1_save_track_direct(uint8_t track, uint16_t *out_slot);
patch_v1_result_t patch_v1_apply_slot_to_track(uint16_t slot, uint8_t target_track);
patch_v1_result_t patch_v1_rename_slot(uint16_t slot, const char *name);
patch_v1_result_t patch_v1_delete_slot(uint16_t slot, uint16_t *out_next_slot);
void patch_v1_set_current_slot(uint16_t slot);
uint16_t patch_v1_get_current_slot(void);
const char *patch_v1_result_label(patch_v1_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_V1_H */
