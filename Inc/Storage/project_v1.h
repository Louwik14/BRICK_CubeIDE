#ifndef PROJECT_V1_H
#define PROJECT_V1_H

#include <stdint.h>

#include "Storage/pattern_live_ram.h"
#include "Sampler/sample_pool.h"

#define PROJECT_V1_BANK_COUNT      16U
#define PROJECT_V1_PATTERN_COUNT   16U
#define PROJECT_V1_SLOT_COUNT      16U
#define PROJECT_V1_MACRO_SCENE_COUNT      16U
#define PROJECT_V1_MACRO_POT_COUNT        4U
#define PROJECT_V1_MACRO_SCENE_LOCK_COUNT 32U
#define PROJECT_V1_MACRO_BANK_COUNT       PROJECT_V1_MACRO_SCENE_COUNT
#define PROJECT_V1_MACRO_PER_BANK         PROJECT_V1_MACRO_POT_COUNT
#define PROJECT_V1_MACRO_SLOT_COUNT       PROJECT_V1_MACRO_SCENE_LOCK_COUNT
#define PROJECT_V1_FILE_MAGIC      0x314A5250UL /* PRJ1 */
#define PROJECT_V1_FILE_VERSION    22U /* Braids Phase Reset param changes PARAM_COUNT-indexed project layout. */

typedef enum
{
    PROJECT_V1_MACRO_HALL_SWITCH_SCENE = 0,
    PROJECT_V1_MACRO_HALL_SWITCH_SWITCH,
    PROJECT_V1_MACRO_HALL_SWITCH_COUNT
} project_v1_macro_hall_switch_mode_t;

#define PROJECT_V1_MACRO_LOCK_TRACK_NONE 0xFFU
#define PROJECT_V1_MACRO_LOCK_PARAM_NONE PARAM_COUNT
#define PROJECT_V1_MACRO_SLOT_TRACK_NONE PROJECT_V1_MACRO_LOCK_TRACK_NONE
#define PROJECT_V1_MACRO_SLOT_PARAM_NONE PROJECT_V1_MACRO_LOCK_PARAM_NONE

typedef enum
{
    PROJECT_V1_ERR_NONE = 0,
    PROJECT_V1_ERR_INVALID_SLOT,
    PROJECT_V1_ERR_INVALID_ARG,
    PROJECT_V1_ERR_ISR_CONTEXT,
    PROJECT_V1_ERR_CAPTURE_FAIL,
    PROJECT_V1_ERR_APPLY_FAIL,
    PROJECT_V1_ERR_SD_LOAD_FAIL,
    PROJECT_V1_ERR_SD_STORE_FAIL,
    PROJECT_V1_ERR_SD_DELETE_FAIL,
    PROJECT_V1_ERR_RECORD_ACTIVE
} project_v1_error_t;

typedef struct
{
    uint8_t active_pattern_bank;
    uint8_t active_pattern_slot;
    uint8_t queued_pattern_valid;
    uint8_t queued_pattern_bank;
    uint8_t queued_pattern_slot;
    uint8_t active_project_slot_valid;
    uint8_t active_project_slot;
    uint8_t active_pattern_index;
    uint8_t bank_has_data[PROJECT_V1_BANK_COUNT][PROJECT_V1_PATTERN_COUNT];
} project_v1_state_block_t;

typedef struct
{
    uint8_t track;
    param_id_t param;
    float scene_value;
} project_v1_macro_lock_t;

typedef project_v1_macro_lock_t project_v1_macro_slot_t;

typedef struct
{
    project_v1_macro_lock_t locks[PROJECT_V1_MACRO_SCENE_LOCK_COUNT];
} project_v1_macro_scene_t;

typedef struct
{
    project_v1_macro_scene_t scenes[PROJECT_V1_MACRO_SCENE_COUNT];
} project_v1_macro_scene_bank_t;

typedef project_v1_macro_scene_t project_v1_macro_t;
typedef project_v1_macro_scene_bank_t project_v1_macro_bank_t;

typedef struct
{
    project_v1_macro_hall_switch_mode_t hall_switch_mode;
    uint8_t macro_scene[PROJECT_V1_MACRO_POT_COUNT];
    project_v1_macro_scene_t scenes[PROJECT_V1_MACRO_SCENE_COUNT];
} project_v1_macro_state_t;

typedef struct
{
    project_v1_state_block_t state;
    sample_pool_project_snapshot_t sample_pool;
    project_v1_macro_state_t macro;
    PatternSaveV1 live;
} ProjectSaveV1;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint16_t bank_count;
    uint16_t pattern_count;
    uint32_t slot_record_size;
    uint32_t pattern_payload_size;
    uint32_t project_slot;
    uint32_t save_counter;
    uint32_t checksum;
} project_v1_file_header_t;

typedef struct __attribute__((packed))
{
    uint8_t bank;
    uint8_t pattern;
    uint8_t has_data;
    uint8_t reserved;
    uint32_t payload_size;
    uint32_t checksum;
} project_v1_slot_record_t;

void project_v1_init(void);
void project_v1_macro_init(void);
project_v1_macro_hall_switch_mode_t project_v1_macro_get_hall_switch_mode(void);
void project_v1_macro_set_hall_switch_mode(project_v1_macro_hall_switch_mode_t mode);
uint8_t project_v1_macro_get_active_bank(void);
void project_v1_macro_set_active_bank(uint8_t bank);
uint8_t project_v1_macro_get_macro_scene(uint8_t macro);
void project_v1_macro_set_macro_scene(uint8_t macro, uint8_t scene);
void project_v1_macro_set_macro_scene_no_sync(uint8_t macro, uint8_t scene);
uint8_t project_v1_macro_scene_has_locks(uint8_t scene);
uint8_t project_v1_macro_assign_scene_lock(uint8_t scene, uint8_t track, param_id_t param, float scene_value);
uint8_t project_v1_macro_clear_scene_lock(uint8_t scene, uint8_t track, param_id_t param);
uint8_t project_v1_macro_get_scene_lock_for_param(uint8_t scene,
                                                  uint8_t track,
                                                  param_id_t param,
                                                  project_v1_macro_lock_t *out_lock);
uint8_t project_v1_macro_scene_lock_is_empty(uint8_t scene, uint8_t lock);
uint8_t project_v1_macro_get_scene_lock(uint8_t scene, uint8_t lock, project_v1_macro_lock_t *out_lock);
uint8_t project_v1_macro_set_scene_lock(uint8_t scene, uint8_t lock, const project_v1_macro_lock_t *in_lock);
uint8_t project_v1_macro_slot_is_empty(uint8_t bank, uint8_t macro, uint8_t slot);
uint8_t project_v1_macro_get_slot(uint8_t bank, uint8_t macro, uint8_t slot, project_v1_macro_slot_t *out_slot);
uint8_t project_v1_macro_set_slot(uint8_t bank,
                                  uint8_t macro,
                                  uint8_t slot,
                                  const project_v1_macro_slot_t *in_slot);
uint8_t project_v1_capture_current(ProjectSaveV1 *out_project);
uint8_t project_v1_apply_snapshot(const ProjectSaveV1 *project, uint8_t resume_transport);
uint8_t project_v1_store_snapshot_to_slot(uint8_t project_slot,
                                          const ProjectSaveV1 *project,
                                          uint8_t mark_active_slot);
uint8_t project_v1_save_slot(uint8_t project_slot);
uint8_t project_v1_load_slot(uint8_t project_slot);
uint8_t project_v1_load_blank(void);
uint8_t project_v1_delete_slot(uint8_t project_slot);
uint8_t project_v1_get_active_slot(uint8_t *out_valid, uint8_t *out_slot);
uint8_t project_v1_slot_has_data(uint8_t project_slot);
void project_v1_refresh_slots(void);
uint8_t project_v1_list_slots(uint8_t *out_slots, uint8_t max_slots);
uint8_t project_v1_restore_boot_context(void);
project_v1_error_t project_v1_get_last_error(void);
const char *project_v1_error_to_string(project_v1_error_t err);
uint8_t project_v1_get_last_sd_error_code(void);

#endif
