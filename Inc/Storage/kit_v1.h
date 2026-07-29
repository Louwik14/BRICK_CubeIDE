#ifndef KIT_V1_H
#define KIT_V1_H

#include <stdint.h>

#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "Sampler/sample_global_pool.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_V1_SLOT_COUNT 128U
#define KIT_V1_NAME_MAX   32U
#define KIT_V1_TRACK_MAX  16U
#define KIT_V1_ASSET_PATH_MAX SAMPLE_GLOBAL_POOL_PATH_MAX
#define KIT_V1_INVALID_SLOT 0xFFFFU

#if UI_TRACK_COUNT > KIT_V1_TRACK_MAX
#error "Kit V1 summary/payload track capacity must cover UI tracks"
#endif

typedef enum
{
    KIT_V1_RESULT_OK = 0,
    KIT_V1_RESULT_INVALID_ARG,
    KIT_V1_RESULT_CAPTURE_FAIL,
    KIT_V1_RESULT_SLOT_FULL,
    KIT_V1_RESULT_SD_BUSY,
    KIT_V1_RESULT_SD_FAIL,
    KIT_V1_RESULT_EMPTY,
    KIT_V1_RESULT_BAD_KIT,
    KIT_V1_RESULT_ASSET_MISS,
    KIT_V1_RESULT_APPLY_FAIL,
    KIT_V1_RESULT_APPLY_TODO,
    KIT_V1_RESULT_RENAME_TODO,
    KIT_V1_RESULT_DELETE_TODO,
    KIT_V1_RESULT_RENAME_FAIL,
    KIT_V1_RESULT_DELETE_FAIL
} kit_v1_result_t;

typedef enum
{
    KIT_V1_LABEL_OFF = 0,
    KIT_V1_LABEL_PR,
    KIT_V1_LABEL_SK,
    KIT_V1_LABEL_RM,
    KIT_V1_LABEL_ST,
    KIT_V1_LABEL_ML,
    KIT_V1_LABEL_LP,
    KIT_V1_LABEL_IN,
    KIT_V1_LABEL_FX,
    KIT_V1_LABEL_BD,
    KIT_V1_LABEL_SN,
    KIT_V1_LABEL_HH,
    KIT_V1_LABEL_WV,
    KIT_V1_LABEL_DY,
    KIT_V1_LABEL_UNKNOWN
} kit_v1_label_code_t;

typedef struct
{
    uint8_t family;
    uint8_t type;
    uint8_t label_code;
    uint8_t off;
} kit_v1_track_summary_t;

typedef struct
{
    uint8_t has_asset;
    uint8_t kind;
    uint16_t global_slot;
    uint16_t backend_index;
    char path[KIT_V1_ASSET_PATH_MAX];
} kit_v1_asset_ref_t;

typedef struct
{
    uint8_t family;
    uint8_t type;
    uint8_t voice_group_role;
    uint8_t voice_group_link;
    uint8_t voice_group_seq_link;
    uint8_t reserved[3];
    float voice_group_spread;
    track_sound_state_t sound;
    track_tone_sound_state_t tone;
    kit_v1_asset_ref_t asset;
} kit_v1_track_payload_t;

typedef struct
{
    char name[KIT_V1_NAME_MAX];
    uint8_t track_count;
    uint8_t reserved[3];
    kit_v1_track_summary_t summary[KIT_V1_TRACK_MAX];
} kit_v1_metadata_t;

typedef struct
{
    kit_v1_metadata_t meta;
    kit_v1_track_payload_t tracks[KIT_V1_TRACK_MAX];
} KitSaveV1;

void kit_v1_init(void);
kit_v1_result_t kit_v1_capture_current(KitSaveV1 *out_kit);
kit_v1_result_t kit_v1_save_direct(uint16_t *out_slot);
kit_v1_result_t kit_v1_apply_slot(uint16_t slot);
kit_v1_result_t kit_v1_rename_slot(uint16_t slot, const char *name);
kit_v1_result_t kit_v1_delete_slot(uint16_t slot, uint16_t *out_next_slot);
void kit_v1_set_current_slot(uint16_t slot);
uint16_t kit_v1_get_current_slot(void);
uint8_t kit_v1_get_current_name(char *out_name, uint32_t out_size);
uint8_t kit_v1_is_dirty(void);
void kit_v1_mark_dirty(void);
void kit_v1_clear_dirty(void);
const char *kit_v1_result_label(kit_v1_result_t result);
const char *kit_v1_label_code_short_name(uint8_t label_code);

#ifdef __cplusplus
}
#endif

#endif /* KIT_V1_H */
