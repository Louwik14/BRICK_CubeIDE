#ifndef PROJECT_V1_H
#define PROJECT_V1_H

#include <stdint.h>

#include "Storage/pattern_live_ram.h"
#include "Sampler/sample_pool.h"

#define PROJECT_V1_BANK_COUNT      16U
#define PROJECT_V1_PATTERN_COUNT   16U
#define PROJECT_V1_SLOT_COUNT      16U
#define PROJECT_V1_MACRO_BANK_COUNT 16U
#define PROJECT_V1_MACRO_PER_BANK   4U
#define PROJECT_V1_MACRO_SLOT_COUNT 4U
#define PROJECT_V1_FILE_MAGIC      0x314A5250UL /* PRJ1 */
#define PROJECT_V1_FILE_VERSION    10U /* Opal replaces the public Plaits TONE surface and changes PARAM_COUNT; legacy project files are intentionally not kept compatible in prototype phase */

typedef enum
{
    PROJECT_V1_MACRO_HALL_SWITCH_SLOT = 0,
    PROJECT_V1_MACRO_HALL_SWITCH_BANK,
    PROJECT_V1_MACRO_HALL_SWITCH_COUNT
} project_v1_macro_hall_switch_mode_t;

#define PROJECT_V1_MACRO_SLOT_TRACK_NONE 0xFFU
#define PROJECT_V1_MACRO_SLOT_PARAM_NONE PARAM_COUNT

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
    PROJECT_V1_ERR_SD_DELETE_FAIL
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
} project_v1_macro_slot_t;

typedef struct
{
    project_v1_macro_slot_t slots[PROJECT_V1_MACRO_SLOT_COUNT];
} project_v1_macro_t;

typedef struct
{
    project_v1_macro_t macros[PROJECT_V1_MACRO_PER_BANK];
} project_v1_macro_bank_t;

typedef struct
{
    project_v1_macro_hall_switch_mode_t hall_switch_mode;
    uint8_t active_bank;
    project_v1_macro_bank_t banks[PROJECT_V1_MACRO_BANK_COUNT];
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
