#ifndef PATTERN_CONTROL_BANK_H
#define PATTERN_CONTROL_BANK_H
#include <stdint.h>
#include "Storage/persistent_control_codec.h"

typedef enum
{
    PATTERN_CONTROL_BANK_ASYNC_NONE = 0,
    PATTERN_CONTROL_BANK_ASYNC_SAVE,
    PATTERN_CONTROL_BANK_ASYNC_LOAD
} pattern_control_bank_async_operation_t;

typedef enum
{
    PATTERN_CONTROL_BANK_PROJECT_LOAD_ERROR = 0,
    PATTERN_CONTROL_BANK_PROJECT_LOAD_OK,
    PATTERN_CONTROL_BANK_PROJECT_LOAD_EMPTY
} pattern_control_bank_project_load_result_t;

void pattern_control_bank_init(void);
uint8_t pattern_control_bank_activate_project(uint8_t slot);
uint8_t pattern_control_bank_validate_project(uint8_t slot);
void pattern_control_bank_deactivate_project(void);
uint8_t pattern_control_bank_active_project(uint8_t *out_slot);
void pattern_control_bank_publish_empty_project(uint8_t slot);
uint8_t pattern_control_bank_delete(uint8_t bank,uint8_t pattern);
uint8_t pattern_control_bank_present(uint8_t bank,uint8_t pattern);
uint16_t pattern_control_bank_count(void);
pattern_control_bank_project_load_result_t pattern_control_bank_load_project(
    uint8_t slot,uint8_t bank,uint8_t pattern,persist_control_pattern_t *out);
uint8_t pattern_control_bank_store_async_begin(
    uint8_t bank,
    uint8_t pattern,
    const persist_control_pattern_t *in,
    uint8_t *encoded,
    uint32_t encoded_capacity);
uint8_t pattern_control_bank_load_async_begin(
    uint8_t bank,
    uint8_t pattern,
    uint8_t *encoded,
    uint32_t encoded_capacity,
    persist_control_pattern_t *out);
void pattern_control_bank_async_service(void);
uint8_t pattern_control_bank_async_busy(void);
uint8_t pattern_control_bank_async_take_result(
    pattern_control_bank_async_operation_t *operation,
    uint8_t *bank,
    uint8_t *pattern,
    uint8_t *success);
#endif
