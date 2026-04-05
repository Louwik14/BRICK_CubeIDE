#ifndef PROJECT_V1_H
#define PROJECT_V1_H

#include <stdint.h>

#include "Storage/pattern_live_ram.h"

#define PROJECT_V1_BANK_COUNT      16U
#define PROJECT_V1_PATTERN_COUNT   16U
#define PROJECT_V1_SLOT_COUNT      16U
#define PROJECT_V1_FILE_MAGIC      0x314A5250UL /* PRJ1 */
#define PROJECT_V1_FILE_VERSION    1U

typedef struct
{
    uint8_t active_pattern_bank;
    uint8_t active_pattern_slot;
    uint8_t queued_pattern_valid;
    uint8_t queued_pattern_bank;
    uint8_t queued_pattern_slot;
    uint8_t active_project_slot_valid;
    uint8_t active_project_slot;
    uint8_t reserved0;
    uint8_t bank_has_data[PROJECT_V1_BANK_COUNT][PROJECT_V1_PATTERN_COUNT];
} project_v1_state_block_t;

typedef struct
{
    project_v1_state_block_t state;
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
uint8_t project_v1_capture_current(ProjectSaveV1 *out_project);
uint8_t project_v1_apply_snapshot(const ProjectSaveV1 *project, uint8_t resume_transport);
uint8_t project_v1_save_slot(uint8_t project_slot);
uint8_t project_v1_load_slot(uint8_t project_slot);
uint8_t project_v1_delete_slot(uint8_t project_slot);
uint8_t project_v1_get_active_slot(uint8_t *out_valid, uint8_t *out_slot);
uint8_t project_v1_slot_has_data(uint8_t project_slot);

#endif
